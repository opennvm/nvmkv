//----------------------------------------------------------------------------
// NVMKV
// |- Copyright 2012-2014 Fusion-io, Inc.

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
#include <string>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
using namespace std;

#include "src/kv_store_mgr.h"
#include "src/kv_wrappers.h"
#include "src/kv_scanner.h"
#include "src/kv_pool_del_manager.h"
#include "src/kv_async_expiry.h"
#include "src/kv_expiry_manager.h"
#include "src/kv_iterator.h"
#include <nvm_primitives.h>
using namespace nvm_wrapper;

//static member definition
NVM_KV_Store_Mgr* NVM_KV_Store_Mgr::m_pInstance = NULL;
pthread_mutex_t NVM_KV_Store_Mgr::m_mtx = PTHREAD_MUTEX_INITIALIZER;
//
//NVM_KV_Store_Mgr class definitions
//

//
//creates mutex
//
NVM_KV_Store_Mgr::NVM_KV_Store_Mgr()
{
    pthread_mutex_init(&m_mtx_open, NULL);
}
//
//destroys mutex
//clears m_kvMap and deletes instance of this class
//
NVM_KV_Store_Mgr::~NVM_KV_Store_Mgr()
{
    map<int, NVM_KV_Store*>::iterator itr = m_kvMap.begin();

    pthread_mutex_destroy(&m_mtx_open);

    for (; itr != m_kvMap.end(); itr++)
    {
        delete itr->second;
    }
    //erases all the entries in the map
    m_kvMap.clear();
}
//
//creates instance of the class
//
NVM_KV_Store_Mgr* NVM_KV_Store_Mgr::instance(bool sync)
{
    int32_t ret_code = NVM_SUCCESS;

    if (sync)
    {
        pthread_mutex_lock(&NVM_KV_Store_Mgr::m_mtx);
    }
    if (m_pInstance == NULL)
    {
        //exception will be thrown if it fails to create kvstore manager
        m_pInstance = new NVM_KV_Store_Mgr();
        if ((ret_code = m_pInstance->initialize()) != NVM_SUCCESS)
        {
            fprintf(stderr, "Error, failed to initialize kvstore manager \n");
            throw ret_code;
        }
    }
    if (sync)
    {
        pthread_mutex_unlock(&NVM_KV_Store_Mgr::m_mtx);
    }
    return m_pInstance;
}
//
//creates instance of the class
//
NVM_KV_Store_Mgr* NVM_KV_Store_Mgr::instance()
{
    return instance(false);
}
///
///kvstore manager initialization
///
int NVM_KV_Store_Mgr::initialize()
{
    return m_buffer_pool.initialize(NVM_KV_Buffer_Pool::M_MAX_BUFFERS,
                 NVM_KV_Buffer_Pool::M_BUFFER_LIMIT,
                 NVM_KV_Buffer_Pool::M_BUFFER_PAGE_ALIGN);
}
///
///initializes KV store and writes KV store metadata if necessary
///
int NVM_KV_Store_Mgr::kv_open(int id, uint32_t version, uint32_t max_pools,
                              uint32_t expiry, uint64_t cache_size)
{
    int32_t ret_code = NVM_SUCCESS;
    NVM_KV_Store *kv_store = NULL;
    struct stat stat;
    uint32_t sparse_addr_bits = 0;
    uint64_t device_size = 0;
    nvm_version_t  nvm_version;
    nvm_handle_t handle = -1;
    nvm_capacity_t capacity_info;
    nvm_kv_store_capabilities_t kv_cap;
    nvm_kv_store_metadata_t *metadata = NULL;
    bool acquired_lock = false;
    pair<map<int, NVM_KV_Store*>::iterator, bool> map_ret_code;
    bool nvm_release_handle_attempted = false;

    if (fstat(id, &stat) == -1)
    {
        fprintf(stderr, "Error, fstat failed \n");
        ret_code = -NVM_ERR_INVALID_HANDLE;
        goto end_kv_open;
    }

    nvm_version.major = NVM_PRIMITIVES_API_MAJOR;
    nvm_version.minor = NVM_PRIMITIVES_API_MINOR;
    nvm_version.micro = NVM_PRIMITIVES_API_MICRO;
    handle = nvm_get_handle(id, &nvm_version);
    if (handle == -1)
    {
        fprintf(stderr, "Error: nvm_get_handle failed. (%s)\n",
                strerror(errno));
        ret_code = -NVM_ERR_IO;
        goto end_kv_open;
    }

    if ((ret_code =
        NVM_KV_Store::initialize_capabilities(handle, &kv_cap))
        != NVM_SUCCESS)
    {
        goto end_kv_open;
    }

    if (nvm_get_capacity(handle, &capacity_info) < 0)
    {
        ret_code = -NVM_ERR_INTERNAL_FAILURE;
        goto end_kv_open;
    }
    else
    {
        device_size = capacity_info.total_logical_capacity *
                              kv_cap.nvm_sector_size;
    }

    // set the total number of sparse address bits
    sparse_addr_bits = comp_sparse_addr_bits(device_size,
                                             kv_cap.nvm_sector_size);

    kv_store = new(std::nothrow) NVM_KV_Store();
    if (!kv_store)
    {
        fprintf(stderr, "Error, allocating for kvstore\n");
        ret_code = -NVM_ERR_OUT_OF_MEMORY;
        goto end_kv_open;
    }

    pthread_mutex_lock(&m_mtx_open);
    acquired_lock = true;
    ret_code = kv_store->initialize(id, handle, kv_cap, sparse_addr_bits,
                                    max_pools, version, expiry, cache_size);
    if (ret_code < 0)
    {
        fprintf(stderr, "Error, KV store initialization failed\n");
        goto end_kv_open;
    }

    //if kvstore object already exists in memory, delete newly created object
    metadata = kv_store->get_store_metadata();

    map_ret_code =
        m_kvMap.insert(pair<int, NVM_KV_Store*>((int) metadata->kv_store_id,
                       kv_store));

    if (map_ret_code.second == false)
    {
        if (nvm_release_handle(handle) == -1)
        {
            ret_code = -NVM_ERR_INTERNAL_FAILURE;
            fprintf(stderr,
                    "Error: nvm_release_handle failed for handle: %d. (%s)\n",
                    handle, strerror(errno));
        }
        else {
            ret_code = metadata->kv_store_id;
        }
        nvm_release_handle_attempted = true;
        delete kv_store;
        kv_store = NULL;
    }
    else
    {
        ret_code = metadata->kv_store_id;
    }

end_kv_open:
    if (acquired_lock)
    {
        pthread_mutex_unlock(&m_mtx_open);
    }
    if (ret_code < 0)
    {
        errno = ret_code;
        if (handle != -1 && nvm_release_handle_attempted == false)
        {
            if (nvm_release_handle(handle) == -1)
            {
                fprintf(stderr,
                        "Error: nvm_release_handle failed for handle: %d. (%s)\n",
                        handle, strerror(errno));
            }
        }
        ret_code = -1;

        if (kv_store)
        {
            delete kv_store;
        }
    }
    return ret_code;
}
//
//creates a new pool
//
int NVM_KV_Store_Mgr::kv_pool_create(int kv_id, nvm_kv_pool_tag_t *pool_tag)
{
    int pool_id = -1;
    int ret_code = NVM_SUCCESS;
    NVM_KV_Store *kv_store = NULL;

    if (kv_internal_validation(KV_VALIDATE_ID, kv_id, &kv_store, 0,
                               KVPOOLCREATE, NULL, 0, NULL,
                               0, NULL, 0) != NVM_SUCCESS)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto end_kv_pool_create;
    }

    pool_id = m_kvMap[kv_id]->get_pool_mgr()->create_pool(pool_tag);
    if (pool_id < 0)
    {
        fprintf(stderr, "Error, creating a new pool\n");
        ret_code = pool_id;
        goto end_kv_pool_create;
    }

end_kv_pool_create:
    if (ret_code < 0)
    {
        errno = ret_code;
        pool_id = -1;
    }
    return pool_id;
}
//
//deletes an existing pool
//
int NVM_KV_Store_Mgr::kv_pool_delete(int kv_id, int pool_id)
{
    int ret_code = NVM_SUCCESS;
    NVM_KV_Store *kv_store = NULL;
    int val_flag = KV_VALIDATE_ID | KV_VALIDATE_POOL_ID;

    if (kv_internal_validation(val_flag, kv_id, &kv_store, pool_id,
                               KVPOOLDELETE, NULL, 0, NULL,
                               0, NULL, 0) != NVM_SUCCESS)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto end_kv_pool_delete;
    }

    ret_code = kv_store->get_pool_mgr()->delete_pool(pool_id);

