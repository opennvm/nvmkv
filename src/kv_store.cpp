//----------------------------------------------------------------------------
// NVMKV
// |- Copyright 2012-2013 Fusion-io, Inc.

// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License version 2 as published by the Free
// Software Foundation;
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General
// Public License v2 for more details.
// A copy of the GNU General Public License v2 is provided with this package and
// can also be found at: http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 59 Temple
// Place, Suite 330, Boston, MA 02111-1307 USA.
//----------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "src/kv_store.h"
#include "util/kv_util.h"
#include "src/kv_wrappers.h"
#include "src/kv_pool_del_manager.h"
#include "src/kv_expiry_manager.h"
#include "src/kv_async_expiry.h"
#include "src/kv_iterator.h"
using namespace nvm_wrapper;

//
//initializes KV store device
//
NVM_KV_Store::NVM_KV_Store()
{
    m_pKvDevice      = NULL;
    m_pPoolManager   = NULL;
    m_pHashFunc      = NULL;
    m_pKvLayout      = NULL;
    m_pStoreMetadata = NULL;
    m_pExpiryThread  = NULL;
    m_pAsyncExpiry   = NULL;
    m_iter           = NULL;
    m_exp_status     = false;
}
//
//@destroys memory allocated for expiry thread, pool manager, KV store layout
//
NVM_KV_Store::~NVM_KV_Store()
{
    //cancel threads before proceeding
    //with destruction
    if (m_pExpiryThread)
    {
        m_pExpiryThread->cleanup_threads();
    }
    if (m_pAsyncExpiry)
    {
        m_pAsyncExpiry->cleanup_threads();
    }
    if (m_pPoolManager)
    {
        m_pPoolManager->cancel_pool_del_scanner();
    }
    delete m_pExpiryThread;
    delete m_pAsyncExpiry;
    delete m_pPoolManager;
    delete m_pKvLayout;
    delete m_pHashFunc;
    delete m_pKvDevice;
    delete m_iter;
    m_buffer_pool.release_buf((char *)m_pStoreMetadata,
        m_meta_data_buf_len);
}
//
//initializes KV store, creates its layout and poolManager
//
int NVM_KV_Store::initialize(int kv_id, nvm_handle_t handle,
                             nvm_kv_store_capabilities_t cap,
                             uint32_t sparse_addr_bits,
                             uint32_t max_pools, uint32_t version,
                             uint32_t expiry)
{
    uint32_t m_meta_data_buf_len = 0; //read write length
    uint32_t ret_rw_len = 0; //read write length
    char *buf = NULL; //buffer for metadata
    nvm_kv_store_metadata_t *metadata = NULL;
    int ret_code = 0;
    nvm_iovec_t iovec[2];
    int iov_count = 0;
    bool nvm_batch_op = true; //flag to be passed in nvm_writev
    uint64_t kv_stamp = 0;
    uint32_t sector_size;
    uint32_t pool_bits = 0;
    int all_poolid = 0;
    NVM_KV_Layout *layout = NULL;
    bool create_new = false; //whether KV store has been created newly
    uint32_t pool_tag_size = 0;
    uint32_t slice_addr = 0;
    uint64_t max_val_range = 0;

    m_pKvDevice = new(std::nothrow) nvm_kv_store_device_t;
    if (m_pKvDevice == NULL)
    {
        return -NVM_ERR_OUT_OF_MEMORY;
    }
    m_pKvDevice->nvm_handle = handle;
    m_pKvDevice->fd = kv_id;
    m_pKvDevice->capabilities = cap;


    //create KV store layout object
    m_pKvLayout = new(std::nothrow) NVM_KV_Layout(
                    m_pKvDevice->capabilities.nvm_sector_size,
                    sparse_addr_bits);
    if (!m_pKvLayout)
    {
        fprintf(stderr, "Error, allocating layout\n");
        return -NVM_ERR_OUT_OF_MEMORY;
    }

    max_val_range = m_pKvLayout->get_max_val_range();
    sector_size = get_sector_size();
    pool_tag_size = sizeof(nvm_kv_pool_tag_t) * max_pools;
    pool_tag_size = nvm_kv_round_upto_blk(pool_tag_size, sector_size);
    pool_tag_size = nvm_kv_round_upto_blk(pool_tag_size, max_val_range);
    //calculate the address that needs to be carved by hashing function,
    //accounting for metadata, in-use bitmap, deleted bitmap and pool_tags
    slice_addr = (pool_tag_size / max_val_range) + 1;
    m_pKvLayout->set_md_rec(slice_addr);

    m_pHashFunc = new(std::nothrow) NVM_KV_Hash_Func();
    if (!m_pHashFunc)
    {
        fprintf(stderr, "Error, allocating for hash function(s)\n");
        return -NVM_ERR_OUT_OF_MEMORY;
    }

    if (m_pHashFunc->initialize(FNV1A, SEQUENTIAL_PROBE,
                                sparse_addr_bits, slice_addr) != 0)
    {
        fprintf(stderr, "Error, hash initialization failed\n");
        return -NVM_ERR_INTERNAL_FAILURE;
    }


    //check if LBA M_KV_METADATA_LBA already exists,
    //if not exists KV store should be newly created
    //else KV store is already present, verify its metadata

    ret_code = nvm_block_exists(m_pKvDevice->nvm_handle,
                                layout->get_metadata_lba());
    if (ret_code == -1)
    {
        //errno set by nvm_block_exists will be re-mapped to
        //NVM_ERR_INTERNAL_FAILURE
        return -NVM_ERR_INTERNAL_FAILURE;
    }
    else if (ret_code == 0)
    {
        //metadata block does not exist, create a new store
        create_new = true;
    }
    else
    {
        //metadata block exists, do not create new store
        create_new = false;
    }

    m_pPoolManager = new(std::nothrow) NVM_KV_Pool_Mgr();
    if (!m_pPoolManager)
    {
        fprintf(stderr, "Error allocating pool manager\n");
        return -NVM_ERR_OUT_OF_MEMORY;
    }

    //do all initialization of KV store object
    m_meta_data_buf_len =
        nvm_kv_round_upto_blk(sizeof(nvm_kv_store_metadata_t), sector_size);
    if ((ret_code = m_buffer_pool.initialize(M_MAX_BUFFERS_IN_POOL,
        m_meta_data_buf_len, sector_size))
        != NVM_SUCCESS)
    {
        fprintf(stderr, "Error, cannot allocate buffer for metadata\n");
        return ret_code;
    }
    //this buffer is allocated for metadata, it need not be freed
    //will be freed in destructor
    buf = m_buffer_pool.get_buf(m_meta_data_buf_len, m_meta_data_buf_len);

    metadata = (nvm_kv_store_metadata_t *) buf;

    iovec[iov_count].iov_base = (uint64_t) buf;
    iovec[iov_count].iov_len = m_meta_data_buf_len;
    iovec[iov_count].iov_lba = m_pKvLayout->get_metadata_lba();
    iovec[iov_count].iov_opcode = NVM_IOV_WRITE;
    iov_count++;

    kv_stamp = m_pKvLayout->get_kv_stamp();
    if (create_new)
    {
        //check if max_pools is more than supported max pool
        //if so return error, this check needs to be done only for first time
        if (max_pools > NVM_KV_MAX_POOLS)
        {
            fprintf(stderr, "max_pools exceeds maximum pools supported \n");
            return -NVM_ERR_INVALID_INPUT;
        }

        if (expiry > KV_GLOBAL_EXPIRY)
        {
            //Supported values of expiry are KV_DISABLE_EXPIRY,
            //KV_ARBITRARY_EXPIRY and KV_GLOBAL_EXPIRY
            fprintf(stderr, "Error, expiry mode not supported\n");
            return -NVM_ERR_INVALID_INPUT;
        }

        //write KV store metadata
        metadata->kv_store_id = kv_id;
        metadata->kv_store_stamp = kv_stamp;
        metadata->version = version;
        metadata->max_pools = max_pools;
        metadata->max_key_size = NVM_KV_MAX_KEY_SIZE;
        //sector is accounted for kv_header and key
        metadata->max_value_size = NVM_KV_MAX_VALUE_SIZE;
        metadata->kv_revision = M_KV_REVISION;
        //when KV store is created, number of pools will be zero
        metadata->total_no_pools = 0;
        metadata->expiry_mode = expiry;
        metadata->global_expiry = 0;

        //writing for iterator marker
        ret_rw_len = nvm_writev(m_pKvDevice,
                                iovec, iov_count, nvm_batch_op);
        if (ret_rw_len < 0)
        {
            fprintf(stderr, "Error, writing metadata failed\n");
            return ret_rw_len;
        }
    }
    else
    {
        ret_rw_len = nvm_readv(m_pKvDevice, iovec,
                               iov_count);
        if (ret_rw_len < 0)
        {
            fprintf(stderr,"Error, reading metadata\n");
            return ret_rw_len;
        }
        if (metadata->version != version || metadata->kv_store_stamp != \
            kv_stamp)
        {
            fprintf(stderr, "Error, verification of KV store metadata"
                    "failed\n");
            return -NVM_ERR_INVALID_INPUT;
        }
    }

    m_pStoreMetadata = metadata;
    m_deleteSectorCount = ((m_pKvLayout->get_max_val_range()) /  sector_size);
    ret_code = m_pPoolManager->initialize(m_pKvDevice, m_pKvLayout,
                                          m_pHashFunc, m_pStoreMetadata,
                                          create_new);
    if (ret_code < 0)
    {
        fprintf(stderr, "Error, pool manager initialize failed\n");
        return -NVM_ERR_INTERNAL_FAILURE;
    }

    //create iterator object
    pool_bits = m_pKvLayout->get_val_bits() - 1;
    all_poolid = m_pPoolManager->get_all_poolid();

    m_iter = new(std::nothrow) NVM_KV_Iterator(this, pool_bits, max_pools,
                                 all_poolid);
    if (!m_iter)
    {
        fprintf(stderr, "Error, allocating kvstore iterator\n");
        return -NVM_ERR_OUT_OF_MEMORY;
    }

    if ((ret_code = m_iter->initialize()) < 0)
    {
        fprintf(stderr, "Error, iterator initialization failed\n");
        return ret_code;
    }

    ret_code = init_expiry_scanners();
    return ret_code;
}
//
//initialises expiry related scanner and pool delete scanner
//
int NVM_KV_Store::init_expiry_scanners()
{
    int ret_code = 0;
    int num_threads = 1;

    //create expiry thread
    if (m_pStoreMetadata->expiry_mode != KV_DISABLE_EXPIRY)
    {
        m_pExpiryThread = new(std::nothrow)
            NVM_KV_Expiry_Manager(num_threads, this);
        if (!m_pExpiryThread)
        {
            fprintf(stderr, "Error, allocating expiry manager\n");
            return -NVM_ERR_OUT_OF_MEMORY;
        }

        if (m_pStoreMetadata->expiry_mode == KV_ARBITRARY_EXPIRY)
        {
            ret_code = m_pExpiryThread->initialize(KV_ARB_EXP_ITER);
        }
        else
        {
            ret_code = m_pExpiryThread->initialize(KV_GLB_EXP_ITER);
        }

        if (ret_code < 0)
        {
            return ret_code;
        }

        //create asynchronous deletion thread for expired keys
        num_threads = 4;
        m_pAsyncExpiry = new(std::nothrow)
            NVM_KV_Async_Expiry(num_threads, this);
        if (!m_pAsyncExpiry)
        {
            fprintf(stderr, "Error, allocating asynchronous expiry\n");
            return -NVM_ERR_OUT_OF_MEMORY;
        }

        ret_code = m_pAsyncExpiry->initialize(0);
        if (ret_code < 0)
        {
            return ret_code;
        }
        m_exp_status = true;
    }
    ret_code = m_pPoolManager->init_pool_del_scanner(this);

    return ret_code;
}
//
//checks if expiry scanners are running
//
bool NVM_KV_Store::expiry_status()
{
    return m_exp_status;
}
//
//gets the pool manager object
//
NVM_KV_Pool_Mgr* NVM_KV_Store::get_pool_mgr()
{
    return m_pPoolManager;
}
//
//gets the pool manager object
//
NVM_KV_Layout* NVM_KV_Store::get_layout()
{
    return m_pKvLayout;
}
//
//fetches maximum kv pairs that can fit in one
//batch operation
//
uint32_t NVM_KV_Store::get_max_batch_size()
{
    uint32_t batch_size = 0;

    batch_size = m_pKvDevice->capabilities.nvm_max_num_iovs;
    //account for kv metadata
    return (batch_size / 2);
}
//
//gets hash function's object
//
NVM_KV_Hash_Func* NVM_KV_Store::get_hash_func()
{
    return m_pHashFunc;
}
//
//gets the KV store device object
//
nvm_kv_store_device_t* NVM_KV_Store::get_store_device()
{
    return m_pKvDevice;
}
//
//number of sectors to be deleted for a 2MiB range based on sector size
//
uint64_t NVM_KV_Store::get_del_sec_count()
{
    return m_deleteSectorCount;
}
//
//gets the KV store metadata
//
nvm_kv_store_metadata_t* NVM_KV_Store::get_store_metadata()
{
    return m_pStoreMetadata;
}
//
//get the sector size of the underlying device
//
uint32_t NVM_KV_Store::get_sector_size()
{
    return m_pKvDevice->capabilities.nvm_sector_size;
}
//
//returns the delete expiry instance associated with the KV store
//
NVM_KV_Scanner* NVM_KV_Store::get_async_expiry_thread()
{
    return m_pAsyncExpiry;
}
//
//returns the expiry instance associated with the KV store
//
NVM_KV_Scanner* NVM_KV_Store::get_expiry_thread()
{
    return m_pExpiryThread;
}
//
//gets the expiry mode of the KV store
//
uint32_t NVM_KV_Store::get_expiry()
{
    return m_pStoreMetadata->expiry_mode;
}
//
//fetches iterator object which holds all iterators of KV store
//
NVM_KV_Iterator* NVM_KV_Store::get_iter()
{
    return m_iter;
}
//
//get the capability values from the underlying device
//and also validate if the capabilities are sufficient to
//support KV store
//
int NVM_KV_Store::initialize_capabilities(
                    nvm_handle_t handle,
                    nvm_kv_store_capabilities_t *kv_cap)
{
    int cap_count = M_CAP_COUNT;
    struct nvm_capability cap_list[cap_count];
    int ret_val = -NVM_ERR_FEATURE_NOT_SUPPORTED;

    memset(kv_cap, 0, sizeof(nvm_kv_store_capabilities_t));

    cap_list[0].cap_id = NVM_CAP_FEATURE_SPARSE_ADDRESSING_ID;
    cap_list[1].cap_id = NVM_CAP_FEATURE_ATOMIC_WRITE_ID;
    cap_list[2].cap_id = NVM_CAP_FEATURE_ATOMIC_TRIM_ID;
    cap_list[3].cap_id = NVM_CAP_ATOMIC_MAX_IOV_ID;
    cap_list[4].cap_id = NVM_CAP_ATOMIC_WRITE_MULTIPLICITY_ID;
    cap_list[5].cap_id = NVM_CAP_ATOMIC_WRITE_MAX_VECTOR_SIZE_ID;
    cap_list[6].cap_id = NVM_CAP_SECTOR_SIZE_ID;
    cap_list[7].cap_id = NVM_CAP_LOGICAL_ITER_MAX_NUM_RANGES_ID;
    cap_list[8].cap_id = NVM_CAP_ATOMIC_TRIM_MAX_VECTOR_SIZE_ID;

    ret_val = nvm_get_capabilities(handle, cap_list, cap_count, false);
    if (ret_val == cap_count)
    {
        if (!(cap_list[0].cap_value & NVM_CAP_FEATURE_ENABLED)
            || !(cap_list[1].cap_value & NVM_CAP_FEATURE_ENABLED)
            || !(cap_list[2].cap_value & NVM_CAP_FEATURE_ENABLED))
        {
            return -NVM_ERR_CAPABILITIES_NOT_SUPPORTED;
        }

        kv_cap->nvm_max_num_iovs = cap_list[3].cap_value;
        kv_cap->nvm_atomic_write_multiplicity = cap_list[4].cap_value;
        kv_cap->nvm_max_write_size_per_iov = cap_list[5].cap_value;
        kv_cap->nvm_max_trim_size_per_iov = cap_list[8].cap_value;
        kv_cap->nvm_sector_size = cap_list[6].cap_value;
        kv_cap->nvm_max_num_logical_iter_ranges = cap_list[7].cap_value;
    }
    else
    {
         fprintf(stderr, "Error, failed to get device capabilities\n");
         return -NVM_ERR_INTERNAL_FAILURE;
    }

    return NVM_SUCCESS;
}
//
//writes metadata atomically
//
int NVM_KV_Store::persist_kv_metadata()
{
    nvm_iovec_t iovec;
    int ret_code = NVM_SUCCESS;
    uint32_t sector_size = get_sector_size();

    //Update the metadata
    iovec.iov_base = (uint64_t) m_pStoreMetadata;
    iovec.iov_len = nvm_kv_round_upto_blk(
                    sizeof(*m_pStoreMetadata),
                    sector_size);
    iovec.iov_lba = m_pKvLayout->get_metadata_lba();
    iovec.iov_opcode = NVM_IOV_WRITE;

    ret_code = nvm_writev(m_pKvDevice, &iovec, 1, true);
    if (ret_code < 0)
    {
        fprintf(stderr, "Error, atomic write failed: %d\n", errno);
        return ret_code;
    }

    return NVM_SUCCESS;
}
//
//insert lba to the safe lba list
//
bool NVM_KV_Store::insert_lba_to_safe_list(uint64_t lba, bool *wait)
{
    return m_safe_lba_list.insert_entry(lba, wait);
}
//
//deletes lba from the safe lba list
//
bool NVM_KV_Store::delete_lba_from_safe_list(uint64_t lba)
{
    return m_safe_lba_list.delete_entry(lba);
}
