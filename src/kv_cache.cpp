//----------------------------------------------------------------------------
// NVMKV
// |- Copyright 2013-2014 Fusion-io, Inc.

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
#include "src/kv_cache.h"
#include "src/kv_store.h"

NVM_KV_Cache::NVM_KV_Cache(NVM_KV_Store *kv_store)
{
    m_kv_store  = kv_store;
}
//
//initializes cache size, creates instance of NVM_KV_Hash_Func
//
int NVM_KV_Cache::initialize(uint64_t cache_size)
{
    m_cache_addr_bits = 0;
    int ret_code = NVM_SUCCESS;
    int count= 0, idx = 0;
    uint64_t num_cache_entries;

    num_cache_entries = cache_size/(sizeof(nvm_kv_cache_entry *) +
                      sizeof(nvm_kv_cache_entry));

    while ((num_cache_entries = num_cache_entries >> 1))
    {
        m_cache_addr_bits++;
    }

    //re-adjust the total entry count to match the m_cache_addr_bits
    m_total_entries = (uint64_t)1 << m_cache_addr_bits;

    m_hash_func = new(std::nothrow) NVM_KV_Hash_Func();
    if (m_hash_func == NULL)
    {
        ret_code = -NVM_ERR_OUT_OF_MEMORY;
        goto end_initialize;
    }
    if (m_hash_func->initialize(FNV1A, SEQUENTIAL_PROBE,
                                m_cache_addr_bits, 0) != 0)
    {
        fprintf(stderr, "Error,"
                " hash initialization failed during cache init\n");
        ret_code = -NVM_ERR_INTERNAL_FAILURE;
        goto end_initialize;
    }

    m_hash_table = new(std::nothrow) nvm_kv_cache_entry*[m_total_entries];
    if (m_hash_table == NULL)
    {
        fprintf(stderr, "Error,"
                " failed to allocate memory for collision cache hash table\n");
        ret_code = -NVM_ERR_OUT_OF_MEMORY;
        goto end_initialize;
    }

    memset(m_hash_table, 0, sizeof(nvm_kv_cache_entry *) * m_total_entries);

    for (count = 0; count < m_total_entries; count++)
    {
        m_hash_table[count] = new(std::nothrow) nvm_kv_cache_entry;

        if (!m_hash_table[count])
        {
            fprintf(stderr, "Error,"
                    " failed to allocate memory for collision cache entry\n");
            ret_code = -NVM_ERR_OUT_OF_MEMORY;
            goto end_initialize;
        }

        memset(m_hash_table[count], 0, sizeof(nvm_kv_cache_entry));

        m_hash_table[count]->index = count;
    }

    //the amount of memory (in bytes) the cache needs to hold
    m_cache_size = (sizeof(nvm_kv_cache_entry *) + sizeof(nvm_kv_cache_entry))
                    * m_total_entries;

    pthread_rwlock_init(&m_cache_rwlock, NULL);

end_initialize:

   if (ret_code < 0)
   {
       if (m_hash_table)
       {
           for (idx = 0; idx < count; idx++)
           {
               delete m_hash_table[idx];
               m_hash_table[idx] = NULL;
           }

           delete m_hash_table;
           m_hash_table = NULL;
       }

       delete m_hash_func;
       m_hash_func = NULL;
   }

   return ret_code;
}
//
//destroys instance of NVM_KV_Hash_Func, NVM_KV_Sync_List
//
NVM_KV_Cache::~NVM_KV_Cache()
{
    nvm_kv_cache_entry *cache_entry = NULL;

    if (m_hash_table)
    {
        for (int idx = 0; idx < m_total_entries; idx++)
        {
            cache_entry = m_hash_table[idx];

            delete cache_entry;
        }

        delete m_hash_table;
    }

    delete m_hash_func;

    pthread_rwlock_destroy(&m_cache_rwlock);
}
//
//acquire the write lock on the cache
//
void NVM_KV_Cache::kv_cache_acquire_wrlock()
{
    pthread_rwlock_wrlock(&m_cache_rwlock);
}
//
//release the lock on the cache
//
void NVM_KV_Cache::kv_cache_release_lock()
{
    pthread_rwlock_unlock(&m_cache_rwlock);
}
//
//writes key-lba mapping into cache
//
int NVM_KV_Cache::kv_cache_put(nvm_kv_key_t *key, uint32_t key_len,
                               int pool_id, uint32_t value_len,
                               uint32_t abs_expiry, uint32_t gen_count,
                               uint64_t lba,
                               nvm_kv_cache_context *cache_context)
{
    uint32_t hash_itr = 0;
    uint64_t key_cache_hash_val, key_cache_hash_val_init;
    nvm_kv_cache_entry *cache_entry = NULL;
    map<time_t, nvm_kv_cache_entry *,
        kv_cache_eviction_comparator> eviction_map;
    map<time_t, nvm_kv_cache_entry *,
        kv_cache_eviction_comparator>::iterator evict_itr;
    uint64_t cache_entry_index;

    //If this entry exists in the cache, replace the currently existing
    //lba in the cache with the new lba
    if (cache_context)
    {
        cache_entry = m_hash_table[cache_context->context_entry.index];

        if (cache_context->context_state == CACHE_ENTRY_FOUND)
        {
            //check again if the entry is still valid. This is done to ensure
            //that between the exists call and the put call, the entry could
            //have been invalidated. In that case, find a new cache slot for
            //this entry
            if (kv_cache_entry_compare(cache_entry, key, key_len, pool_id))
            {
                cache_entry->value_len = value_len;
                cache_entry->ts = time(NULL);
                goto end_kv_cache_put;
            }
        }
        else if (cache_context->context_state == CACHE_ENTRY_EMPTY)
        {
            //cache entry is empty as expected
            if (cache_entry->key_len == 0)
            {
                kv_cache_entry_set(cache_entry, key, key_len, pool_id,
                                   value_len, abs_expiry, gen_count, lba,
                                   time(NULL));
                goto end_kv_cache_put;
            }
        }
        else if (cache_context->context_state == CACHE_ENTRY_DELETE)
        {
            //check if the cache entry is the same as the one in the context.
            if (kv_cache_entry_compare(cache_entry,
                                       &cache_context->context_entry))
            {
                kv_cache_entry_set(cache_entry, key, key_len, pool_id,
                                   value_len, abs_expiry, gen_count, lba,
                                   time(NULL));
                goto end_kv_cache_put;
            }
        }
    }

    //If cache context is NULL or the previous cache context entry is no
    //longer valid, continue hash the key within the cache address space
    key_cache_hash_val = m_hash_func->key_hash((uint8_t *) key, key_len,
                                               pool_id, m_cache_addr_bits,
                                               false);

    key_cache_hash_val_init = key_cache_hash_val;

    //Index into the hash table and find an empty cache entry
    cache_entry = m_hash_table[key_cache_hash_val];

    while (hash_itr < M_COLLISION_LIMIT)
    {
        //There is already an entry present, look for an different slot
        if (cache_entry->key_len != 0)
        {
            if (kv_cache_entry_compare(cache_entry, key, key_len, pool_id))
            {
                break;
            }

            key_cache_hash_val =
                m_hash_func->resolve_coll((uint8_t *) key, key_len, pool_id,
                                          key_cache_hash_val_init, hash_itr,
                                          m_cache_addr_bits, false);

            //add the current cache entry into the eviction map
            eviction_map.insert(pair<time_t, nvm_kv_cache_entry *>
                                (cache_entry->ts, cache_entry));

            cache_entry = m_hash_table[key_cache_hash_val];

            hash_itr++;
        }
        else
        {
            //Found an empty entry
            break;
        }
    }

    //If no matching cache entry has been found
    if (hash_itr == M_COLLISION_LIMIT)
    {
        //pick the first item if one exists. In an ordered map, the first
        //entry that exists is the oldest one
        evict_itr = eviction_map.begin();

        if (evict_itr->second)
        {
            cache_entry = evict_itr->second;
            cache_entry_index = cache_entry->index;
            memset(cache_entry, 0, sizeof(nvm_kv_cache_entry));
            cache_entry->index = cache_entry_index;
        }
    }

    //Add the entry to the cache.
    kv_cache_entry_set(cache_entry, key, key_len, pool_id, value_len,
                       abs_expiry, gen_count, lba, time(NULL));

end_kv_cache_put:

    return NVM_SUCCESS;
}
//
//reads the key-lba mapping if it exists in the cache
//
int NVM_KV_Cache::kv_cache_get(nvm_kv_key_t *key,
                               uint32_t key_len,
                               int pool_id,
                               nvm_kv_cache_context *cache_context)
{
    uint32_t hash_itr = 0;
    uint32_t cur_time = 0;
    uint64_t key_cache_hash_val, key_cache_hash_val_init;
    nvm_kv_cache_entry *cache_entry = NULL;
    int ret_code = -NVM_ERR_OBJECT_NOT_FOUND;
    map<time_t, nvm_kv_cache_entry *,
        kv_cache_eviction_comparator> eviction_map;
    map<time_t, nvm_kv_cache_entry *,
        kv_cache_eviction_comparator>::iterator evict_itr;

    if (cache_context)
    {
        //context_state is initialized to CACHE_ENTRY_NOT_FOUND
        memset(cache_context, 0, sizeof(nvm_kv_cache_context));
    }

    //hash the key within the cache address space
    key_cache_hash_val =
        m_hash_func->key_hash((uint8_t *) key, key_len, pool_id,
                              m_cache_addr_bits, false);

    key_cache_hash_val_init = key_cache_hash_val;

    //Lock the hash table
    pthread_rwlock_rdlock(&m_cache_rwlock);

    //Index into the hash table and find the cache entry
    cache_entry = m_hash_table[key_cache_hash_val];

    while (hash_itr < M_COLLISION_LIMIT)
    {
        //if cache entry exists
        if (cache_entry->key_len != 0)
        {
            //Check if key exists in the cache
            if (kv_cache_entry_compare(cache_entry, key, key_len, pool_id))
            {
                //We have found a suitable candidate, populate the context
                if (cache_context)
                {
                    memcpy(&cache_context->context_entry, cache_entry,
                           sizeof(nvm_kv_cache_entry));
                    cache_context->context_state = CACHE_ENTRY_FOUND;

                    //Check if expiry is enabled
                    if (m_kv_store->get_expiry() != KV_DISABLE_EXPIRY)
                    {
                        cur_time = time(NULL);

                        if (cache_entry->expiry != 0 &&
                            (cur_time > cache_entry->expiry))
                        {
                            cache_context->expired = true;
                        }
                    }
                }

                ret_code = NVM_SUCCESS;

                goto end_cache_get;
            }

            key_cache_hash_val =
                m_hash_func->resolve_coll((uint8_t *) key, key_len, pool_id,
                                          key_cache_hash_val_init, hash_itr,
                                          m_cache_addr_bits, false);

            eviction_map.insert(pair<time_t, nvm_kv_cache_entry *>
                                (cache_entry->ts, cache_entry));
        }
        else
        {
            //if an empty entry is found, then remember that location
            //for cache entry addition. If we don't find the entry
            //after 8 attempts, then we can populate the location
            //as we are sure there's no cache entry exists for this key.
            if (cache_context &&
                cache_context->context_state == CACHE_ENTRY_NOT_FOUND)
            {
                cache_context->context_entry.index = key_cache_hash_val;
                cache_context->context_state = CACHE_ENTRY_EMPTY;
            }

        }

        hash_itr++;
        cache_entry = m_hash_table[key_cache_hash_val];
    }

    //If we have not found a candidate entry yet, which means we need to
    //evict an entry from the cache. The candidate for eviction will be
    //the oldest entry in the eviction map
    if (cache_context &&
        cache_context->context_state == CACHE_ENTRY_NOT_FOUND)
    {
        evict_itr = eviction_map.begin();

        //We have found a candidate entry, then populate it in the
        //context and return
        if (evict_itr->second)
        {
            cache_entry = evict_itr->second;

            if (cache_context)
            {
                memcpy(&cache_context->context_entry, cache_entry,
                        sizeof(nvm_kv_cache_entry));
                cache_context->context_state = CACHE_ENTRY_DELETE;
            }
        }
    }

end_cache_get:

    pthread_rwlock_unlock(&m_cache_rwlock);

    return ret_code;
}
//
//deletes key-lba mapping from cache
//
int NVM_KV_Cache::kv_cache_delete(nvm_kv_key_t *key,
                                  uint32_t key_len,
                                  int pool_id,
                                  nvm_kv_cache_entry &deleted_entry)
{
    uint32_t hash_itr = 0;
    uint64_t key_cache_hash_val, key_cache_hash_val_init;
    nvm_kv_cache_entry *cache_entry = NULL;
    int ret_code = NVM_SUCCESS;

    memset(&deleted_entry, 0, sizeof(nvm_kv_cache_entry));

    //hash the key within the cache address space
    key_cache_hash_val = m_hash_func->key_hash((uint8_t *) key, key_len,
                                                pool_id,
                                                m_cache_addr_bits, false);

    key_cache_hash_val_init = key_cache_hash_val;

    //Lock the hash table
    pthread_rwlock_wrlock(&m_cache_rwlock);

    //Index into the hash table and find the cache entry
    cache_entry = m_hash_table[key_cache_hash_val];

    while (hash_itr < M_COLLISION_LIMIT)
    {
        //if cache entry exists
        if (kv_cache_entry_compare(cache_entry, key, key_len, pool_id))
        {
            memcpy(&deleted_entry, cache_entry, sizeof(nvm_kv_cache_entry));
            memset(cache_entry, 0, sizeof(nvm_kv_cache_entry));
            cache_entry->index = deleted_entry.index;
            goto end_cache_delete;
        }

        key_cache_hash_val =
            m_hash_func->resolve_coll((uint8_t *) key, key_len, pool_id,
                                      key_cache_hash_val_init, hash_itr,
                                      m_cache_addr_bits, false);

        hash_itr++;
        cache_entry = m_hash_table[key_cache_hash_val];
    }

    if (hash_itr == M_COLLISION_LIMIT)
    {
        ret_code = -NVM_ERR_OBJECT_NOT_FOUND;
    }

end_cache_delete:

    pthread_rwlock_unlock(&m_cache_rwlock);
    return ret_code;
}
///
///delete the cache entries for the given list of lba
///
void NVM_KV_Cache::kv_cache_delete(const set<uint64_t>& lba_list)
{
    set<uint64_t>::iterator it;

    pthread_rwlock_wrlock(&m_cache_rwlock);
    for (it = lba_list.begin(); it!= lba_list.end(); it++)
    {
        for (int i = 0; i < m_total_entries; i++)
        {
            if (m_hash_table[i]->key_len != 0 &&
                m_hash_table[i]->lba == *it)
            {
                memset(m_hash_table[i], 0, sizeof(nvm_kv_cache_entry));
                m_hash_table[i]->index = i;
            }
        }
    }
    pthread_rwlock_unlock(&m_cache_rwlock);
}
///
///deletes key-lba mapping for the given list of pools from the cache
///
void NVM_KV_Cache::kv_cache_delete_pools(const set<uint32_t>& pool_ids)
{
    pthread_rwlock_wrlock(&m_cache_rwlock);
    for (int i = 0; i < m_total_entries; i++)
    {
        if (m_hash_table[i]->key_len != 0 &&
            pool_ids.find(m_hash_table[i]->pool_id) != pool_ids.end())
        {
            memset(m_hash_table[i], 0, sizeof(nvm_kv_cache_entry));
            m_hash_table[i]->index = i;
        }
    }
    pthread_rwlock_unlock(&m_cache_rwlock);
}
///
///deletes cache entries of all pools except for the default pool
///
void NVM_KV_Cache::kv_cache_delete_all_pools()
{
    uint32_t default_pool_id =
                m_kv_store->get_pool_mgr()->get_default_poolid();

    pthread_rwlock_wrlock(&m_cache_rwlock);

    for (int i = 0; i < m_total_entries; i++)
    {
        if (m_hash_table[i]->key_len != 0 &&
            m_hash_table[i]->pool_id != default_pool_id)
        {
            memset(m_hash_table[i], 0, sizeof(nvm_kv_cache_entry));
            m_hash_table[i]->index = i;
        }
    }

    pthread_rwlock_unlock(&m_cache_rwlock);
}
///
///delete all key-lba mapping entries from cache
///
void NVM_KV_Cache::kv_cache_delete_all()
{
    pthread_rwlock_wrlock(&m_cache_rwlock);

    for (int i = 0; i < m_total_entries; i++)
    {
        if (m_hash_table[i]->key_len != 0)
        {
            memset(m_hash_table[i], 0, sizeof(nvm_kv_cache_entry));
            m_hash_table[i]->index = i;
        }
    }

    pthread_rwlock_unlock(&m_cache_rwlock);
}
//
//get the size of the cache
//
uint64_t NVM_KV_Cache::kv_cache_get_size()
{
    return m_cache_size;
}
///
///compare the cache entry with the given key and pool id
///
bool NVM_KV_Cache::kv_cache_entry_compare(const nvm_kv_cache_entry *cache_entry,
                                          const nvm_kv_key_t *key,
                                          uint32_t key_len,
                                          uint32_t pool_id)
{
     if (cache_entry->key_len == key_len &&
         memcmp(cache_entry->key, key, key_len) == 0 &&
         cache_entry->pool_id == pool_id)
     {
         return true;
     }
     else
     {
         return false;
     }

}
///
///compare the two cache entries
///
bool NVM_KV_Cache::kv_cache_entry_compare(
                            const nvm_kv_cache_entry *cache_entry_one,
                            const nvm_kv_cache_entry *cache_entry_two)
{
     if (cache_entry_one->key_len == cache_entry_two->key_len &&
         memcmp(cache_entry_one->key, cache_entry_two->key,
                cache_entry_one->key_len) == 0 &&
         cache_entry_one->pool_id == cache_entry_two->pool_id)
     {
         return true;
     }
     else
     {
         return false;
     }

}
///
///populate the cache entry with the given content, except for the
///entry index
///
void NVM_KV_Cache::kv_cache_entry_set(nvm_kv_cache_entry *cache_entry,
                                      const nvm_kv_key_t *key,
                                      const uint32_t key_len,
                                      const uint32_t pool_id,
                                      const uint32_t value_len,
                                      const uint32_t abs_expiry,
                                      const uint32_t gen_count,
                                      const uint64_t lba,
                                      const time_t ts)
{
    memcpy(cache_entry->key, key, key_len);
    cache_entry->key_len = key_len;
    cache_entry->pool_id = pool_id;
    cache_entry->value_len = value_len;
    cache_entry->expiry = abs_expiry;
    cache_entry->gen_count = gen_count;
    cache_entry->lba = lba;
    cache_entry->ts = ts;
}