end_kv_pool_delete:
    if (ret_code < 0)
    {
        errno = ret_code;
        ret_code = -1;
    }
    return ret_code;
}
//
//puts key value pair in KV store
//
int NVM_KV_Store_Mgr::kv_put(int id, int pool_id, nvm_kv_key_t *key,
                             uint32_t key_len, void *value, uint32_t value_len,
                             uint32_t expiry, bool replace, uint32_t gen_count)
{
    char *buf = NULL;
    uint32_t buf_len = 0;
    uint64_t key_hash_val = 0;
    uint32_t num_iovs = 0;
    int ret_code = NVM_SUCCESS;
    bool insert_lba = false;
    nvm_kv_store_device_t *kv_device = NULL;
    NVM_KV_Store *kv_store = NULL;
    int val_flag = (KV_VALIDATE_ID | KV_VALIDATE_KEY | KV_VALIDATE_VALUE |
        KV_VALIDATE_POOL_ID);
    bool user_buffer_usage = false;
    bool exists_in_cache = false;
    bool wait = false;
    nvm_iovec_t *iovec = NULL;
    uint32_t value_offset = 0; //value offset on media
    uint32_t trim_len = 0;  //value length replaced, if key is been replaced
    uint32_t iovec_index = 0;
    uint32_t abs_expiry = 0;
    nvm_kv_store_metadata_t *metadata = NULL;
    NVM_KV_Cache *cache = NULL;
    nvm_kv_cache_context cache_context;

    if (kv_internal_validation(val_flag, id, &kv_store, pool_id, KVPUT,
                               key, key_len, value, value_len, NULL, 0)
                               != NVM_SUCCESS)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto end_kv_put;
    }
    kv_device = kv_store->get_store_device();
    metadata = kv_store->get_store_metadata();
    cache = kv_store->get_cache();

    if (cache)
    {
        //Check if entry is available in the cache
        if ((ret_code =
            cache->kv_cache_get(key, key_len, pool_id, &cache_context))
            == NVM_SUCCESS)
        {
            //If the entry does not have to be replaced and it is
            //not expired
            if (!replace && !cache_context.expired)
            {
                ret_code = -NVM_ERR_OBJECT_EXISTS;
                goto end_kv_put;
            }

            if (cache_context.context_state == CACHE_ENTRY_FOUND)
            {
               if (kv_store->insert_lba_to_safe_list(cache_context.context_entry.lba, &wait))
               {
                   if (wait)
                   {
                       kv_store->delete_lba_from_safe_list(cache_context.context_entry.lba);
                       memset(&cache_context, 0, sizeof(cache_context));
                   }
                   else
                   {
                       insert_lba = true;
                       exists_in_cache = true;
                   }
               }
               else
               {
                   ret_code = -NVM_ERR_INTERNAL_FAILURE;
                   goto end_kv_put;
               }
            }
        }
    }

    ret_code = kv_alloc_for_write(kv_device, key_len, value, value_len, &buf,
                                  &buf_len, &num_iovs, &user_buffer_usage,
                                  &value_offset);
    if (ret_code != NVM_SUCCESS)
    {
        goto end_kv_put;
    }

    if (!exists_in_cache)
    {
        ret_code = kv_gen_lba(kv_store, key, key_len, pool_id, value_len,
                              buf, replace, &trim_len, key_hash_val,
                              insert_lba, NULL);

        if (ret_code != NVM_SUCCESS)
        {
            goto end_kv_put;
        }
    }
    else
    {
        if (cache_context.context_entry.value_len > value_len)
        {
            trim_len = cache_context.context_entry.value_len;
        }

        key_hash_val = cache_context.context_entry.lba;
    }

    iovec = new(std::nothrow)
        nvm_iovec_t[kv_device->capabilities.nvm_max_num_iovs];
    if (iovec == NULL)
    {
        ret_code = -NVM_ERR_OUT_OF_MEMORY;
        goto end_kv_put;
    }

    ret_code = kv_process_for_trim(value_len, trim_len, iovec, &iovec_index,
                                   key_hash_val, kv_device);
    if (ret_code != NVM_SUCCESS)
    {
        goto end_kv_put;
    }

    if (kv_store->get_expiry() == KV_GLOBAL_EXPIRY)
    {
        expiry = metadata->global_expiry;
    }

    ret_code = kv_process_for_write(pool_id, key, key_len, value, value_len,
                                    buf, kv_device, (iovec + iovec_index),
                                    user_buffer_usage, expiry,
                                    gen_count, key_hash_val, value_offset,
                                    &abs_expiry);
    if (ret_code != NVM_SUCCESS)
    {
        goto end_kv_put;
    }

    ret_code = nvm_writev(kv_device, iovec, num_iovs, true);
    if (ret_code < 0)
    {
        fprintf(stderr, "Error, atomic write failed in kv_put, %d\n",
                errno);
        goto end_kv_put;
    }

    if (cache)
    {
        cache->kv_cache_acquire_wrlock();

        //add or replace this entry in the cache.
        ret_code =
            cache->kv_cache_put(key, key_len, pool_id, value_len,
                                abs_expiry, gen_count, key_hash_val,
                                &cache_context);

        cache->kv_cache_release_lock();

        if (ret_code < 0)
        {
            fprintf(stderr, "Error, updating the cache failed in kv_put\n");
        }
    }

end_kv_put:
    if (ret_code < 0)
    {
        errno = ret_code;
        ret_code = -1;
    }
    else
    {
        ret_code = value_len;
    }

    delete[] iovec;

    if (buf_len)
    {
        m_buffer_pool.release_buf(buf, buf_len);
    }
    //once done writing delete the lba if inserted and releases the lock
    if (insert_lba)
    {
        kv_store->delete_lba_from_safe_list(key_hash_val);
    }
    return ret_code;
}
//
//gets key value pair in KV store
//
int NVM_KV_Store_Mgr::kv_get(int id, int pool_id, nvm_kv_key_t *key,
                             uint32_t key_len, void *value, uint32_t value_len,
                             bool read_exact, nvm_kv_key_info_t *key_info)
{
    char *buf = NULL;
    nvm_kv_header_t *hdr = NULL;
    uint32_t ret_buf_size = 0;
    uint32_t sector_size = 0;
    NVM_KV_Store *kv_store = NULL;
    int ret_code = NVM_SUCCESS;
    ssize_t ret_read_len;
    uint64_t key_hash_val = 0;
    bool buf_acquired = false;

    int val_flag = (KV_VALIDATE_ID | KV_VALIDATE_KEY | KV_VALIDATE_VALUE |
        KV_VALIDATE_POOL_ID);

    if (kv_internal_validation(val_flag, id, &kv_store, pool_id, KVGET,
        key, key_len, value, value_len, NULL, 0) != NVM_SUCCESS)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto end_kv_get;
    }
    if (!key_info)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto end_kv_get;
    }

    sector_size = kv_store->get_sector_size();

    buf = m_buffer_pool.get_buf(sector_size, ret_buf_size);
    if ((ret_buf_size < sector_size) || (buf == NULL))
    {
        ret_code = -NVM_ERR_OUT_OF_MEMORY;
        goto end_kv_get;
    }
    buf_acquired = true;

    ret_code = kv_read_media(&hdr, pool_id, kv_store, key, key_len, value,
                             value_len, &key_hash_val, buf, read_exact);
    if (ret_code < 0)
    {
        goto end_kv_get;
    }

    //if the key is expired, return error to the user
    ret_code = kv_expire(key, kv_store, hdr, &key_hash_val, true);
    if (ret_code < 0)
    {
        goto end_kv_get;
    }

    //if value_offset is greater than or equal to sector_size
    //value is already read into user buffer,
    //else copy value from buffer pool buffer to user buffer.
    if (hdr->value_offset < sector_size)
    {
        memcpy(value, buf + hdr->value_offset, hdr->value_len);
    }
    key_info->gen_count = hdr->metadata.gen_count;
    key_info->expiry = hdr->metadata.expiry;
    key_info->key_len = hdr->key_len;
    key_info->value_len = hdr->value_len;

    if (hdr->value_len > value_len)
    {
        ret_read_len = value_len;
    }
    else
    {
        ret_read_len = hdr->value_len;
    }

end_kv_get:
    if (ret_code < 0)
    {
        errno = ret_code;
        ret_code = -1;
    }
    else
    {
        ret_code = ret_read_len;
    }
    if (buf_acquired)
    {
        m_buffer_pool.release_buf(buf, ret_buf_size);
    }

    return ret_code;
}
//
//deletes key value pair in KV store
//
int NVM_KV_Store_Mgr::kv_delete(int id, int pool_id, nvm_kv_key_t *key,
                                uint32_t key_len)
{
    int ret_code = NVM_SUCCESS;
    NVM_KV_Store *kv_store = NULL;
    int val_flag = (KV_VALIDATE_ID | KV_VALIDATE_KEY | KV_VALIDATE_POOL_ID);

    if (kv_internal_validation(val_flag, id, &kv_store, pool_id, KVDELETE,
                               key, key_len, NULL, 0, NULL, 0) != NVM_SUCCESS)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto kv_delete_exit;
    }

    ret_code = kv_delete_sync(pool_id, key, key_len, kv_store);

kv_delete_exit:
    if (ret_code < 0)
    {
        errno = ret_code;
        ret_code = -1;
    }

    return ret_code;
}
//
//checks if key value pair already exists on the KV store
//
int NVM_KV_Store_Mgr::kv_exists(int id, int pool_id, nvm_kv_key_t *key,
                                uint32_t key_len, nvm_kv_key_info_t *key_info)
{
    nvm_kv_header_t *hdr = NULL;
    int ret_code = NVM_SUCCESS;
    NVM_KV_Store *kv_store = NULL;
    int val_flag = (KV_VALIDATE_ID | KV_VALIDATE_KEY | KV_VALIDATE_POOL_ID);

    if (kv_internal_validation(val_flag, id, &kv_store, pool_id, KVEXISTS,
                               key, key_len, NULL, 0, NULL, 0) != NVM_SUCCESS)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto end_kv_exists;
    }

    if (!key_info)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto end_kv_exists;
    }

    ret_code = kv_range_exist_wrapper(kv_store, pool_id, key, key_len,
                                      KVEXISTS, NULL, &hdr);

end_kv_exists:
    if (ret_code < 0)
    {
        if (ret_code == -NVM_ERR_OBJECT_NOT_FOUND)
        {
            ret_code = 0;
        }
        else
        {
            errno = ret_code;
            ret_code = -1;
        }
    }
    else
    {
        ret_code = 1;
        key_info->pool_id = pool_id;
        key_info->key_len = hdr->key_len;
        key_info->value_len = hdr->value_len;
        key_info->expiry = hdr->metadata.expiry;
        key_info->gen_count = hdr->metadata.gen_count;
    }

    return ret_code;
}
//
//gets all KV pairs in one batch operation
//
int NVM_KV_Store_Mgr::kv_batch_get(int id, int pool_id, nvm_kv_iovec_t *kv_iov,
                                   uint32_t iov_count)
{
    int ret_code = 0;
    int val_flag = 0;
    NVM_KV_Store *kv_store = NULL;
    nvm_kv_key_info_t key_info;

    val_flag = (KV_VALIDATE_ID | KV_VALIDATE_BATCH | KV_VALIDATE_POOL_ID);
    if (kv_internal_validation(val_flag, id, &kv_store, pool_id, KVBATCHGET,
                        NULL, 0, NULL, 0, kv_iov, iov_count) != NVM_SUCCESS)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto end_batch_get;
    }

    //Loop through all the IOVs and get the desired values
    for (int i = 0; i < iov_count; i++)
    {
        ret_code = kv_get(id, pool_id, kv_iov[i].key, kv_iov[i].key_len,
                          kv_iov[i].value, kv_iov[i].value_len, true,
                          &key_info);

        if (ret_code < 0)
        {
            goto end_batch_get;
        }
    }

