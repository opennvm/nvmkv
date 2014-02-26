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
#ifndef KV_CACHE_H_
#define KV_CACHE_H_

#include <set>
#include "include/kv_macro.h"
#include "include/nvm_kv.h"
#include "util/kv_hash_func.h"
#include "src/kv_common.h"

using namespace std;

class NVM_KV_Store;

///
///This class holds implementation of collision cache, certain put workload
///makes two media calls, one for reading the key and the other for overwriting
///key if already exist. To avoid two media calls, collision cache can be used
///which stores the key-lba mapping in memory
///
class NVM_KV_Cache
{
    public:
        NVM_KV_Cache(NVM_KV_Store *kv_store);
        ///
        ///initializes the collision cache based on the cache_size
        ///
        ///@param[in] cache_size amount of memory (in bytes) used for
        ///                      collision cache
        ///
        ///@return  return success or appropriate error code
        ///
        int initialize(uint64_t cache_size);
        ///
        ///destroys instance of NVM_KV_Hash_Func
        ///
        ~NVM_KV_Cache();
        ///
        ///writes key-lba mapping into cache
        ///
        ///@param[in] key           key that needs to be written into cache
        ///@param[in] key_len       length of the key
        ///@param[in] pool_id       Id of the pool to which the key belongs
        ///@param[in] value_len     length of the value
        ///@param[in] abs_expiry    absolute expiry time
        ///@param[in] gen_count     generation count
        ///@param[in] lba           LBA associated with key
        ///@param[in] cache_context pointer to the cache context
        ///
        ///@return                  NVM_SUCCESS on success
        ///                         appropriate error code on failure
        ///
        int kv_cache_put(nvm_kv_key_t *key, uint32_t key_len,
                         int pool_id, uint32_t value_len,
                         uint32_t abs_expiry, uint32_t gen_count,
                         uint64_t lba,
                         nvm_kv_cache_context *cache_context);
        ///
        ///reads the key-lba mapping if its exists in cache
        ///
        ///@param[in]  key           key that needs to be checked
        ///@param[in]  key_len       length of the key
        ///@param[in]  pool_id       Id of the pool to which the key belongs
        ///@param[out] cache_context pointer to the cache context
        ///
        ///@return                   NVM_SUCCESS on success
        ///                          appropriate error code on failure
        ///
        int kv_cache_get(nvm_kv_key_t *key, uint32_t key_len,
                         int pool_id, nvm_kv_cache_context *cache_context);
        ///
        ///deletes key-lba mapping from cache
        ///
        ///@param[in]     key           key that needs to be deleted from cache
        ///@param[in]     key_len       length of the key
        ///@param[in]     pool_id       Id of the pool to which the key belongs
        ///@param[in,out] deleted_entry the cache entry gets deleted
        ///
        ///@return            NVM_SUCCESS on success or appropriate error
        ///                   code on failure
        ///
        int kv_cache_delete(nvm_kv_key_t *key, uint32_t key_len, int pool_id,
                            nvm_kv_cache_entry &deleted_entry);
        ///
        ///delete the cache entries for the given list of lba
        ///
        ///@param[in] lba_list lba list to be deleted from the cache
        ///
        void kv_cache_delete(const set<uint64_t>& lba_list);
        ///
        ///deletes key-lba mapping for the given list of pools from the cache
        ///
        ///@param[in] list of pools to be deleted from the cache
        ///
        void kv_cache_delete_pools(const set<uint32_t>& pool_ids);
        ///
        ///deletes cache entries of all pools except for the default pool
        ///
        void kv_cache_delete_all_pools();
        ///
        ///delete all key-lba mapping entries from cache
        ///
        void kv_cache_delete_all();
        ///
        ///acquire a write lock on the cache
        ///
        void kv_cache_acquire_wrlock();
        ///
        ///release the lock on the cache
        ///
        void kv_cache_release_lock();
        ///
        ///A comparator function to maintain an eviction map where the map is used in the
        ///ascending order of timestamps
        ///
        typedef struct kv_cache_eviction_comparator
        {
            bool operator () (time_t a, time_t b)
            {
                double diff = difftime(b, a);

                if (diff > 0)
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }
        } kv_cache_eviction_comparator;
        ///
        ///get the size of the cache
        ///
        uint64_t kv_cache_get_size();

    private:
        ///
        ///compare the cache entry with the given key and pool id
        ///
        ///@param[in] entry   pointer to the cache entry
        ///@param[in] key     pointer to the key to be compared
        ///@param[in] key_len length of the key
        ///@param[in] pool_id id of the pool to which the key belongs
        ///
        ///@return            true, if the entry matches the key and pool id
        ///                   false, otherwise
        ///
        static bool kv_cache_entry_compare(const nvm_kv_cache_entry *entry,
                                           const nvm_kv_key_t *key,
                                           uint32_t key_len,
                                           uint32_t pool_id);
        ///
        ///compare the two cache entries
        ///
        ///@param[in] entry_one   pointer to the first cache entry
        ///@param[in] entry_two   pointer to the second cache entry
        ///
        ///@return            true, if the two entry matches
        ///                   false, otherwise
        ///
        static bool kv_cache_entry_compare(
                                    const nvm_kv_cache_entry *entry_one,
                                    const nvm_kv_cache_entry *entry_two);
        ///
        ///populate the cache entry with the given content, except for the
        ///entry index
        ///
        ///@param[in] entry      pointer to the cache entry
        ///@param[in] key        pointer to the key to be compared
        ///@param[in] key_len    length of the key
        ///@param[in] pool_id    id of the pool to which the key belongs
        ///@param[in] value_len  length of the value
        ///@param[in] abs_expiry absolute expiry time
        ///@param[in] gen_count  generation count
        ///@param[in] lba        lba of the key/value pair on the media
        ///@param[in] ts         timestamp of storage of the entry in the cache
        ///
        static void kv_cache_entry_set(
                                    nvm_kv_cache_entry *entry,
                                    const nvm_kv_key_t *key,
                                    const uint32_t key_len,
                                    const uint32_t pool_id,
                                    const uint32_t value_len,
                                    const uint32_t abs_expiry,
                                    const uint32_t gen_count,
                                    const uint64_t lba,
                                    const time_t ts);

        static const uint64_t M_COLLISION_LIMIT = 8;///< number of times collision needs to be resolved

        //disable copy constructor and assignment operator
        DISALLOW_COPY_AND_ASSIGN(NVM_KV_Cache);

        NVM_KV_Store *m_kv_store;                   ///< KV store object
        pthread_rwlock_t m_cache_rwlock;            ///< reader-writer lock for thread safety
        uint64_t  m_cache_size;                     ///< amount of memory (in bytes) for cache
        uint64_t  m_total_entries;                  ///< total number of cache entries
        uint32_t  m_cache_addr_bits;                ///< number of bits that represent the cache size
        NVM_KV_Hash_Func *m_hash_func;              ///< hash functions used for collision cache
        nvm_kv_cache_entry **m_hash_table;          ///< hash table of cache entries
};

#endif //KV_CACHE_H_
