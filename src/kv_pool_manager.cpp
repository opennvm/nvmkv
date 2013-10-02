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
#include <string.h>
#include <errno.h>
#include "src/kv_pool_manager.h"
#include "src/kv_common.h"
#include "util/kv_util.h"
#include "src/kv_store.h"
#include "src/kv_pool_del_manager.h"
#include "util/kv_hash_func.h"
#include "src/kv_wrappers.h"
using namespace nvm_wrapper;
//
//initializes bitmaps and bitmap mutex
//
NVM_KV_Pool_Mgr::NVM_KV_Pool_Mgr()
{
    pthread_mutex_init(&m_bitmaps.mutex, NULL);
    pthread_mutex_init(&m_glb_mtx, NULL);
    pthread_cond_init(&m_glb_cond, NULL);
    //objects that are instantiated inside NVM_KV_Pool_Mgr class
    m_bitmaps.in_use = NULL;
    m_bitmaps.deleted = NULL;
    m_pPoolTags = NULL;
    m_pPoolDelThread = NULL;
}
//
//destroys memory related to pool bitmaps
//
NVM_KV_Pool_Mgr::~NVM_KV_Pool_Mgr()
{
    delete m_pPoolDelThread;
    pthread_mutex_destroy(&m_bitmaps.mutex);
    pthread_cond_destroy(&m_glb_cond);
    pthread_mutex_destroy(&m_glb_mtx);
    bitmap_free(m_bitmaps.in_use);
    bitmap_free(m_bitmaps.deleted);
    m_tags_map.erase(m_tags_map.begin(), m_tags_map.end());
    m_buffer_pool.release_buf((char *)m_pPoolTags, m_poolTagSize);
}
//
//cancel pool delete scanner
//
void NVM_KV_Pool_Mgr::cancel_pool_del_scanner()
{
    if (m_pPoolDelThread)
    {
        m_pPoolDelThread->cleanup_threads();
    }
}
//
//initializes pool manager data set
//
int NVM_KV_Pool_Mgr::initialize(nvm_kv_store_device_t *kv_device,
                                NVM_KV_Layout *layout,
                                NVM_KV_Hash_Func *hash_func,
                                nvm_kv_store_metadata_t *metadata,
                                bool create_new)
{
    int ret_code = NVM_SUCCESS;
    uint32_t del_bm_lba = 0;
    uint32_t bm_size = 0;
    int sector_size = 0;

    if (!kv_device || !layout || !metadata)
    {
        return -NVM_ERR_INVALID_INPUT;
    }

    m_pStoreMetadata = metadata;
    m_pKvDevice = kv_device;
    m_pLayout = layout;
    m_pHashFunc = hash_func;

    if (metadata->max_pools == 0)
    {
        //There are no pools to be created/maintained.
        //Just return success in such case.
        return NVM_SUCCESS;
    }

    sector_size = get_sector_size();
    //allocate memory for pool tags
    m_poolTagSize = sizeof(nvm_kv_pool_tag_t) * m_pStoreMetadata->max_pools;
    m_poolTagSize = nvm_kv_round_upto_blk(m_poolTagSize, sector_size);

    if ((ret_code = m_buffer_pool.initialize(M_MAX_BUFFERS_IN_POOL,
                        m_poolTagSize, sector_size)) != NVM_SUCCESS)
    {
        fprintf(stderr, "memory allocation failed in pool manager\n");
        return ret_code;
    }
    m_pPoolTags = (nvm_kv_pool_tag_t *)m_buffer_pool.get_buf(m_poolTagSize, m_poolTagSize);

    m_bitmaps.in_use = bitmap_alloc_aligned(m_pStoreMetadata->max_pools,
                                            sector_size);
    if (!m_bitmaps.in_use)
    {
        fprintf(stderr, "Error, in-use bitmap allocation failed!\n");
        return -NVM_ERR_OUT_OF_MEMORY;
    }
    m_bitmaps.deleted = bitmap_alloc_aligned(m_pStoreMetadata->max_pools,
                                             sector_size);
    if (!m_bitmaps.deleted)
    {
        fprintf(stderr, "Error, deleted bitmap allocation failed!\n");
        return -NVM_ERR_OUT_OF_MEMORY;
    }

    //pool id 0 is in use for default pools
    bitmap_set(m_bitmaps.in_use, 0);
    bm_size = bits_per_uint8(round32(m_pStoreMetadata->max_pools));
    del_bm_lba = m_pLayout->get_inuse_bm_lba() +
                 (nvm_kv_round_upto_blk(bm_size, sector_size) / sector_size);
    m_pLayout->set_del_bm_lba(del_bm_lba);

    if (create_new)
    {
        //Write the metadata to the media consistently
        ret_code =
            persist_store_info((M_KV_BM_INUSE |
                                M_KV_BM_DELETED | M_KV_POOL_TAGS), -1);
    }
    else
    {
        ret_code = restore();
    }


    return ret_code;
}
//
//initializes pool deletion scanner
//
int NVM_KV_Pool_Mgr::init_pool_del_scanner(NVM_KV_Store *kv_store)
{
    int num_threads = 1;

    //create pool deletion thread, that deletes pool
    //asynchronously
    m_pPoolDelThread = new(std::nothrow)
        NVM_KV_Pool_Del_Manager(num_threads, kv_store);
    if (!m_pPoolDelThread)
    {
        fprintf(stderr, "Error, allocating pool deletion manager\n");
        return -NVM_ERR_OUT_OF_MEMORY;
    }
    return m_pPoolDelThread->initialize(KV_POOL_DEL_ITER);
}
//
//load pools specific information from media onto memory
//
int NVM_KV_Pool_Mgr::restore()
{
    //load bitmaps from persistent media
    nvm_iovec_t *iovec = NULL;
    int ret_code = NVM_SUCCESS;
    int num_iovs = 2;//only 2 IOVs are needed for the read
    int iov_count = 0;
    kv_delete_map_entry_t *entry = NULL;
    uint32_t pool_hash = 0;
    uint32_t sector_size = get_sector_size();

    //maximum number of IOVs that is supported by the device is allocated
    //pool tags takes more than one IOV based on pool size
    iovec = new(std::nothrow) nvm_iovec_t[num_iovs];
    if (iovec == NULL)
    {
        fprintf(stderr, "Error, iovec allocation failed; \
            before restoring bitmaps\n");
        return -NVM_ERR_OUT_OF_MEMORY;
    }
    iovec[iov_count].iov_base = (uint64_t) m_bitmaps.in_use;
    iovec[iov_count].iov_len = nvm_kv_round_upto_blk( \
                               bits_per_uint8(
                               round32(m_pStoreMetadata->max_pools)),
                               sector_size);
    iovec[iov_count].iov_lba = m_pLayout->get_inuse_bm_lba();
    iov_count++;
    iovec[iov_count].iov_base = (uint64_t) m_bitmaps.deleted;
    iovec[iov_count].iov_len = nvm_kv_round_upto_blk( \
                               bits_per_uint8(
                               round32(m_pStoreMetadata->max_pools)),
                               sector_size);
    iovec[iov_count].iov_lba = m_pLayout->get_del_bm_lba();
    iov_count++;
    ret_code = nvm_readv(m_pKvDevice, iovec, iov_count);
    if (ret_code < 0)
    {
        fprintf(stderr, "Error, read from media failed, %d\n",
                errno);
        goto end_restore;
    }
    //pool tags are read as separate reads because many platform and
    //read system calls does not allow reading from multiple offsets
    iov_count = 0;
    iovec[iov_count].iov_base = (uint64_t) m_pPoolTags;
    iovec[iov_count].iov_len = m_poolTagSize;
    iovec[iov_count].iov_lba = m_pLayout->get_pool_tags_lba();
    iov_count++;
    ret_code = nvm_readv(m_pKvDevice, iovec, iov_count);
    if (ret_code < 0)
    {
        fprintf(stderr, "Error, read from media failed, %d\n",
                errno);
        goto end_restore;
    }

    for (int i = 1; i < m_pStoreMetadata->max_pools; i++)
    {
        nvm_kv_pool_tag_t pool_tag;
        string pool_tag_str;

        //if pool_id is in use, add pool_tag for the pool_id in pool_tag map
        get_pool_tag(i, &pool_tag);
        pool_tag_str.assign((char *) &pool_tag);
        if (!pool_tag_str.empty())
        {
            m_tags_map.insert(std::pair<string, int>(pool_tag_str, i));
        }
        //If there is any bit set in the deleted bitmap, that means that there was
        //deletion of some pools in progress and hence we should add those in
        //hash map for pools in deletion so that scanner considers them for
        //deletion
        if (bitmap_test(m_bitmaps.deleted, i))
        {
            //This pool id was marked for deletion. Make sure that it is
            //set in the in-use bitmap as well and add an entry corresponding
            //to it in hash map for pools in deletion.
            if (bitmap_test(m_bitmaps.in_use, i))
            {
                pool_hash = get_poolid_hash(i);
                entry = new(std::nothrow) kv_delete_map_entry_t();
                if (entry == NULL)
                {
                    ret_code = -NVM_ERR_OUT_OF_MEMORY;
                    goto end_restore;
                }
                m_delete_pools[pool_hash] = entry;

                //This pool_id starts out having seen 0 scanner passes
                //through drive
                (*entry)[i] = 0;
            }
            else
            {
                //As the in-use bitmap does not have this pool id set,
                //there is some inconsistency problem as we expect both in-use
                //and deleted bitmap bits set if a particular pool is in the
                //process of deletion.
                fprintf(stderr, "Error, inconsistent state; \
                        while restoring deleted bitmap\n");
                ret_code = -NVM_ERR_INTERNAL_FAILURE;
                goto end_restore;
            }
        }
    }

end_restore:
    delete iovec;
    return ret_code;
}
//
//creates pool, writes in-use bitmap to media
//
int NVM_KV_Pool_Mgr::create_pool(nvm_kv_pool_tag_t *pool_tag)
{
    int pool_id = -1;
    int retval = NVM_SUCCESS;
    std::map<string, int>::iterator tag_itr;
    string pool_tag_str;

    if (!pool_tag)
    {
        pool_tag_str.assign("");
    }
    else
    {
        pool_tag_str.assign((char *) pool_tag);
    }
    pthread_mutex_lock(&m_bitmaps.mutex);
    //if provided pool_tag already exist in the map then
    //return pool_id associated with the tag
    if (!pool_tag_str.empty() && !m_tags_map.empty() &&
        (tag_itr = m_tags_map.find(pool_tag_str)) != m_tags_map.end())
    {
        pool_id = tag_itr->second;
        goto end_pool_manager_create;
    }
    if(m_pStoreMetadata->total_no_pools++ > m_pStoreMetadata->max_pools)
    {
        fprintf(stderr, "maximum pools reached\n");
        retval = -NVM_ERR_INVALID_INPUT;
        goto end_pool_manager_create;
    }
    pool_id = bitmap_ffa(m_bitmaps.in_use, m_pStoreMetadata->max_pools);
    if (pool_id == BM_FULL)
    {
        fprintf(stderr, "maximum pools reached\n");
        retval = -NVM_ERR_INTERNAL_FAILURE;
        goto end_pool_manager_create;
    }
    if (set_pool_tag(pool_id, pool_tag) < 0)
    {
        retval = -NVM_ERR_INTERNAL_FAILURE;
        goto end_pool_manager_create;
    }

    //Write the metadata to the media consistently
    retval =
        persist_store_info((M_KVSTORE_METADATA | M_KV_BM_INUSE |
                            M_KV_POOL_TAGS), pool_id);
    if (!pool_tag_str.empty())
    {
        m_tags_map.insert(std::pair<string, int>(pool_tag_str, pool_id));
    }

end_pool_manager_create:
    if (retval < 0)
    {
        //As write fails, we need to revert the bitmap and clear the bit
        //set for this pool_id
        if (pool_id > 0)
        {
            bitmap_clear(m_bitmaps.in_use, pool_id);
        }
        m_pStoreMetadata->total_no_pools--;
        pool_id = retval;
        //Write to media failed, revert back the total number of pools
    }
    pthread_mutex_unlock(&m_bitmaps.mutex);
    return pool_id;
}
//
//Mark a pool/all pools for deletion and restart the scanner
//NOTICE: This is an asynchronous operation. Deletes do not occur
//immediately once this function is called.
//
int NVM_KV_Pool_Mgr::delete_pool(int pool_id)
{
    int retval = NVM_SUCCESS;
    kv_delete_map_entry_t *entry = NULL;
    kv_delete_map_t::iterator it;
    uint32_t pool_hash = 0;
    int i = 0;

    //Go over all the pools existing in the in-use bitmap and mark them in
    //in delete bitmap, skip the default pool id i.e, 0.
    if (pool_id == M_POOLID_ALL)
    {
        i = 1;
    }
    else
    {
        //If the call is for deleting a specific pool
        //and pool id is not valid return error
        int pool_id_status = check_pool_status(pool_id, false);

        if (pool_id_status == POOL_IS_INVALID || pool_id_status ==
            POOL_NOT_IN_USE)
        {
            return -NVM_ERR_INVALID_INPUT;
        }
        //if pool is already in process of deletion, return
        //success
        if (pool_id_status == POOL_DELETION_IN_PROGRESS)
        {
            return NVM_SUCCESS;
        }
        i = pool_id;
    }

    m_pPoolDelThread->lock_mutex();
    pthread_mutex_lock(&m_bitmaps.mutex);
    do
    {
        if (bitmap_test(m_bitmaps.in_use, i))
        {
            nvm_kv_pool_tag_t pool_tag;
            char *pool_tag_offset = 0;

            pool_tag_offset = (char *) m_pPoolTags + i *
                sizeof(nvm_kv_pool_tag_t);
            memcpy(&pool_tag, pool_tag_offset, sizeof(nvm_kv_pool_tag_t));
            //nullify the pool tag for pool_id i
            memset(pool_tag_offset, 0, sizeof(nvm_kv_pool_tag_t));
            m_tags_map.erase((char *) &pool_tag);

            pool_hash = get_poolid_hash(i);
            it = m_delete_pools.find(pool_hash);
            if (it == m_delete_pools.end())
            {
                entry = new(std::nothrow) kv_delete_map_entry_t();
                if (entry == NULL)
                {
                    retval = -NVM_ERR_OUT_OF_MEMORY;
                    goto pool_manager_delete_pool_exit;
                }
                m_delete_pools[pool_hash] = entry;
            }
            else
            {
                entry = it->second;
            }
            bitmap_set(m_bitmaps.deleted, i);
            //This pool_id starts out having seen 0 scanner passes through
            //the drive
            (*entry)[i] = 0;
        }
        i++;
    } while ((i < m_pStoreMetadata->max_pools) && (pool_id == M_POOLID_ALL));

    //Persist the deleted bitmap
    retval = persist_store_info(M_KV_BM_DELETED, pool_id);

    if (retval < 0)
    {
        //The bitmap should be reverted back as the write failed
        if (pool_id == M_POOLID_ALL)
        {
            i = 1;
        }
        else
        {
            i = pool_id;
        }
        do
        {
            if (bitmap_test(m_bitmaps.in_use, i))
            {
               uint32_t pool_hash = get_poolid_hash(i);
               delete m_delete_pools[pool_hash];
               bitmap_clear(m_bitmaps.deleted, i);
            }
            i++;
        } while ((i < m_pStoreMetadata->max_pools) &&
                 (pool_id == M_POOLID_ALL));
        goto pool_manager_delete_pool_exit;
    }
    m_pPoolDelThread->restart_scanner_if_asleep();

pool_manager_delete_pool_exit:
    pthread_mutex_unlock(&m_bitmaps.mutex);
    m_pPoolDelThread->unlock_mutex();
    return retval;
}
//
//does cleanup of deleted entries after scanner pass
//
void NVM_KV_Pool_Mgr::update_del_map()
{
    int pool_id = 0;
    kv_delete_map_entry_t *entry = NULL;
    kv_delete_map_t::iterator hash_iter;
    kv_delete_map_entry_t::iterator id_iter;

    pthread_mutex_lock(&m_bitmaps.mutex);
    hash_iter = m_delete_pools.begin();
    while (hash_iter != m_delete_pools.end())
    {
        //We maintain invariant that entries in m_delete_pools are not NULL
        entry = hash_iter->second;
        id_iter = entry->begin();
        while (id_iter != entry->end())
        {
            pool_id = id_iter->first;
            //Increment count
            (id_iter->second)++;
            //Erase if >= 2 passes have occurred.
            if (id_iter->second >= 2)
            {
                //Using post increment ensures we erase current entry
                entry->erase(id_iter++);
                bitmap_clear(m_bitmaps.deleted, pool_id);
                bitmap_clear(m_bitmaps.in_use, pool_id);
                m_pStoreMetadata->total_no_pools--;
                persist_store_info((M_KVSTORE_METADATA | M_KV_BM_DELETED |
                                    M_KV_BM_INUSE), pool_id);
            }
            else
            {
                ++id_iter;
            }
        }
        if (entry->empty())
        {
            delete entry;
            //Again post increment ensures we erase current entry
            m_delete_pools.erase(hash_iter);
        }
        ++hash_iter;
    }
    pthread_mutex_unlock(&m_bitmaps.mutex);
}
//
//checks the status of the given pool_id or pool_hash depending on the boolean
//flag value
//
int NVM_KV_Pool_Mgr::check_pool_status(int pool_id, bool poolid_hashed)
{
    bool is_pool_in_del;
    bool is_valid_poolid;

    if (pool_id < 0 || pool_id >= NVM_KV_MAX_POOLS)
    {
        return POOL_IS_INVALID;
    }

    is_pool_in_del = pool_in_del(pool_id, poolid_hashed);
    is_valid_poolid = valid_poolid(pool_id);

    if (is_valid_poolid && !is_pool_in_del)
    {
        return POOL_IN_USE;
    }
    else if (is_pool_in_del)
    {
        return POOL_DELETION_IN_PROGRESS;
    }

    return POOL_NOT_IN_USE;
}
//
//check if the given pool id is valid
//
bool NVM_KV_Pool_Mgr::valid_poolid(int pool_id)
{
    bool retval = false;

    if (m_pStoreMetadata->max_pools == 0)
    {
        if (pool_id == M_DEFAULT_POOL_ID)
        {
            // There are no pools created and the id
            // is default
            retval = true;
        }
    }
    else
    {
        pthread_mutex_lock(&m_bitmaps.mutex);
        if (bitmap_test(m_bitmaps.in_use, pool_id))
        {
            retval = true;
        }
        pthread_mutex_unlock(&m_bitmaps.mutex);
    }

    return retval;
}
//
//checks if the pool id or pool hash is in the process of deletion
//
bool NVM_KV_Pool_Mgr::pool_in_del(int pool_val, bool poolid_hashed)
{
    bool retval = false;

    if (m_pStoreMetadata->max_pools != 0)
    {
        //if max pools is 0, then bitmaps wont be created
        pthread_mutex_lock(&m_bitmaps.mutex);
        if (!poolid_hashed)
        {
            if (bitmap_test(m_bitmaps.in_use, pool_val))
            {
                retval = bitmap_test(m_bitmaps.deleted, pool_val);
            }
        }
        else
        {
             retval = ((m_delete_pools.find(pool_val) ==
                m_delete_pools.end())? false : true);
        }
        pthread_mutex_unlock(&m_bitmaps.mutex);
    }

    return retval;
}
//
//returns true iff there are any pending deletes
//
bool NVM_KV_Pool_Mgr::pool_del_in_progress()
{
    bool retval = false;

    pthread_mutex_lock(&m_bitmaps.mutex);
    retval = !m_delete_pools.empty();
    pthread_mutex_unlock(&m_bitmaps.mutex);

    return retval;
}
//
//getter for the layout
//
NVM_KV_Layout *NVM_KV_Pool_Mgr::get_layout()
{
    return m_pLayout;
}
//
//get global mutex shared by pool deletion manager and expiry
//manager
//
pthread_mutex_t* NVM_KV_Pool_Mgr::get_glb_mutex()
{
    return &m_glb_mtx;
}
//
//get global condition variable associated with global mutex
//
pthread_cond_t* NVM_KV_Pool_Mgr::get_glb_cond_var()
{
    return &m_glb_cond;
}
//
//getter for the metadata
//
nvm_kv_store_metadata_t *NVM_KV_Pool_Mgr::get_metadata()
{
    return m_pStoreMetadata;
}
//
//get the sector size of the underlying device
//
uint32_t NVM_KV_Pool_Mgr::get_sector_size()
{
    return m_pKvDevice->capabilities.nvm_sector_size;
}
//
//if max_pools for KV store is less than 2^pool_bits returns
//pool id as is, else returns hashed pool id
//
uint32_t NVM_KV_Pool_Mgr::get_poolid_hash(int pool_id)
{
    int extra_pool_id = 0;
    bool align_hash = false;
    uint32_t pool_bits = m_pLayout->get_val_bits() - 1;

    if (m_pStoreMetadata->max_pools > (1 << pool_bits))
    {
        //calculate hash for the pool id
        return m_pHashFunc->key_hash((uint8_t *) &pool_id, sizeof(pool_id),
                                     extra_pool_id, pool_bits, align_hash);
    }
    else
    {
        return pool_id;
    }
}
//
//gets sector to which the pool tag belongs
//
int NVM_KV_Pool_Mgr::get_pool_tag_sect(int pool_id)
{
    int sect_num = 0;
    int sect_bits = m_pLayout->get_sect_bits();

    sect_num = pool_id * sizeof(nvm_kv_pool_tag_t);
    sect_num = sect_num >> sect_bits;

    return sect_num;

}
//
//get pool tag associated with pool id
//
int NVM_KV_Pool_Mgr::get_pool_tag(int pool_id, nvm_kv_pool_tag_t *pool_tags)
{
    char *pool_tag_offset = 0;

    pool_tag_offset = (char *) m_pPoolTags +
                      pool_id * sizeof(nvm_kv_pool_tag_t);
    if (check_pool_status(pool_id, false) == POOL_IN_USE)
    {
        memcpy(pool_tags, pool_tag_offset, sizeof(nvm_kv_pool_tag_t));
    }
    else
    {
        memset(pool_tags, 0, sizeof(nvm_kv_pool_tag_t));
    }
    return NVM_SUCCESS;
}
//
//get pool tag associated with pool id
//
int NVM_KV_Pool_Mgr::set_pool_tag(int pool_id, nvm_kv_pool_tag_t *pool_tags)
{
    char *pool_tag_offset = 0;

    pool_tag_offset = (char *) m_pPoolTags +
                      pool_id * sizeof(nvm_kv_pool_tag_t);
    if (pool_tags)
    {
        memcpy(pool_tag_offset, pool_tags, sizeof(nvm_kv_pool_tag_t));
    }
    else
    {
        memset(pool_tag_offset, 0, sizeof(nvm_kv_pool_tag_t));
    }
    return NVM_SUCCESS;
}
//
//writes atomically the metadata/in use/deleted bitmaps
//
int NVM_KV_Pool_Mgr::persist_store_info(uint32_t type, int pool_index)
{
    nvm_iovec_t *iovec = NULL;
    int ret_code = NVM_SUCCESS;
    int num_iovs = m_pKvDevice->capabilities.nvm_max_num_iovs;
    int iov_count = 0;
    uint32_t sector_size = get_sector_size();
    uint32_t sector_bits = m_pLayout->get_sect_bits();
    //if bitmap is more than one sector size base_bytes is used
    //to calculate base address
    int base_bytes = 0;

    iovec = new(std::nothrow) nvm_iovec_t[num_iovs];
    if (iovec == NULL)
    {
        fprintf(stderr, "Error, iovec allocation failed; \
            before restoring bitmaps\n");
        return -NVM_ERR_OUT_OF_MEMORY;
    }
    if (type & M_KV_POOL_TAGS)
    {

        if (pool_index == -1)
        {
            //Update the complete pool_tags
            //pool_tags are updated separately since num of IOVs
            //might not be enough for max_pools of 1M
            persist_pool_tags(iovec);
        }
        else
        {
            base_bytes = get_pool_tag_sect(pool_index) * sector_size;

            //Update specific pool_tag
            iovec[iov_count].iov_base = (uint64_t) m_pPoolTags +
                                        base_bytes;
            iovec[iov_count].iov_len = sector_size;
            iovec[iov_count].iov_lba = m_pLayout->get_pool_tags_lba() +
                get_pool_tag_sect(pool_index);
            iovec[iov_count].iov_opcode = NVM_IOV_WRITE;
            iov_count++;
        }
    }

    if (type & M_KVSTORE_METADATA)
    {
        //Update the metadata
        iovec[iov_count].iov_base = (uint64_t) m_pStoreMetadata;
        iovec[iov_count].iov_len = nvm_kv_round_upto_blk(
                                   sizeof(*m_pStoreMetadata),
                                   sector_size);
        iovec[iov_count].iov_lba = m_pLayout->get_metadata_lba();
        iovec[iov_count].iov_opcode = NVM_IOV_WRITE;
        iov_count++;
    }
    //basebytes for in-use and deleted bitmaps when pool_id is not -1
    if (pool_index != -1)
    {
        base_bytes = bit_index_sector(pool_index, sector_bits) * sector_size;
    }

    if (type & M_KV_BM_INUSE)
    {
        //Update the in-use bitmap
        if (pool_index == -1)
        {
            //Update the complete bitmap
            iovec[iov_count].iov_base = (uint64_t) m_bitmaps.in_use;
            iovec[iov_count].iov_len = nvm_kv_round_upto_blk(bits_per_uint8(
                                       round32(m_pStoreMetadata->max_pools)),
                                       sector_size);
            iovec[iov_count].iov_lba = m_pLayout->get_inuse_bm_lba();
        }
        else
        {

            //Update the specific pool index
            iovec[iov_count].iov_base = (uint64_t) m_bitmaps.in_use + \
                                         base_bytes;
            iovec[iov_count].iov_len = sector_size;
            iovec[iov_count].iov_lba = m_pLayout->get_inuse_bm_lba() + \
                                      bit_index_sector(pool_index, sector_bits);
        }
        iovec[iov_count].iov_opcode = NVM_IOV_WRITE;
        iov_count++;
    }
    if (type & M_KV_BM_DELETED)
    {
        //Update the deleted bitmap
        if (pool_index == -1)
        {
            //Update the complete bitmap
            iovec[iov_count].iov_base = (uint64_t) m_bitmaps.deleted;
            iovec[iov_count].iov_len = nvm_kv_round_upto_blk(bits_per_uint8(
                                       round32(m_pStoreMetadata->max_pools)),
                                       sector_size);
            iovec[iov_count].iov_lba = m_pLayout->get_del_bm_lba();
        }
        else
        {
            //Update the specific index in the bitmap
            iovec[iov_count].iov_base = (uint64_t) m_bitmaps.deleted +
                                        base_bytes;
            iovec[iov_count].iov_len = sector_size;
            iovec[iov_count].iov_lba = m_pLayout->get_del_bm_lba() +
                                       bit_index_sector(pool_index,
                                                        sector_bits);
        }
        iovec[iov_count].iov_opcode = NVM_IOV_WRITE;
        iov_count++;
    }

    ret_code = nvm_writev(m_pKvDevice, iovec, iov_count, true);
    delete iovec;
    if (ret_code < 0)
    {
        fprintf(stderr, "Error, atomic write failed: %d\n", errno);
        return ret_code;
    }
    return NVM_SUCCESS;
}
//
//fills IOVs with pool tag data to write pool tags on the media
//
int NVM_KV_Pool_Mgr::persist_pool_tags(nvm_iovec_t *iov)
{
    int ret_code = 0;
    int max_write_size_per_iov = 0;
    uint32_t remaining_len = 0;
    int sector_size = get_sector_size();
    int count = 0;
    int iov_count = 0;
    int num_iovs = m_pKvDevice->capabilities.nvm_max_num_iovs;

    remaining_len = m_poolTagSize;

    max_write_size_per_iov =
        m_pKvDevice->capabilities.nvm_atomic_write_multiplicity *
        m_pKvDevice->capabilities.nvm_max_write_size_per_iov;
    while (remaining_len)
    {
        iov_count = 0;

        while (remaining_len && iov_count < num_iovs)
        {
            int vector_size = (remaining_len < max_write_size_per_iov) ?
                remaining_len : max_write_size_per_iov;
            iov[iov_count].iov_base = (uint64_t) m_pPoolTags + (count *
                    max_write_size_per_iov);
            //vector_size in number of packets
            iov[iov_count].iov_len = vector_size;
            iov[iov_count].iov_lba = m_pLayout->get_pool_tags_lba() + (count
                * (max_write_size_per_iov / sector_size));
            iov[iov_count].iov_opcode = NVM_IOV_WRITE;
            iov_count++;
            count++;
            remaining_len -= vector_size;
        }
        ret_code = nvm_writev(m_pKvDevice, iov, iov_count, true);
        if (ret_code < 0)
        {
            fprintf(stderr, "Error, atomic write failed: %d\n", errno);
            return ret_code;
        }

    }

    return NVM_SUCCESS;
}
//
//returns the constant for default pool id
//
int NVM_KV_Pool_Mgr::get_default_poolid()
{
    return M_DEFAULT_POOL_ID;
}
//
//returns the constant for all pool id
//
int NVM_KV_Pool_Mgr::get_all_poolid()
{
    return M_POOLID_ALL;
}
//
//checks if pool deletion thread is running
//
bool NVM_KV_Pool_Mgr::check_pool_del_status()
{
    return m_pool_del_status;
}
//
//sets the status of the pool deletion thread
//
void NVM_KV_Pool_Mgr::set_pool_del_status(bool mode)
{
    m_pool_del_status = mode;
}