end_batch_get:

    if (ret_code < 0 && ret_code != -1)
    {
        errno = ret_code;
        ret_code = -1;
    }

    return ret_code;
}
//
//puts all KV pair in one batch operation
//
int NVM_KV_Store_Mgr::kv_batch_put(int id, int pool_id, nvm_kv_iovec_t *kv_iov,
                                   uint32_t iov_count)
{
    int ret_code = NVM_SUCCESS;
    int buf_acquired = 0; //number of buffers acquired
    nvm_kv_store_device_t *kv_device = NULL;
    NVM_KV_Store *kv_store = NULL;
    int val_flag = 0; //parameter validation
    bool insert_lba = false;
    bool wait = false;
    map<uint64_t, nvm_kv_iovec_t*> lba_list; //stores lba that were validated
    uint32_t final_iov_count = 0;
    char *buf = NULL; //buffer acquired from the pool
    nvm_iovec_t *iovec_wrt = NULL;
    nvm_iovec_t *iovec_trm = NULL;
    uint32_t *ret_buf_sizes = NULL;
    char **arr_bufs = NULL;
    uint32_t idx_wrt = 0;
    uint32_t idx_trm = 0;
    nvm_kv_store_metadata_t *metadata = NULL;
    NVM_KV_Cache *cache = NULL;
    uint32_t max_batch_size = 0;
    nvm_kv_cache_context cache_context[iov_count];
    uint64_t key_hash_val[iov_count];
    uint32_t abs_expiry[iov_count];
    uint32_t gen_count[iov_count];


    val_flag = (KV_VALIDATE_ID | KV_VALIDATE_BATCH | KV_VALIDATE_POOL_ID);
    if (kv_internal_validation(val_flag, id, &kv_store, pool_id, KVBATCHPUT,
                               NULL, 0, NULL, 0, kv_iov, iov_count) != NVM_SUCCESS)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto end_kv_batch_put;
    }
    max_batch_size = kv_store->get_max_batch_size();

    //check if number of vectors exceeds max batch size
    if (iov_count > max_batch_size)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto end_kv_batch_put;
    }
    kv_device = kv_store->get_store_device();
    metadata = kv_store->get_store_metadata();
    cache = kv_store->get_cache();

    ret_buf_sizes = new(std::nothrow)
        uint32_t[kv_device->capabilities.nvm_max_num_iovs];
    if (ret_buf_sizes == NULL)
    {
        ret_code = -NVM_ERR_OUT_OF_MEMORY;
        goto end_kv_batch_put;
    }

    arr_bufs = new(std::nothrow)
        char*[kv_device->capabilities.nvm_max_num_iovs];
    if (arr_bufs == NULL)
    {
        ret_code = -NVM_ERR_OUT_OF_MEMORY;
        goto end_kv_batch_put;
    }

    iovec_trm = new(std::nothrow)
        nvm_iovec_t[kv_device->capabilities.nvm_max_num_iovs];
    if (iovec_trm == NULL)
    {
        ret_code = -NVM_ERR_OUT_OF_MEMORY;
        goto end_kv_batch_put;
    }

    iovec_wrt = new(std::nothrow)
        nvm_iovec_t[kv_device->capabilities.nvm_max_num_iovs];
    if (iovec_wrt == NULL)
    {
        ret_code = -NVM_ERR_OUT_OF_MEMORY;
        goto end_kv_batch_put;
    }

    val_flag = (KV_VALIDATE_KEY | KV_VALIDATE_VALUE);

    memset(cache_context, 0, sizeof(nvm_kv_cache_context) * iov_count);
    memset(key_hash_val, 0, sizeof(uint64_t) * iov_count);
    memset(abs_expiry, 0, sizeof(uint32_t) * iov_count);
    memset(gen_count, 0, sizeof(uint32_t) * iov_count);

    //first loop through the incoming IOVs to validate and get the buffers
    for (int i = 0; i < iov_count; i++)
    {
        uint32_t ret_buf_size = 0;
        uint32_t trim_len = 0;
        bool user_buffer_usage = false;
        bool exists_in_cache = false;
        uint32_t num_iovs = 0; //number of IOVs needed
        uint32_t value_offset = 0; //value offset on media

        if (kv_internal_validation(val_flag, id, &kv_store, pool_id,
            KVBATCHPUT, kv_iov[i].key, kv_iov[i].key_len, kv_iov[i].value,
            kv_iov[i].value_len, NULL, 0) != NVM_SUCCESS)
        {
            ret_code = -NVM_ERR_INVALID_INPUT;
            goto end_kv_batch_put;
        }

        if (cache)
        {
            if ((ret_code =
                 cache->kv_cache_get(kv_iov[i].key, kv_iov[i].key_len,
                                     pool_id, &cache_context[i]))
                 == NVM_SUCCESS)
            {
                if (!kv_iov[i].replace && !cache_context[i].expired)
                {
                    ret_code = -NVM_ERR_OBJECT_EXISTS;
                    goto end_kv_batch_put;
                }

                if (cache_context[i].context_state == CACHE_ENTRY_FOUND)
                {
                    uint64_t lba = cache_context[i].context_entry.lba;
                    map<uint64_t, nvm_kv_iovec_t *>::iterator itr = lba_list.find(lba);

                    //if lba is not present in the lba list
                    if (itr == lba_list.end())
                    {
                        if (kv_store->insert_lba_to_safe_list(lba, &wait))
                        {
                            if (wait)
                            {
                                kv_store->delete_lba_from_safe_list(lba);
                                memset(&cache_context[i], 0,
                                       sizeof(cache_context[i]));
                            }
                            else
                            {
                                insert_lba = true;
                                exists_in_cache = true;
                            }
                        }
                        else
                        {
                            ret_code = -NVM_ERR_INTERNAL_FAILURE;
                            goto end_kv_batch_put;
                        }
                    }
                    else
                    {
                        nvm_kv_iovec_t *dup_keys = itr->second;

                        if (dup_keys->key_len == kv_iov[i].key_len &&
                            (memcmp(dup_keys->key, kv_iov[i].key, kv_iov[i].key_len) == 0))
                        {
                            exists_in_cache = true;
                        }
                    }
                }
            }
        }

        ret_code = kv_alloc_for_write(kv_device, kv_iov[i].key_len,
                                      kv_iov[i].value, kv_iov[i].value_len,
                                      &buf, &ret_buf_size, &num_iovs,
                                      &user_buffer_usage,
                                      &value_offset);
        if (ret_code != NVM_SUCCESS)
        {
            goto end_kv_batch_put;
        }

        final_iov_count += num_iovs;
        if (final_iov_count > kv_device->capabilities.nvm_max_num_iovs)
        {
            fprintf(stderr, "Error, number of io vectors exceeded \
                    maximum limit in batch put operation\n");
            ret_code = -NVM_ERR_IO;
            goto end_kv_batch_put;
        }
        arr_bufs[i] = buf;
        ret_buf_sizes[i] = ret_buf_size;
        buf_acquired++;

        if (!exists_in_cache)
        {
            ret_code = kv_gen_lba(kv_store, kv_iov[i].key,
                                  kv_iov[i].key_len, pool_id,
                                  kv_iov[i].value_len, buf, kv_iov[i].replace,
                                  &trim_len, key_hash_val[i],
                                  insert_lba, &lba_list);
             if (ret_code != NVM_SUCCESS)
             {
                 goto end_kv_batch_put;
             }
        }
        else
        {
            if (cache_context[i].context_entry.value_len > kv_iov[i].value_len)
            {
                trim_len = cache_context[i].context_entry.value_len;
            }

            key_hash_val[i] = cache_context[i].context_entry.lba;
        }

        lba_list.insert(pair<uint64_t, nvm_kv_iovec_t*>(key_hash_val[i],
                                                        &kv_iov[i]));
        ret_code = kv_process_for_trim(kv_iov[i].value_len, trim_len,
                                       iovec_trm, &idx_trm,
                                       key_hash_val[i], kv_device);
        if (ret_code != NVM_SUCCESS)
        {
            goto end_kv_batch_put;
        }
        //number of trims should not exceed max_batch_size since the number
        //of iov_count will always be <= max_batch_size
        if (idx_trm > max_batch_size)
        {
            ret_code = -NVM_ERR_IO;
            goto end_kv_batch_put;
        }
        if (kv_store->get_expiry() == KV_GLOBAL_EXPIRY)
        {
            kv_iov[i].expiry = metadata->global_expiry;
        }
        ret_code = kv_process_for_write(pool_id, kv_iov[i].key,
                                        kv_iov[i].key_len, kv_iov[i].value,
                                        kv_iov[i].value_len, buf, kv_device,
                                        &iovec_wrt[idx_wrt], user_buffer_usage,
                                        kv_iov[i].expiry, kv_iov[i].gen_count,
                                        key_hash_val[i], value_offset, &abs_expiry[i]);
        if (ret_code != NVM_SUCCESS)
        {
            goto end_kv_batch_put;
        }
        idx_wrt += num_iovs;

        gen_count[i] = kv_iov[i].gen_count;
    }
    //if there are any trim IOVs then club trim IOVs and write IOVs together
    //if idx_trim + idx_write is less than or equal to
    //kv_device->capabilities.nvm_max_num_iovs.
    //if idx_trim exists and idx_trim + idx_write is greater than
    //kv_device->capabilities.nvm_max_num_iovs, send trim IOVs as one batch
    //operation first, followed by write IOVs as another batch operations.
    if (idx_trm)
    {
        uint32_t count = 0;

        if (idx_wrt + idx_trm <= kv_device->capabilities.nvm_max_num_iovs)
        {
            while ((count < idx_wrt) &&
                   (idx_trm < kv_device->capabilities.nvm_max_num_iovs))
            {
                iovec_trm[idx_trm++] = iovec_wrt[count++];
            }
        }
        ret_code = nvm_writev(kv_device, iovec_trm, idx_trm, true);
        if (ret_code < 0)
        {
            goto end_kv_batch_put;
        }
    }

    if (!idx_trm ||
        (idx_wrt + idx_trm > kv_device->capabilities.nvm_max_num_iovs))
    {
        ret_code = nvm_writev(kv_device, iovec_wrt, idx_wrt, true);
        if (ret_code < 0)
        {
            fprintf(stderr, "Error, atomic write failed, %d\n",
                    errno);
            goto end_kv_batch_put;
        }
    }

    if (cache)
    {
        cache->kv_cache_acquire_wrlock();

        for (int i = 0 ; i < iov_count; i++)
        {
            ret_code = cache->kv_cache_put(kv_iov[i].key,
                                           kv_iov[i].key_len,
                                           pool_id, kv_iov[i].value_len,
                                           abs_expiry[i], gen_count[i],
                                           key_hash_val[i],
                                           &cache_context[i]);

            if (ret_code < 0)
            {
                fprintf(stderr, "Error, updating the cache failed in "
                                "kv_batch_put\n");
                break;
            }
        }

        cache->kv_cache_release_lock();
    }

end_kv_batch_put:
    if (ret_code < 0)
    {
        errno = ret_code;
        ret_code = -1;
    }
    else
    {
        ret_code = NVM_SUCCESS;
    }
    for (int k = 0; k < buf_acquired; k++)
    {
        m_buffer_pool.release_buf(arr_bufs[k],
                                                    ret_buf_sizes[k]);
    }
    //once done writing delete the lba if inserted and releases the lock
    if (insert_lba && !lba_list.empty())
    {
        map<uint64_t, nvm_kv_iovec_t*>::iterator itr = lba_list.begin();
        for (;itr != lba_list.end(); itr++)
        {
            uint64_t del_lba = itr->first;
            kv_store->delete_lba_from_safe_list(del_lba);
        }
    }

    delete[] iovec_wrt;
    delete[] iovec_trm;
    delete[] ret_buf_sizes;
    delete[] arr_bufs;

    return ret_code;
}
//
//initializes an iterator and returns its iterator id. Fails if
//no more iterators are available or if no object is found.
//
int NVM_KV_Store_Mgr::kv_begin(int id, int pool_id)
{
    int it_id = -1;
    int ret_code = NVM_SUCCESS;
    NVM_KV_Store *kv_store = NULL;
    int val_flag = KV_VALIDATE_ID | KV_VALIDATE_POOL_ID;
    uint64_t usr_data_lba = 0;
    uint64_t MAX_VALID_SECTOR = 0;
    uint32_t pool_hash = 0;

    if (kv_internal_validation(val_flag, id, &kv_store, pool_id,
        KVBEGIN, NULL, 0, NULL, 0, NULL, 0) != NVM_SUCCESS)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto kv_begin_exit;
    }

    pool_hash = kv_store->get_pool_mgr()->get_poolid_hash(pool_id);
    it_id = kv_store->get_iter()->alloc_iter(KV_REGULAR_ITER, pool_id,
                                             pool_hash);

    if (it_id < 0)
    {
        fprintf(stderr, "Error, Iterator Limit Exceeded!\n");
        ret_code = it_id;
        goto kv_begin_exit;
    }

    usr_data_lba = kv_store->get_layout()->get_data_start_lba();
    MAX_VALID_SECTOR = kv_store->get_layout()->get_kv_len() - usr_data_lba;
    //initialize iterator params
    if ((ret_code = kv_store->get_iter()-> \
         init_iter(it_id, usr_data_lba, MAX_VALID_SECTOR,
                   KV_REGULAR_ITER)) < 0)
    {
        goto kv_begin_exit;
    }

    //set the iterator to next available position. If nothing is
    //found, kv_begin should return error of object not found
    ret_code = kv_store->get_iter()->iter_over_pool(it_id, KV_REGULAR_ITER);

