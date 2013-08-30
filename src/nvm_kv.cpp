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
#include <pthread.h>
#include "src/kv_store_mgr.h"
//
//initializes KV store and writes KV store metadata if
//necessary
//
int nvm_kv_open(int id, uint32_t version, uint32_t max_pools,
                  uint32_t expiry)
{
    try
    {
        return NVM_KV_Store_Mgr::instance(true)->kv_open(id, version,
                                                         max_pools,
                                                         expiry);
    }
    catch (...)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }
}
//
//creates a new pool for the given KV store
//
int nvm_kv_pool_create(int id, nvm_kv_pool_tag_t *pool_tag)
{
    try
    {
        return NVM_KV_Store_Mgr::instance()->kv_pool_create(id, pool_tag);
    }
    catch (...)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }
}
//
//returns information about a given pool in store
//
int nvm_kv_get_pool_info(int id, int pool_id, nvm_kv_pool_info_t *pool_info)
{
    try
    {
        return NVM_KV_Store_Mgr::instance()->kv_get_pool_info(id, pool_id,
                                                              pool_info);
    }
    catch (...)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }
}
//
//deletes a pool from the given KV store
//
int nvm_kv_pool_delete(int kv_id, int pool_id)
{
    try
    {
        return NVM_KV_Store_Mgr::instance()->kv_pool_delete(kv_id, pool_id);
    }
    catch (...)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }
}
//
//deletes all the keys in the given KV store
//
int nvm_kv_delete_all(int kv_id)
{
    try
    {
        return NVM_KV_Store_Mgr::instance()->kv_delete_all(kv_id);
    }
    catch (...)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }
}
//
//clean up in-memory data structures related to the KV store.
//KV store data on the drive remains intact.
//
int nvm_kv_close(int kv_id)
{
    try
    {
        return NVM_KV_Store_Mgr::instance()->kv_close(kv_id);
    }
    catch (...)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }
}
//
//puts key value pair in KV store
//
int nvm_kv_put(int id, int pool_id, nvm_kv_key_t *key, uint32_t key_len,
               void *value, uint32_t value_len, uint32_t expiry, bool replace,
               uint32_t gen_count)
{
    try
    {
       return NVM_KV_Store_Mgr::instance()->kv_put(id, pool_id, key, key_len,
                                                   value, value_len, expiry,
                                                   replace, gen_count);
    }
    catch (...)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }
}
//
//gets key value pair in KV store
//
int nvm_kv_get (int id, int pool_id, nvm_kv_key_t *key, uint32_t key_len,
                void *value, uint32_t value_len, bool read_exact,
                nvm_kv_key_info_t *key_info)
{
    try
    {
       return NVM_KV_Store_Mgr::instance()->kv_get(id, pool_id, key, key_len,
                                                   value, value_len,
                                                   read_exact, key_info);
    }
    catch (...)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }

}
//
//deletes key value pair in KV store
//
int nvm_kv_delete(int id, int pool_id, nvm_kv_key_t *key, uint32_t key_len)
{
    try
    {
       return NVM_KV_Store_Mgr::instance()->kv_delete(id, pool_id, key,
                                                      key_len);
    }
    catch (...)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }
}
//
//checks if key value pair exists in KV store
//
int nvm_kv_exists(int id, int pool_id, nvm_kv_key_t *key, uint32_t key_len,
                  nvm_kv_key_info_t *key_info)
{
    try
    {
       return NVM_KV_Store_Mgr::instance()->kv_exists(id, pool_id, key,
                                                      key_len, key_info);
    }
    catch (...)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }
}
//
//returns approximate value length for the key
//
int nvm_kv_get_val_len(int id, int pool_id, nvm_kv_key_t *key,
                       uint32_t key_len)
{
    try
    {
       return NVM_KV_Store_Mgr::instance()->kv_get_val_len(id, pool_id, key,
                                                           key_len);
    }
    catch (...)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }
}
//
//puts all KV pair in one batch operation
//
int nvm_kv_batch_put(int id, int pool_id, nvm_kv_iovec_t *kv_iov,
                     uint32_t iov_count)
{
    try
    {
       return NVM_KV_Store_Mgr::instance()->kv_batch_put(id, pool_id, kv_iov,
                                                         iov_count);
    }
    catch (...)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }
}

//
//sets iterator to beginning of a given pool
//
int nvm_kv_begin(int id, int pool_id)
{
    try
    {
        return NVM_KV_Store_Mgr::instance()->kv_begin(id, pool_id);
    }
    catch (...)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }
}

//
//sets iterator to the next location in given pool
//
int nvm_kv_next(int id, int it_id)
{
    try
    {
        return NVM_KV_Store_Mgr::instance()->kv_next(id, it_id);
    }
    catch (...)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }
}
//
//retrieves key-value pair at current iterator location in pool
//
int nvm_kv_get_current(int id, int it_id, nvm_kv_key_t *key,
                       uint32_t *key_len, void *value, uint32_t value_len,
                       nvm_kv_key_info_t *key_info)
{
    try
    {
       return NVM_KV_Store_Mgr::instance()->kv_get_current(id, it_id, key,
                                                           key_len, value,
                                                           value_len,
                                                           key_info);
    }
    catch (...)
    {
        errno = -NVM_ERR_INTERNAL_FAILURE;
        return -1;
    }
}
//
//ends iteration and releases the iterator id to free pool
//
int nvm_kv_iteration_end(int id, int it_id)
{
    try
    {
       return NVM_KV_Store_Mgr::instance()->kv_iteration_end(id, it_id);
    }
    catch (...)
    {
       errno = -NVM_ERR_INTERNAL_FAILURE;
       return -1;
    }
}
//
//retrieves metadata information about a KV store
//
int nvm_kv_get_store_info(int kv_id, nvm_kv_store_info_t *store_info)
{
    try
    {
       return NVM_KV_Store_Mgr::instance()->kv_get_store_info(kv_id,
                                                              store_info);
    }
    catch (...)
    {
       errno = -NVM_ERR_INTERNAL_FAILURE;
       return -1;
    }
}
//
//retrieves metadata information about a specific key
//
int nvm_kv_get_key_info(int kv_id, int pool_id, nvm_kv_key_t *key,
                        uint32_t key_len, nvm_kv_key_info_t *key_info)
{
    try
    {
       return NVM_KV_Store_Mgr::instance()->kv_get_key_info(kv_id, pool_id,
                                                            key, key_len,
                                                            key_info);
    }
    catch (...)
    {
       errno = -NVM_ERR_INTERNAL_FAILURE;
       return -1;
    }
}
//
//sets the global expiry value for KV store
//
int nvm_kv_set_global_expiry(int kv_id, uint32_t expiry)
{
    try
    {
       return NVM_KV_Store_Mgr::instance()->kv_set_global_expiry(kv_id,
                                                                 expiry);
    }
    catch (...)
    {
       errno = -NVM_ERR_INTERNAL_FAILURE;
       return -1;
    }
}
//
//fetches pool_id and associated tag iteratively for all the pools in a
//KV store
//
int nvm_kv_get_pool_metadata(int kv_id, nvm_kv_pool_metadata_t *pool_md,
                             uint32_t count, uint32_t start_count)
{
    try
    {
       return NVM_KV_Store_Mgr::instance()->kv_get_pool_metadata(kv_id,
                                                                 pool_md,
                                                                 count,
                                                                 start_count);
    }
    catch (...)
    {
       errno = -NVM_ERR_INTERNAL_FAILURE;
       return -1;
    }
}