kv_begin_exit:
    if (ret_code < 0)
    {
        errno = ret_code;
        ret_code = -1;

        if (it_id >= 0)
        {
            kv_store->get_iter()->free_iterator(it_id, KV_REGULAR_ITER);
        }
    }
    else
    {
        ret_code = it_id;
    }
    return ret_code;
}
//
//moves the iterator one position forward within the current pool
//
int NVM_KV_Store_Mgr::kv_next(int id, int it_id)
{
    int ret_code = NVM_SUCCESS;
    NVM_KV_Store *kv_store = NULL;
    int val_flag = KV_VALIDATE_ID;

    if (kv_internal_validation(val_flag, id, &kv_store, 0, KVNEXT,
                               NULL, 0, NULL, 0, NULL, 0) != NVM_SUCCESS)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto kv_next_exit;
    }

    //set the iterator to next available position. If nothing is
    //found, kv_next should return error of object not found
    ret_code = kv_store->get_iter()->iter_over_pool(it_id, KV_REGULAR_ITER);

kv_next_exit:
    if (ret_code < 0)
    {
        errno = ret_code;
        ret_code = -1;
    }
    return ret_code;
}
//
//fetches Key/Value at current iterator position
//
int NVM_KV_Store_Mgr::kv_get_current(int id, int it_id, nvm_kv_key_t *key,
                                     uint32_t *key_len, void *value,
                                     uint32_t value_len,
                                     nvm_kv_key_info_t *key_info)
{
    char *buf = NULL;
    nvm_kv_header_t *hdr = NULL;
    int ret_code = NVM_SUCCESS;
    uint32_t ret_buf_size = 0;
    uint64_t key_loc = 0;
    uint64_t read_len = 0;
    uint32_t sector_size = 0;
    nvm_kv_store_device_t *kv_device = NULL;
    NVM_KV_Store *kv_store = NULL;
    bool buf_acquired = false;
    int val_flag = KV_VALIDATE_ID | KV_VALIDATE_VALUE;
    int ret_read_len = 0;
    nvm_iovec_t iovec[2]; //num of IOVs will not exceed 2 in case of read
    int iov_count = 0;
    uint64_t val_len_rounded = 0;
    bool user_buffer_usage = false;

    if (kv_internal_validation(val_flag, id, &kv_store, 0, KVGETCURRENT,
                               NULL, 0, value, value_len, NULL, 0) != NVM_SUCCESS)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto end_kv_get_current;
    }

    if (!key_info || !key || !key_len)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto end_kv_get_current;
    }

    kv_device = kv_store->get_store_device();
    sector_size = kv_store->get_sector_size();

    while (true)
    {
        iov_count = 0;
        ret_code = kv_store->get_iter()->get_iter_loc(it_id, &key_loc,
            &read_len, KV_REGULAR_ITER);
        if (ret_code < 0)
        {
            goto end_kv_get_current;
        }
        read_len *= sector_size;
        user_buffer_usage = (read_len > sector_size) ? true : false;
        read_len = user_buffer_usage ? (read_len - sector_size) : \
                   sector_size;
        buf = m_buffer_pool.get_buf(sector_size,
                                                      ret_buf_size);
        if ((ret_buf_size < sector_size) || buf == NULL)
        {
            key_info->key_len = 0;
            key_info->value_len = 0;
            ret_code = -NVM_ERR_OUT_OF_MEMORY;
            goto end_kv_get_current;
        }
        buf_acquired = true;

        //round value in terms of sector
        val_len_rounded = nvm_kv_round_upto_blk(value_len, sector_size);

        if (val_len_rounded < read_len)
        {
            read_len = val_len_rounded;
        }

        iovec[iov_count].iov_base = (uint64_t) buf;
        iovec[iov_count].iov_len = sector_size;
        iovec[iov_count].iov_lba = key_loc;
        iov_count++;
        if (user_buffer_usage)
        {
            iovec[iov_count].iov_base = (uint64_t) value;
            iovec[iov_count].iov_len = read_len;
            iovec[iov_count].iov_lba = key_loc + 1;
            iov_count++;
        }
        ret_read_len = nvm_readv(kv_device, iovec, iov_count);
        if (ret_read_len < 0)
        {
            ret_code = ret_read_len;
            goto end_kv_get_current;
        }

        hdr = (nvm_kv_header_t *) buf;
        if ((ret_read_len == 0) || (hdr->key_len == 0) ||
            (hdr->key_len > NVM_KV_MAX_KEY_SIZE))
        {

            ret_code = kv_store->get_iter()->iter_over_pool(it_id, KV_REGULAR_ITER);
            if (ret_code < 0)
            {
                goto end_kv_get_current;
            }

            m_buffer_pool.release_buf(buf, ret_buf_size);
            continue;
        }

        //copy the key before calling expire so as to check if it
        //expired
        memcpy(key, buf + M_KV_HEADER_SIZE, hdr->key_len);

        //if the key is expired, return error to the user
        ret_code = kv_expire(key, kv_store, hdr, &key_loc, true);
        if (ret_code < 0)
        {
            if (ret_code == -NVM_ERR_OBJECT_NOT_FOUND)
            {
                //the key is expired and hence we need to check for
                //next valid key.So set the iterator to the new position
                //and try to fetch the key again.
                ret_code = kv_store->get_iter()->iter_over_pool(it_id,
                    KV_REGULAR_ITER);
                if (ret_code < 0)
                {
                    goto end_kv_get_current;
                }
                //free the buffer allocated for previous IO
                m_buffer_pool.release_buf(buf,
                                                            ret_buf_size);
                continue;
            }
            goto end_kv_get_current;
        }
        break;
    }

    //set the key_len and value_len to their correct values
    key_info->key_len = hdr->key_len;
    *key_len = hdr->key_len;
    key_info->value_len = hdr->value_len;

    //Copy value and key back to user
    ret_read_len = (hdr->value_len > value_len) ? value_len : hdr->value_len;
    //if value_offset is greater than or equal to sector_size
    //value is already read into user buffer,
    //else copy value from buffer pool buffer to user buffer.
    if (hdr->value_offset < sector_size)
    {
        memcpy(value, buf + hdr->value_offset, ret_read_len);
    }

end_kv_get_current:
    if (ret_code)
    {
        errno = ret_code;
        ret_code = -1;
    }
    else
    {
        ret_code = ret_read_len;
    }
    if (buf_acquired)
    {
        m_buffer_pool.release_buf(buf, ret_buf_size);
    }
    return ret_code;
}
//
//ends iteration and releases the iterator id to free pool
//
int NVM_KV_Store_Mgr::kv_iteration_end(int id, int it_id)
{
    int ret_code = NVM_SUCCESS;
    NVM_KV_Store *kv_store = NULL;
    int val_flag = KV_VALIDATE_ID;

    if (kv_internal_validation(val_flag, id, &kv_store, 0, KVITEREND,
                               NULL, 0, NULL, 0, NULL, 0) != NVM_SUCCESS)
    {
        errno = -NVM_ERR_INVALID_INPUT;
        return -1;
    }

    ret_code = kv_store->get_iter()->free_iterator(it_id, KV_REGULAR_ITER);

    if (ret_code < 0)
    {
        errno = ret_code;
        ret_code = -1;
    }

    return ret_code;
}
//
//get information for the particular KV Store in question.
//
int NVM_KV_Store_Mgr::kv_get_store_info(int kv_id,
                                        nvm_kv_store_info_t *store_info)
{
    int ret_code = NVM_SUCCESS;
    NVM_KV_Store *kv_store = NULL;
    NVM_KV_Cache *kv_cache = NULL;
    nvm_kv_store_metadata_t *kv_store_metadata;
    nvm_capacity_t capacity_info;
    uint64_t usr_data_lba = 0;
    uint64_t max_valid_sector = 0;
    uint32_t sector_size = 0;
    uint64_t free_space = 0;

    if (store_info == NULL)
    {
        errno = -NVM_ERR_INVALID_INPUT;
        return -1;
    }

    if (kv_internal_validation(KV_VALIDATE_ID, kv_id, &kv_store, 0,
                               KVGETSTOREINFO, NULL, 0, NULL, 0, NULL, 0)
                               != NVM_SUCCESS)
    {
        errno = -NVM_ERR_INVALID_INPUT;
        return -1;
    }
    sector_size = kv_store->get_sector_size();
    kv_store_metadata = kv_store->get_store_metadata();
    usr_data_lba = kv_store->get_layout()->get_data_start_lba();
    max_valid_sector = kv_store->get_layout()->get_kv_len() - usr_data_lba;

    kv_cache = kv_store->get_cache();

    if (kv_store_metadata == NULL)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }

    ret_code = nvm_get_capacity(kv_store->get_store_device()->nvm_handle,
                                &capacity_info);
    if (ret_code < 0)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }
    free_space = capacity_info.total_phys_capacity -
                 capacity_info.used_phys_capacity;
    //free space in terms of bytes
    free_space *= sector_size;

    store_info->version = kv_store_metadata->version;
    store_info->num_pools = kv_store_metadata->total_no_pools;
    store_info->num_keys = kv_store->get_iter()->count_ranges(usr_data_lba,
                                                              max_valid_sector);
    store_info->free_space = free_space;
    store_info->max_pools = kv_store_metadata->max_pools;
    store_info->expiry_mode = kv_store_metadata->expiry_mode;
    store_info->global_expiry = kv_store_metadata->global_expiry;

    if (kv_cache)
    {
        store_info->cache_size = kv_cache->kv_cache_get_size();
    }
    else
    {
        store_info->cache_size = 0;
    }

    return ret_code;
}
//
//returns approximate value length of the key
//
int NVM_KV_Store_Mgr::kv_get_val_len(int id, int pool_id, nvm_kv_key_t *key,
                                     uint32_t key_len)
{
    int max_val_len = 0;
    int ret_code = NVM_SUCCESS;
    NVM_KV_Store *kv_store = NULL;
    uint32_t sector_size = 0;

    int val_flag = (KV_VALIDATE_ID | KV_VALIDATE_KEY | KV_VALIDATE_POOL_ID);

    if (kv_internal_validation(val_flag, id, &kv_store, pool_id, KVGETVALLEN,
                               key, key_len, NULL, 0, NULL, 0) != NVM_SUCCESS)
    {
        errno = -NVM_ERR_INVALID_INPUT;
        return -1;
    }

    sector_size = kv_store->get_sector_size();

    ret_code = kv_range_exist_wrapper(kv_store, pool_id, key, key_len,
                                      KVGETVALLEN, &max_val_len, NULL);
    if (ret_code < 0)
    {
        errno = ret_code;
        return -1;
    }

    return (max_val_len * sector_size);
}
//
//get information for the particular key from KV Store.
//
int NVM_KV_Store_Mgr::kv_get_key_info(int kv_id, int pool_id, nvm_kv_key_t *key,
                                      uint32_t key_len,
                                      nvm_kv_key_info_t *key_info)
{
    char *buf = NULL;
    nvm_kv_header_t *hdr = NULL;
    uint32_t ret_buf_size = 0;
    uint32_t sector_size = 0;
    NVM_KV_Store *kv_store = NULL;
    int ret_code = NVM_SUCCESS;
    uint64_t key_hash_val = 0;
    bool buf_acquired = false;
    int val_flag = (KV_VALIDATE_ID | KV_VALIDATE_KEY | KV_VALIDATE_POOL_ID);

    if (kv_internal_validation(val_flag, kv_id, &kv_store, pool_id,
        KVGETKEYINFO, key, key_len, NULL, 0, NULL, 0) != NVM_SUCCESS)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto kv_get_key_info_exit;
    }
    if (!key_info)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto kv_get_key_info_exit;
    }

    sector_size = kv_store->get_sector_size();

    buf = m_buffer_pool.get_buf(sector_size, ret_buf_size);
    if ((ret_buf_size < sector_size) || (buf == NULL))
    {
        ret_code = -NVM_ERR_OUT_OF_MEMORY;
        goto kv_get_key_info_exit;
    }
    buf_acquired = true;

    ret_code = kv_read_media(&hdr, pool_id, kv_store, key, key_len, NULL,
                             0, &key_hash_val, buf, false);
    if (ret_code < 0)
    {
        goto kv_get_key_info_exit;
    }

    //if the key is expired, return error to the user
    ret_code = kv_expire(key, kv_store, hdr, &key_hash_val, true);
    if (ret_code < 0)
    {
        goto kv_get_key_info_exit;
    }

    key_info->pool_id = pool_id;
    key_info->key_len = hdr->key_len;
    key_info->value_len = hdr->value_len;
    key_info->expiry = hdr->metadata.expiry;
    key_info->gen_count = hdr->metadata.gen_count;

kv_get_key_info_exit:
    if (ret_code < 0)
    {
        errno = ret_code;
        ret_code = -1;
    }
    if (buf_acquired)
    {
        m_buffer_pool.release_buf(buf, ret_buf_size);
    }
    return ret_code;
}
//
//get information for the particular pool in question.
//
int NVM_KV_Store_Mgr::kv_get_pool_info(int kv_id, int pool_id,
                                       nvm_kv_pool_info_t *pool_info)
{
    int val_flag = KV_VALIDATE_ID;
    NVM_KV_Store *kv_store = NULL;
    NVM_KV_Pool_Mgr *manager = NULL;

    if (pool_info == NULL)
    {
        errno = -NVM_ERR_INVALID_INPUT;
        return -1;
    }

    if (kv_internal_validation(val_flag, kv_id, &kv_store, 0,
                               KVGETPOOLINFO, NULL, 0,
                               NULL, 0, NULL, 0) != NVM_SUCCESS)
    {
        errno = -NVM_ERR_INVALID_INPUT;
        return -1;
    }

    manager = kv_store->get_pool_mgr();
    if (manager == NULL)
    {
        errno = -NVM_ERR_INVALID_INPUT;
        return -1;
    }
    pool_info->pool_status = manager->check_pool_status(pool_id);
    //current version of pool_info structure
    pool_info->version = 0;

    return NVM_SUCCESS;
}
//
//set global expiry value in KV store metadata
//
int NVM_KV_Store_Mgr::kv_set_global_expiry(int id, uint32_t expiry)
{
    NVM_KV_Store *kv_store = NULL;
    nvm_kv_store_metadata_t *metadata = NULL;

    if (expiry <= 0)
    {
        errno = -NVM_ERR_INVALID_INPUT;
        return -1;
    }

    if (kv_internal_validation(KV_VALIDATE_ID, id, &kv_store, 0,
                               KVSETEXPIRY, NULL, 0, NULL, 0,
                               NULL, 0) != NVM_SUCCESS)
    {
        errno = -NVM_ERR_INVALID_INPUT;
        return -1;
    }

    metadata = kv_store->get_store_metadata();
    if (!metadata)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }

    if (metadata->expiry_mode != KV_GLOBAL_EXPIRY)
    {
        errno = -NVM_ERR_OPERATION_NOT_SUPPORTED;
        return -1;
    }

    //set the expiry in metadata
    metadata->global_expiry = expiry;

    //persist the metadata
    if ((errno = kv_store->persist_kv_metadata()) < 0)
    {
        return -1;
    }

    return NVM_SUCCESS;
}
//
//fetches pool_id and associated tag iteratively for all the pools in a
//KV store
//
int NVM_KV_Store_Mgr::kv_get_pool_metadata(int kv_id,
                                           nvm_kv_pool_metadata_t *pool_md,
                                           uint32_t count,
                                           uint32_t start_count)
{
    NVM_KV_Store *kv_store = NULL;
    NVM_KV_Pool_Mgr *pool_mgr = NULL;
    int pool_id = start_count;
    int num_pools = 0;
    int i = 0;

    if (!pool_md)
    {
        errno = -NVM_ERR_INVALID_INPUT;
        return -1;
    }

    if (kv_internal_validation(KV_VALIDATE_ID | KV_VALIDATE_POOL_ID,
                               kv_id, &kv_store, pool_id,
                               KVGETPOOLMETADATA, NULL, 0, NULL, 0,
                               NULL, 0) != NVM_SUCCESS)
    {
        errno = -NVM_ERR_INVALID_INPUT;
        return -1;
    }
    //do not count the default pool as default pool does not have tag
    num_pools =
        kv_store->get_store_metadata()->total_no_pools - NVM_KV_MIN_POOLS;
    pool_mgr = kv_store->get_pool_mgr();

    for ( ;i < count && pool_id <= num_pools; i++)
    {
        pool_md[i].pool_id = pool_id++;
        if (pool_mgr->get_pool_tag(pool_md[i].pool_id, &pool_md[i].pool_tag) <
            0)
        {
            errno = -NVM_ERR_INTERNAL_FAILURE;
            return -1;
        }
    }
    return i;
}
//
//Deletes all the keys on the drive
//
int NVM_KV_Store_Mgr::kv_delete_all(int id)
{
    NVM_KV_Store *kv_store = NULL;
    int val_flag = KV_VALIDATE_ID;
    int ret_code = NVM_SUCCESS;

    if (kv_internal_validation(val_flag, id, &kv_store, 0, KVDELETEWRAPPER,
                               NULL, 0, NULL, 0, NULL, 0) != NVM_SUCCESS)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
    }
    else
    {
        ret_code = kv_store->delete_all();
    }

    if (ret_code != NVM_SUCCESS)
    {
        errno = ret_code;
        ret_code = -1;
    }

    return ret_code;
}
//
//clean up in-memory data structures related to the KV store.
//KV store data on the drive remains intact.
//
int NVM_KV_Store_Mgr::kv_close(int id)
{
    int ret_code = NVM_SUCCESS;
    NVM_KV_Store *kv_store = NULL;

    if (kv_internal_validation(KV_VALIDATE_ID, id, &kv_store, 0, KVCLOSE, NULL,
        0, NULL, 0, NULL, 0) != NVM_SUCCESS)
    {
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto end_kv_close;
    }

    if (nvm_release_handle(kv_store->get_store_device()->nvm_handle) == -1)
    {
        fprintf(stderr, "Error: nvm_release_handle failed. (%s)\n",
                strerror(errno));
        ret_code = -NVM_ERR_INVALID_INPUT;
        goto end_kv_close;
    }

    delete kv_store;
    //remove KV store id from the map
    m_kvMap.erase(id);

end_kv_close:
    if (ret_code != NVM_SUCCESS)
    {
        errno = ret_code;
        ret_code = -1;
    }

    return ret_code;
}
//
//utility function that queries media for the regions of
//contiguous data in a given range
//
int NVM_KV_Store_Mgr::kv_range_exists(nvm_handle_t handle,
                                      nvm_block_range_t *found_range,
                                      nvm_block_range_t *search_range)
{
    nvm_range_op_t range_exists;
    int ret_code = NVM_SUCCESS;

    if (!search_range || !found_range)
    {
        return -NVM_ERR_INVALID_INPUT;
    }

    range_exists.check_range.start_lba = search_range->start_lba;
    range_exists.check_range.length = search_range->length;

    //make nvm_range_exists call to discover the range
    if ((ret_code = nvm_range_exists(handle, &range_exists)) != 0)
    {
        fprintf(stderr, "Error, nvm api failed in range exists errno %d\n",
                errno);
        return -NVM_ERR_INTERNAL_FAILURE;
    }

    //copy the retrieved data back
    found_range->start_lba = range_exists.range_found.start_lba;
    found_range->length = range_exists.range_found.length;

    if (found_range->length == 0)
    {
        return -NVM_ERR_OBJECT_NOT_FOUND;
    }

    return ret_code;
}
//
//internal API which deletes key value pair on the KV store and synchronizes
//the delete ops
//
int NVM_KV_Store_Mgr::kv_delete_sync(int pool_id, nvm_kv_key_t *key,
                                     uint32_t key_len, NVM_KV_Store *kv_store)
{
    int ret_code = NVM_SUCCESS;
    char *buf = NULL;
    uint32_t ret_buf_size = 0;
    uint32_t sector_size = 0;
    bool buf_acquired = false;
    bool insert_lba = false;
    bool wait = false;
    void *value = NULL;
    uint32_t value_len = 0;
    uint64_t key_hash = 0;
    nvm_kv_header_t *hdr = NULL;
    NVM_KV_Cache *cache = NULL;
    nvm_kv_cache_entry deleted_entry;
    bool found_in_cache = false;

    cache = kv_store->get_cache();
    if (cache)
    {
        if ((ret_code =
            cache->kv_cache_delete(key, key_len, pool_id, deleted_entry))
            != NVM_SUCCESS && ret_code != -NVM_ERR_OBJECT_NOT_FOUND)
        {
            fprintf(stderr, "Failed to delete from the cache.\n");
        }
        else if (ret_code == -NVM_ERR_OBJECT_NOT_FOUND)
        {
            ret_code = NVM_SUCCESS;
        }
        else
        {
            found_in_cache = true;
            key_hash = deleted_entry.lba;
        }
    }

    if (!found_in_cache)
    {
        sector_size = kv_store->get_sector_size();

        buf = m_buffer_pool.get_buf(sector_size, ret_buf_size);
        if ((ret_buf_size < sector_size) || (buf == NULL))
        {
            ret_code = -NVM_ERR_OUT_OF_MEMORY;
            goto end_kv_delete_sync;
        }
        buf_acquired = true;

        ret_code = kv_read_media(&hdr, pool_id, kv_store, key, key_len, value,
                value_len, &key_hash, buf, false);
        if (ret_code < 0)
        {
            //for delete, object_not_found is considered as success
            if (ret_code == -NVM_ERR_OBJECT_NOT_FOUND)
            {
                ret_code = NVM_SUCCESS;
            }
            goto end_kv_delete_sync;
        }
    }

    //to make kv_delete thread safe
    //insert lba in the thread safe lba list before deleting the lba.
    //when more than one thread acquire same lba, first thread which
    //gets the lock on the lba list will first delete that location
    //other threads will wait untill first thread deletes that lba
    if (!kv_store->insert_lba_to_safe_list(key_hash, &wait))
    {
        ret_code = -NVM_ERR_INTERNAL_FAILURE;
        goto end_kv_delete_sync;
    }
    insert_lba = true;

    //If wait is true when insert_lba_to_safe_list returns, the LBA content
    //might be changed by other thread(s), read the media again
    if (wait)
    {
        if (!buf_acquired)
        {
            sector_size = kv_store->get_sector_size();

            buf = m_buffer_pool.get_buf(sector_size, ret_buf_size);
            if ((ret_buf_size < sector_size) || (buf == NULL))
            {
                ret_code = -NVM_ERR_OUT_OF_MEMORY;
                goto end_kv_delete_sync;
            }
            buf_acquired = true;
        }

        ret_code = kv_read_media(&hdr, pool_id, kv_store, key, key_len,
                            value, value_len, &key_hash, buf, false);
        if (ret_code < 0)
        {
            //for delete, object_not_found is considered as success
            if (ret_code == -NVM_ERR_OBJECT_NOT_FOUND)
            {
                ret_code = NVM_SUCCESS;
            }
            goto end_kv_delete_sync;
        }
    }

    if ((ret_code = kv_delete_internal(key_hash, hdr, kv_store))
                                        != NVM_SUCCESS)
    {
        goto end_kv_delete_sync;
    }

end_kv_delete_sync:

    if (buf_acquired)
    {
        m_buffer_pool.release_buf(buf, ret_buf_size);
    }
    if (insert_lba)
    {
        kv_store->delete_lba_from_safe_list(key_hash);
    }
    return ret_code;
}
//
//deletes key value pair in KV store, used internally
//
int NVM_KV_Store_Mgr::kv_delete_internal(uint64_t del_lba,
                                         nvm_kv_header_t *hdr,
                                         NVM_KV_Store *kv_store)
{
    uint64_t num_sect = 0;

    num_sect = kv_store->get_del_sec_count();
    return kv_store->delete_range(del_lba, num_sect);
}
//
//helper functions used to read a KV pair for a specific key from media
//
int NVM_KV_Store_Mgr::kv_read_media(nvm_kv_header_t **header, int pool_id,
                                    NVM_KV_Store *kv_store, nvm_kv_key_t *key,
                                    uint32_t key_len, void *value,
                                    uint32_t value_len, uint64_t *key_hash,
                                    char *buf, bool read_exact)
{
    int ret_code = NVM_SUCCESS;
    ssize_t ret_read_len;
    uint32_t sector_size = 0;
    uint32_t hash_len = 0;
    uint32_t pool_hash = 0;
    nvm_kv_store_device_t *kv_device = NULL;
    nvm_iovec_t iovec[2];//size of the array will not exceed 2
    int iov_count = 0;
    uint32_t hash_itr = 1;
    uint64_t key_hash_val_init = 0;
    uint64_t key_hash_val = 0;
    uint32_t num_iovs = 0;
    nvm_kv_header_t *hdr = NULL;
    NVM_KV_Hash_Func *hash_func = NULL;

    if (kv_store == NULL)
    {
        return -NVM_ERR_INVALID_INPUT;
    }

    hash_len = kv_store->get_layout()->get_key_bits();
    pool_hash = kv_store->get_pool_mgr()->get_poolid_hash(pool_id);
    kv_device = kv_store->get_store_device();
    sector_size = kv_store->get_sector_size();
    hash_func = kv_store->get_hash_func();

    hdr = (nvm_kv_header_t *) buf;
    key_hash_val = key_hash_val_init = hash_func->key_hash((uint8_t *) key,
                                                            key_len, pool_id,
                                                            hash_len, true);
    key_hash_val |= pool_hash;
    iovec[iov_count].iov_base = (uint64_t) buf;
    iovec[iov_count].iov_len = sector_size;
    iov_count++;
    //for deletes only first sector is read
    num_iovs = 1;
    if(value_len > 0)
    {
        num_iovs = 2;
        iovec[iov_count].iov_base= (uint64_t) value;
        iovec[iov_count].iov_len = nvm_kv_round_upto_blk(value_len,
                                                         sector_size);
        iov_count++;
    }

    do
    {
        ret_code = NVM_SUCCESS;
        //if read_exact flag is true, read exact length that is written on
        //media, exact length is obtained by making range exist call
        if (read_exact && value_len)
        {
            nvm_block_range_t found_range;
            nvm_block_range_t search_range;

            search_range.length = kv_store->get_layout()->get_max_search_range();
            search_range.start_lba = key_hash_val;
            ret_code = kv_range_exists(kv_device->nvm_handle, &found_range,
                                       &search_range);

            //if range exists
            if (ret_code == NVM_SUCCESS)
            {
                uint32_t temp_val_len = (found_range.length * sector_size) -
                                         sector_size;
                if (temp_val_len)
                {
                    if (nvm_kv_round_upto_blk(value_len, sector_size) >
                        temp_val_len)
                    {
                        iovec[num_iovs - iov_count + 1].iov_len = temp_val_len;
                    }
                }
                else
                {
                    //if temp_val_len is 0 it means that only one IOV needs
                    //to be read which has both metadata and KV pair
                    num_iovs = 1;
                    iov_count = 1;
                }
            }
            else if (ret_code != -NVM_ERR_OBJECT_NOT_FOUND)
            {
                return ret_code;
            }
        }

        //if range exists
        if (ret_code == NVM_SUCCESS)
        {
            iovec[num_iovs - iov_count].iov_lba = key_hash_val;
            iovec[num_iovs - iov_count + 1].iov_lba = key_hash_val + 1;
            ret_read_len =  nvm_readv(kv_device, iovec, num_iovs);
            if (ret_read_len < 0)
            {
                fprintf(stderr, "Error, reading from device errno %d\n",
                        errno);
                return ret_read_len;
            }

            //if key found, quit rehashing
            if ((ret_read_len != 0) && (hdr->key_len == key_len) &&
                    (memcmp(buf + M_KV_HEADER_SIZE, key, key_len) == 0) &&
                    (hdr->pool_id == pool_id))
            {
                break;
            }
        }

        key_hash_val = hash_func->resolve_coll((uint8_t *) key, key_len,
                                                pool_id, key_hash_val_init,
                                                hash_itr, hash_len, true);
        key_hash_val |= pool_hash;
    } while (hash_itr++ < M_MAX_COLLISION);

    //key was not found by completing the full iteration
    if ((hash_itr > M_MAX_COLLISION))
    {
        return -NVM_ERR_OBJECT_NOT_FOUND;
    }

    ret_code = ret_read_len;
    *header = hdr;
    *key_hash = key_hash_val;
    return ret_code;
}
//
//helper method used to preprocess buffer
//before write, used by APIs like put
//
int NVM_KV_Store_Mgr::kv_process_for_write(int pool_id, nvm_kv_key_t *key,
                                           uint32_t key_len, void *value,
                                           uint32_t value_len, char *buf,
                                           nvm_kv_store_device_t *kv_device,
                                           nvm_iovec_t *iovec,
                                           bool user_buffer_usage,
                                           uint32_t expiry, uint32_t gen_count,
                                           uint64_t key_hash_val,
                                           uint32_t value_offset,
                                           uint32_t *abs_expiry)
{
    nvm_kv_header_t *hdr = NULL;
    char *val_buf = NULL;
    char *temp_buf = NULL;
    uint32_t iovec_count = 0;
    uint32_t count = 0;
    uint32_t remaining_len = 0;
    uint64_t max_write_size_per_iov = 0; //max io size per IOV in bytes
    uint32_t sector_size = kv_device->capabilities.nvm_sector_size;

    hdr = (nvm_kv_header_t *) buf;
    hdr->metadata_len = sizeof(nvm_kv_key_metadata_t);
    hdr->metadata.gen_count = gen_count;
    //Expiry if non zero, is added to the current time
    //in seconds since epoch and stored it on the media.
    //While fetching the key, this time is compared
    //against the fetching time to find if the key is
    //expired or not
    //Expiry if zero, key should never expire
    (expiry == 0) ? (hdr->metadata.expiry = 0) :
                    (hdr->metadata.expiry = (expiry + time(NULL)));
    hdr->metadata.num_key = 0;
    hdr->key_len = key_len;
    hdr->value_len = value_len;
    hdr->pool_id = pool_id;
    hdr->value_offset = value_offset;

    memcpy(buf + M_KV_HEADER_SIZE, key, key_len);
    if (user_buffer_usage)
    {
        iovec[iovec_count].iov_base = (uint64_t)buf;
        iovec[iovec_count].iov_len = sector_size;
        iovec[iovec_count].iov_lba = key_hash_val;
        iovec[iovec_count].iov_opcode = NVM_IOV_WRITE;
        temp_buf = (char *) value;
        iovec_count++;
        key_hash_val++;
        remaining_len =
            nvm_kv_round_upto_blk(value_len, sector_size);
    }
    else
    {
        temp_buf = buf;
        val_buf = buf + value_offset;
        memcpy(val_buf, value, value_len);
        remaining_len = sector_size;
    }

    max_write_size_per_iov =
        kv_device->capabilities.nvm_atomic_write_multiplicity *
        kv_device->capabilities.nvm_max_write_size_per_iov;
    while (remaining_len)
    {
        int vector_size = (remaining_len < max_write_size_per_iov) ?
                          remaining_len : max_write_size_per_iov;
        iovec[iovec_count].iov_base = (uint64_t) temp_buf + (count * \
                                      max_write_size_per_iov);
        //vector_size in number of packets
        iovec[iovec_count].iov_len = vector_size;
        iovec[iovec_count].iov_lba = key_hash_val + (count *
            (max_write_size_per_iov / sector_size));
        iovec[iovec_count].iov_opcode = NVM_IOV_WRITE;
        iovec_count++;
        count++;
        remaining_len -= vector_size;
    }

    //return the absolute expiry time
    if (abs_expiry && expiry)
    {
        *abs_expiry = hdr->metadata.expiry;
    }

    return NVM_SUCCESS;

}
//
//helper method which preprocesses trim IOVs, used in APIs like kv_put and
//kv_batch_put
//
int NVM_KV_Store_Mgr::kv_process_for_trim(uint32_t value_len, uint32_t trim_len,
                                          nvm_iovec_t *iovec,
                                          uint32_t *iovec_count, uint64_t lba,
                                          nvm_kv_store_device_t *kv_device)
{
    uint64_t max_trim_size_per_iov = 0;
    uint32_t sector_size = kv_device->capabilities.nvm_sector_size;

    if (!iovec_count || !iovec)
    {
        return -NVM_ERR_INVALID_INPUT;
    }

    max_trim_size_per_iov =
            kv_device->capabilities.nvm_atomic_write_multiplicity *
            kv_device->capabilities.nvm_max_trim_size_per_iov;
    if (trim_len > max_trim_size_per_iov)
    {
        return -NVM_ERR_INTERNAL_FAILURE;
    }
    else if (trim_len)
    {
        uint32_t trim_bytes = nvm_kv_round_upto_blk(trim_len, sector_size);
        uint32_t value_bytes = nvm_kv_round_upto_blk(value_len, sector_size);

        trim_len = trim_bytes - value_bytes;
        if (trim_len > 0)
        {
            iovec[*iovec_count].iov_base = 0;
            iovec[*iovec_count].iov_len = trim_len;
            iovec[*iovec_count].iov_lba = lba + (value_bytes / sector_size);
            iovec[*iovec_count].iov_opcode = NVM_IOV_TRIM;
            *iovec_count = (*iovec_count) + 1;
        }
    }
    return NVM_SUCCESS;
}
//
//helper method used to allocate buffer before
//write, used by APIs like put, batch_put
//
int NVM_KV_Store_Mgr::kv_alloc_for_write(nvm_kv_store_device_t *kv_device,
                                         uint32_t key_len,
                                         void *value, uint32_t value_len,
                                         char **buf, uint32_t *ret_buf_size,
                                         uint32_t *iov_count,
                                         bool *user_buffer_usage,
                                         uint32_t *value_offset)
{
    uint32_t sum_write_len = 0;
    uint32_t buf_len = 0;
    uint32_t iov_len = 0;
    uint32_t sector_size = kv_device->capabilities.nvm_sector_size;
    uint64_t max_write_size_per_iov = 0;

    if(!ret_buf_size || !iov_count || !user_buffer_usage)
    {
        //setting this as internal error since
        //this API is internal and these param should
        //always be valid
        return -NVM_ERR_INTERNAL_FAILURE;
    }
    max_write_size_per_iov =
        kv_device->capabilities.nvm_atomic_write_multiplicity *
        kv_device->capabilities.nvm_max_write_size_per_iov;
    sum_write_len = M_KV_HEADER_SIZE + key_len + value_len;
    *user_buffer_usage = kv_use_user_buf(sum_write_len, sector_size);
    //check if user buffer passed is sector aligned
    //if user buffer is not sector aligned and user_buffer_usage is true
    //recalculate sum_write_len and set user_buffer_usage to false
    if (*user_buffer_usage && ((uint64_t) value & (sector_size - 1)))
    {
        *user_buffer_usage = false;
        //sector for metadata + value length
        //one sector for metadata is required because in kv_get
        //read is done as two IOVs, first IOV reads metadata and second reads
        //the actual value which has user buffer
        sum_write_len = sector_size + value_len;
    }

    if (*user_buffer_usage)
    {
        *value_offset = sector_size;
        buf_len = sector_size;
        iov_len = nvm_kv_round_upto_blk(value_len, sector_size);
        //extra IOV for metadata
        *iov_count = (*iov_count) + 1;
    }
    else
    {
        //if not sector aligned value will start after metadata sector
        if (((uint64_t) value & (sector_size - 1))
            && (sum_write_len > sector_size))
        {
            *value_offset = sector_size;
        }
        else
        {
            *value_offset = M_KV_HEADER_SIZE + key_len;
        }
        buf_len = iov_len =
            nvm_kv_round_upto_blk(sum_write_len, sector_size);
    }
    //calculate how many IOVs are needed to hold num_sect
    *iov_count = (*iov_count) +
                 (nvm_kv_round_upto_blk(iov_len, max_write_size_per_iov) /
                  max_write_size_per_iov);

    if (*iov_count > kv_device->capabilities.nvm_max_num_iovs)
    {
        fprintf( stderr, "Error, IOV count larger than max IOV count in kv put\n");
        return -NVM_ERR_INVALID_INPUT;
    }

    *buf =  m_buffer_pool.get_buf(buf_len, *ret_buf_size);
    if ((*ret_buf_size < buf_len) || *buf == NULL)
    {
        fprintf( stderr, "Error, cannot allocate memory in kv_put\n");
        return -NVM_ERR_OUT_OF_MEMORY;
    }
    return NVM_SUCCESS;
}
///
///get the store object for the given store id
///
NVM_KV_Store* NVM_KV_Store_Mgr::get_store(int id)
{
    map<int, NVM_KV_Store*>::iterator itr;

    itr = m_kvMap.find(id);
    if (itr == m_kvMap.end())
    {
        return NULL;
    }
    else
    {
        return itr->second;
    }
}
//
//validation of parameters passed into APIs
//
int NVM_KV_Store_Mgr::kv_internal_validation(int flag, int id,
                                             NVM_KV_Store **kv_store,
                                             int pool_id, nvm_kv_api api,
                                             nvm_kv_key_t *key,
                                             uint32_t key_len, void *value,
                                             uint32_t value_len,
                                             nvm_kv_iovec_t *kv_iov,
                                             int iov_count)
{
    int ret_code = 0;
    map<int, NVM_KV_Store*>::iterator blk_itr;
    NVM_KV_Pool_Mgr *pool_mgr = NULL;

    if (flag & KV_VALIDATE_ID)
    {
        blk_itr = m_kvMap.find(id);
        if (blk_itr == m_kvMap.end())
        {
            fprintf(stderr, "Error, validation failed, id unknown\n");
            ret_code = -NVM_ERR_INVALID_INPUT;
            goto end_kv_validation;
        }
        *kv_store = blk_itr->second;
        if (flag & KV_VALIDATE_POOL_ID)
        {
            pool_mgr = (*kv_store)->get_pool_mgr();

            if (api == KVGETPOOLMETADATA || api == KVPOOLDELETE)
            {
                if (pool_id == pool_mgr->get_default_poolid())
                {
                    fprintf(stderr, "Error, validation failed, pool id "
                            "%u incorrect\n", pool_id);
                    ret_code = -NVM_ERR_INVALID_INPUT;
                    goto end_kv_validation;
                }
            }

            if (pool_mgr->check_pool_status(pool_id) != POOL_IN_USE
                && pool_id != pool_mgr->get_all_poolid())
            {
                fprintf(stderr, "Error, validation failed, pool id "
                        "%u incorrect\n", pool_id);
                ret_code = -NVM_ERR_INVALID_INPUT;
                goto end_kv_validation;
            }
        }
    }
    if (flag & KV_VALIDATE_KEY)
    {
        if (key_len == 0 || key_len > NVM_KV_MAX_KEY_SIZE || !key)
        {
            fprintf(stderr, "Error, validation failed, key incorrect\n");
            ret_code = -NVM_ERR_INVALID_INPUT;
            goto end_kv_validation;
        }
    }
    if (flag & KV_VALIDATE_VALUE)
    {
        int sector_size =
            (*kv_store)->get_store_device()->capabilities.nvm_sector_size;

        if (value_len == 0 || !value || (value_len > NVM_KV_MAX_VALUE_SIZE))
        {
            fprintf(stderr, "Error, validation failed, value incorrect\n");
            ret_code = -NVM_ERR_INVALID_INPUT;
            goto end_kv_validation;
        }
        //if the API is kv_get and the value_len is not multiple of
        //sector_size, return error
        if (((api == KVGET) || (api == KVBATCHGET))
             && (value_len & (sector_size - 1)))
        {
            ret_code = -NVM_ERR_INVALID_INPUT;
            goto end_kv_validation;
        }
    }
    if (flag & KV_VALIDATE_BATCH)
    {
        if(!kv_iov)
        {
            fprintf(stderr, "Error, validation failed, IOV null\n");
            ret_code = -NVM_ERR_INVALID_INPUT;
            goto end_kv_validation;
        }
        if(iov_count == 0)
        {
            fprintf(stderr, "Error, validation failed, count incorrect\n");
            ret_code = -NVM_ERR_INVALID_INPUT;
            goto end_kv_validation;
        }
    }

    ret_code = NVM_SUCCESS;

end_kv_validation:

    return ret_code;
}
//
//exist call before writing on to the LBA
//
int NVM_KV_Store_Mgr::kv_gen_lba(NVM_KV_Store *kv_store, nvm_kv_key_t *key,
                                 uint32_t key_len, int pool_id,
                                 uint32_t value_len, char *buf, bool replace,
                                 uint32_t *trim_len, uint64_t &key_hash_val,
                                 bool &insert_lba,
                                 map<uint64_t, nvm_kv_iovec_t*> *lba_list)
{
    uint32_t hash_itr = 1;
    bool read_retry = false;
    uint64_t key_hash_val_init = 0;
    int ret_code = 0;
    nvm_kv_store_device_t *kv_device = kv_store->get_store_device();
    uint32_t sector_size = kv_store->get_sector_size();
    nvm_kv_header_t *hdr = (nvm_kv_header_t *) buf;
    uint32_t pool_hash = kv_store->get_pool_mgr()->get_poolid_hash(pool_id);
    uint32_t hash_len = kv_store->get_layout()->get_key_bits();
    NVM_KV_Hash_Func *hash_func = NULL;
    bool del_exp = false;

    hash_func = kv_store->get_hash_func();

    *trim_len = 0;
    //generate hash
    key_hash_val = hash_func->key_hash((uint8_t *) key, key_len, pool_id,
                                       hash_len, true);
    key_hash_val_init = key_hash_val;
    key_hash_val |= pool_hash;
    //already LBA is been used by other key within same batch
    //find next available
    if (lba_list)
    {
        map<uint64_t, nvm_kv_iovec_t*>::iterator itr = lba_list-> \
                                                   find(key_hash_val);

        while (itr != lba_list->end() && hash_itr < M_MAX_COLLISION)
        {
            nvm_kv_iovec_t *dup_keys = itr->second;

            //if same key already exists in batch put just return
            if (dup_keys->key_len == key_len &&
                (memcmp(dup_keys->key, key, key_len) == 0))
            {
                return NVM_SUCCESS;
            }
            key_hash_val = hash_func-> \
                           resolve_coll((uint8_t *) key, key_len, pool_id,
                                        key_hash_val_init, hash_itr, hash_len,
                                        true);
            key_hash_val |= pool_hash;
            hash_itr++;
            itr = lba_list->find(key_hash_val);
        }
        if (hash_itr == M_MAX_COLLISION)
        {
            return -NVM_ERR_INTERNAL_FAILURE;
        }
    }

    //continue re-hashing
    do
    {
        bool wait = false;
        ssize_t ret_read_len = 0;
        nvm_iovec_t iovec; //num of IOVs will not exceed 1
        int iov_count = 1;
        nvm_block_range_t found_range;
        nvm_block_range_t search_range;

        search_range.length = kv_store->get_layout()->get_max_search_range();
        search_range.start_lba = key_hash_val;
        ret_code = kv_range_exists(kv_store->get_store_device()->nvm_handle,
                                   &found_range, &search_range);
        //range does not exists
        if (ret_code == -NVM_ERR_OBJECT_NOT_FOUND)
        {
            //to make kvPut thread safe
            //insert lba in the thread safe lba list before writing
            //to the lba.
            //when more than one thread acquires same lba, first thread which
            //gets the lock on the lba list will first write to that location
            if (!kv_store->insert_lba_to_safe_list(key_hash_val, &wait))
            {
                return -NVM_ERR_INTERNAL_FAILURE;
            }
            insert_lba = true;
            if (!wait)
            {
                break;
            }
        }
        else if (ret_code != NVM_SUCCESS)
        {
            return -NVM_ERR_INTERNAL_FAILURE;
        }

        do
        {
            iovec.iov_base = (uint64_t) buf;
            iovec.iov_len = sector_size;
            iovec.iov_lba = key_hash_val;
            ret_read_len = nvm_readv(kv_device, &iovec, iov_count);
            if (ret_read_len < 0)
            {
                fprintf(stderr, "Error, reading data buffer, \
                        %d bytes read out of %d\n", (int) ret_read_len,
                        sector_size);
                return ret_read_len;
            }

            if ((hdr->key_len == key_len) &&
                 (memcmp(buf + M_KV_HEADER_SIZE, key, key_len) == 0) &&
                 (hdr->pool_id == pool_id))
            {
                ret_code = kv_expire(key, kv_store, hdr, &key_hash_val,
                                     del_exp);
                if (ret_code < 0)
                {
                    if (ret_code == -NVM_ERR_OBJECT_NOT_FOUND)
                    {
                        //key is expired, so allow put to proceed
                        //return success here
                        if (!insert_lba)
                        {
                            if (!kv_store->insert_lba_to_safe_list(key_hash_val,
                                                                   &wait))
                            {
                               return -NVM_ERR_INTERNAL_FAILURE;
                            }

                            insert_lba = true;

                            //check if we had to wait on the safe list. If
                            //we did, that means another thread has got here
                            //first, so keep the LBA in the safe list and
                            //retry the read
                            if (wait)
                            {
                                read_retry = true;
                                continue;
                            }
                        }
                        ret_code = NVM_SUCCESS;
                    }

                    return ret_code;
                 }

                 //key is not expired, check for replace
                 if (replace)
                 {
                     //add the LBA to the safe list if not already inserted
                     if (!insert_lba)
                     {
                         if (!kv_store->insert_lba_to_safe_list(key_hash_val,
                                                                &wait))
                         {
                             return -NVM_ERR_INTERNAL_FAILURE;
                         }

                         insert_lba = true;

                         if (!wait)
                         {
                             if (hdr->value_len > value_len)
                             {
                                 *trim_len = hdr->value_len;
                             }

                             break;
                         }

                         read_retry = true;
                         continue;
                     }
                     else
                     {
                         //discovered a duplicate key, so replace
                         if (hdr->value_len > value_len)
                         {
                             *trim_len = hdr->value_len;
                         }
                     }
                 }
                 else
                 {
                     return -NVM_ERR_OBJECT_EXISTS;
                 }
                 break;
            }
            else
            {
                //The keys don't match, we need to look for a different
                //LBA
                if (insert_lba)
                {
                    kv_store->delete_lba_from_safe_list(key_hash_val);
                    insert_lba = false;
                }

                read_retry = false;
                break;
            }
        } while (read_retry);

        //The LBA has been inserted into the safe list, which means we
        //are all set to work on that LBA, so break
        if (insert_lba)
        {
            break;
        }

        //continue to rehash to find the new LBA
        key_hash_val = hash_func->resolve_coll((uint8_t *) key, key_len,
                                                pool_id, key_hash_val_init,
                                                hash_itr, hash_len, true);
        key_hash_val |= pool_hash;
    } while (hash_itr++ < M_MAX_COLLISION);

    if (hash_itr > M_MAX_COLLISION)
    {
        return -NVM_ERR_INTERNAL_FAILURE;
    }
    return NVM_SUCCESS;
}
//
//internal API called to check whether to use
//user buffer or buffer from bufferPool
//
bool NVM_KV_Store_Mgr::kv_use_user_buf(uint64_t sum_write_len,
                                       uint32_t sector_size)
{
    return (sum_write_len <= sector_size ? false : true);
}
//
//compute number of sparse address
//bits based on device or file size
//
int NVM_KV_Store_Mgr::comp_sparse_addr_bits(uint64_t size,
                                            uint32_t sector_size)
{
    int size_bits = 0;
    int sector_bits = 0;
    while (size)
    {
        size_bits++;
        size = size >> 1;
    }

    while (sector_size)
    {
        sector_bits++;
        sector_size = sector_size >> 1;
    }
    size_bits = size_bits - sector_bits;
    return size_bits;
}
//
//Checks if the key is expired and if it is, then calls delete
//
int NVM_KV_Store_Mgr::kv_expire(nvm_kv_key_t *key, NVM_KV_Store *kv_store,
                                nvm_kv_header_t *hdr, uint64_t *key_loc,
                                bool del_exp)
{
    uint32_t curtime = 0;
    int ret_code = 0;
    uint32_t expiry = hdr->metadata.expiry;

    //check if expiry is disabled, if so no need to check for key expiry
    if (kv_store->get_expiry() == KV_DISABLE_EXPIRY)
    {
        return NVM_SUCCESS;
    }

    //if the key is expired, return error to the user
    curtime = time(NULL);

    if ((curtime > expiry) && (expiry != 0))
    {
	if (!del_exp)
	{
	    return -NVM_ERR_OBJECT_NOT_FOUND;
	}
	ret_code =
            ((NVM_KV_Async_Expiry *)(kv_store->get_async_expiry_thread()))
            ->update_expiry_queue(*key_loc, hdr);

        if (ret_code < 0)
        {
            //capture the delete error
            return ret_code;
        }
        else
        {
            //even if delete is successful we need to send
            //error as the key is expired
            return  -NVM_ERR_OBJECT_NOT_FOUND;
        }
    }

    return NVM_SUCCESS;
}
//
//wrapper for going over all collisions of keys checking for range exists
//and performing actions on found range according to requirement
//
int NVM_KV_Store_Mgr::kv_range_exist_wrapper(NVM_KV_Store *kv_store,
                                             int pool_id,
                                             nvm_kv_key_t *key,
                                             uint32_t key_len,
                                             nvm_kv_api api, int *max_val_len,
                                             nvm_kv_header_t **hdr)
{
    char *buf = NULL;
    uint32_t hash_itr = 1;
    uint32_t sector_size = 0;
    uint32_t ret_buf_size = 0;
    uint32_t read_bytes = 0;
    bool buf_acquired = false;
    uint64_t key_hash_val_init = 0;
    uint64_t key_hash_val = 0;
    int ret_code = NVM_SUCCESS;
    uint32_t pool_hash = 0;
    uint32_t hash_len = 0;
    nvm_block_range_t found_range;
    nvm_block_range_t search_range;
    nvm_kv_store_device_t *kv_device = NULL;
    nvm_iovec_t iovec; //num of IOVs will not exceed 1
    int iov_count = 1;
    NVM_KV_Hash_Func *hash_func = NULL;

    if (api == KVGETVALLEN)
    {
        if (max_val_len == NULL)
        {
            ret_code = -NVM_ERR_INVALID_INPUT;
            goto end_kv_range_exist_wrapper;
        }
        else
        {
            *max_val_len = 0;
        }
    }

    hash_len = kv_store->get_layout()->get_key_bits();
    pool_hash = kv_store->get_pool_mgr()->get_poolid_hash(pool_id);
    kv_device = kv_store->get_store_device();
    sector_size = kv_store->get_sector_size();
    hash_func = kv_store->get_hash_func();

    if (api == KVEXISTS)
    {
        buf = m_buffer_pool.get_buf(sector_size, ret_buf_size);
        if ((ret_buf_size < sector_size) || buf == NULL)
        {
            ret_code = -NVM_ERR_OUT_OF_MEMORY;
            goto end_kv_range_exist_wrapper;
        }
        buf_acquired = true;
        *hdr = (nvm_kv_header_t *) buf;
    }

    //generate hash
    key_hash_val_init = key_hash_val = hash_func-> \
                                       key_hash((uint8_t *)key, key_len,
                                                pool_id, hash_len, true);
    key_hash_val |= pool_hash;
    do
    {
        //search length in terms of sectors
        search_range.length = kv_store->get_layout()->get_max_search_range();
        search_range.start_lba = key_hash_val;
        ret_code = kv_range_exists(kv_device->nvm_handle, &found_range,
                                   &search_range);
        if (ret_code < 0 && ret_code != -NVM_ERR_OBJECT_NOT_FOUND)
        {
            ret_code = -NVM_ERR_INTERNAL_FAILURE;
            goto end_kv_range_exist_wrapper;
        }

        //if there exists KV pair read and compare
        //for call from kv_exists
        if (ret_code == NVM_SUCCESS)
        {
            if (api == KVEXISTS)
            {
                iovec.iov_base = (uint64_t) buf;
                iovec.iov_len = sector_size;
                iovec.iov_lba = key_hash_val;
                read_bytes = nvm_readv(kv_device, &iovec, iov_count);
                if (read_bytes < 0)
                {
                    fprintf(stderr, "Error, reading data buffer failed during"
                            "exists call\n");
                    ret_code = read_bytes;
                    goto end_kv_range_exist_wrapper;
                }

                if ((read_bytes > 0 ) && ((*hdr)->key_len == key_len) &&
                    (memcmp(buf + M_KV_HEADER_SIZE, key, key_len) == 0) &&
                    ((*hdr)->pool_id == pool_id))
                {
                    //if the key is expired, return error to the user
                    ret_code = kv_expire(key, kv_store, *hdr, &key_hash_val, true);
                    goto end_kv_range_exist_wrapper;
                }
            }
            else if (api == KVGETVALLEN)
            {
                //update the max value
                if (found_range.length > *max_val_len)
                {
                    //update the maximum value length seen
                    //so far
                    *max_val_len = found_range.length;
                }
            }
        }
        key_hash_val = hash_func->resolve_coll((uint8_t *)key, key_len,
                                               pool_id, key_hash_val_init,
                                               hash_itr, hash_len, true);
        key_hash_val |= pool_hash;
    } while(hash_itr++ < M_MAX_COLLISION);

    if (api == KVGETVALLEN && *max_val_len > 0)
    {
        ret_code = NVM_SUCCESS;
    }

    //key was not found by completing the full iteration
    if ((hash_itr > M_MAX_COLLISION) && (api == KVEXISTS))
    {
        ret_code = -NVM_ERR_OBJECT_NOT_FOUND;
    }

end_kv_range_exist_wrapper:
    if(buf_acquired && ret_buf_size)
    {
       m_buffer_pool.release_buf(buf, ret_buf_size);
    }

    return ret_code;
}
