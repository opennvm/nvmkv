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
/// \file nvm_kv.h
/// \brief  NVMKV Key-Value Store Header File.
#ifndef NVM_KV_H_
#define NVM_KV_H_

#include <pthread.h>
#include <stdint.h>

#define NVM_KV_MAX_KEY_SIZE   128        ///< max key size in bytes
#define NVM_KV_MAX_VALUE_SIZE 1047552    ///< max value size in bytes (1MiB - 1KiB)
#define NVM_KV_MAX_POOLS      1048576    ///< currently supported max pools
#define NVM_KV_MIN_POOLS      1          ///< mininum number of pools in the store
#define NVM_KV_MAX_ITERATORS  128        ///< max iterators supported over all pools
#ifdef __cplusplus
extern "C"
{
#endif
    ///
    ///key type
    ///
    typedef uint8_t nvm_kv_key_t;
    ///
    ///pool tag associated with each pool
    ///
    typedef struct
    {
        uint8_t pool_tag[16];///< 16 bytes pool tag
    } nvm_kv_pool_tag_t;
    ///
    ///key info associated with each key
    ///
    typedef struct
    {
        uint32_t pool_id;  ///< pool id to which key belongs to
        uint32_t key_len;  ///< length of key in bytes
        uint32_t value_len;///< length of values in bytes
        uint32_t expiry;   ///< expiry in seconds
        uint32_t gen_count;///< generation count
        uint32_t reserved1;///< reserved
    } nvm_kv_key_info_t;
    ///
    ///structure used for batch operation, holds io vectors
    ///
    typedef struct
    {
        uint32_t key_len;  ///< length of the key in bytes
        uint32_t value_len;///< length of value in bytes
        uint32_t expiry;   ///< expiry in seconds
        uint32_t gen_count;///< generation count
        uint32_t replace;  ///< if set, replace the value
        uint32_t reserved1;///< reserved
        nvm_kv_key_t *key; ///< key, array of bytes
        void     *value;   ///< value, array of bytes
    } nvm_kv_iovec_t;
    ///
    ///an enum with mutually exclusive possible states of the pool
    ///
    typedef enum
    {
        POOL_NOT_IN_USE = 0,          ///< pool is not in use
        POOL_IN_USE = 1,              ///< pool is in use
        POOL_DELETION_IN_PROGRESS = 2,///< pool is currently being deleted
        POOL_IS_INVALID = 3           ///< pool is invalid
    } nvm_kv_pool_status_t;

    ///
    ///information about a specific KV Store
    ///
    typedef struct
    {
        uint32_t version;       ///< version of the KV store
        uint32_t num_pools;     ///< number of pools currently created on KV store
        uint32_t max_pools;     ///< maximum number of pools
        uint32_t expiry_mode;   ///< KV store expiry mode nvm_kv_expiry_t enum value
        uint32_t global_expiry; ///< global expiry value for KV store
        uint32_t reserved1;     ///< reserved
        uint64_t cache_size;    ///< cache size in bytes
        uint64_t num_keys;      ///< total number of keys in KV store
        uint64_t free_space;    ///< available free space in bytes
    } nvm_kv_store_info_t;
    ///
    ///information about a pool specified pool
    ///
    typedef struct
    {
        uint32_t version;    ///< version of the pools, currently set to 0
        uint32_t pool_status;///< status of the pool
    } nvm_kv_pool_info_t;
    ///
    ///
    ///
    typedef struct
    {
        uint32_t pool_id;          ///< pool id
        nvm_kv_pool_tag_t pool_tag;///< pool tag associated with pool_id
    } nvm_kv_pool_metadata_t;
    ///
    ///enum used to check the type of expiry
    ///
    typedef enum
    {
        KV_DISABLE_EXPIRY   = 0,///< expiry support disabled by default
        KV_ARBITRARY_EXPIRY = 1,///< arbitrary expiry support
        KV_GLOBAL_EXPIRY    = 2 ///< global expiry support
    } nvm_kv_expiry_t;
    ///
    ///initializes KV store, updates KV store specific metadata if required
    ///at 0th LBA. This api needs to be called before using any other kvstore
    ///apis
    ///
    ///@param[in] id         KV store id
    ///@param[in] version    version of the KV store API
    ///@param[in] max_pools  maximum number of pools that can be created
    ///                      with in KV store, this cannot be changed once
    ///                      KV store is created
    ///@param[in] expiry     expiry support. Expected values:
    ///                      KV_DISABLE_EXPIRY(0):   Disable the expiry
    ///                      KV_ARBITRARY_EXPIRY(1): Enable arbitraty expiry
    ///                      KV_GLOBAL_EXPIRY(2):    Enable global expiry
    ///@param[in] cache_size amount of memory (in bytes) that needs to be
    ///                      allocated for the cache
    ///@return               KV store id on success or -1 on failure,
    ///                      appropriate error code is set on error
    ///
    int nvm_kv_open(int id, uint32_t version, uint32_t max_pools,
                    uint32_t expiry, uint64_t cache_size);
    ///
    ///create a new pool in the KV store
    ///
    ///@param[in] kv_id    KV store id
    ///@param[in] pool_tag tag associated with each pool, pool can be
    ///                    identified either by pool_id or pool_tag
    ///@return             return pool_id on success or -1 on failure
    ///                    appropriate error code is set on error
    ///
    int nvm_kv_pool_create(int kv_id, nvm_kv_pool_tag_t *pool_tag);
    ///
    ///delete specified pool from the KV store
    ///
    ///@param[in] kv_id      KV store id
    ///@param[in] pool_id    pool id, if set to 0 operation is done on default
    ///                      pool, if set to -1, delete all pools except the
    ///                      default pool
    ///@return               return 0 on success or -1 on failure,
    ///                      appropriate error code is set on error
    ///
    int nvm_kv_pool_delete(int kv_id, int pool_id);
    ///
    ///Deletes all the keys in the given pool
    ///
    ///@param[in] kv_id  KV store id
    ///@return           returns 0 on success and -1 on failure. Error number
    ///                  is set on failure
    ///
    int nvm_kv_delete_all(int kv_id);
    ///
    ///clean up in-memory data structures related to the KV store.
    ///KV store data on the drive remains intact.
    ///
    ///@param[in] kv_id  KV store id
    ///@return           returns 0 on success and -1 on failure. Error number
    ///                  is set on failure
    ///
    int nvm_kv_close(int kv_id);
    ///
    ///stores key value pair on the KV store
    ///
    ///@param[in] kv_id      KV store id
    ///@param[in] pool_id    pool id, if set to 0 operation is done on
    ///                      default pool
    ///@param[in] key        unique key which is byte array
    ///@param[in] key_len    size of the key in number of bytes
    ///@param[in] value      user data corresponding to key that needs to
    ///                      be stored
    ///@param[in] value_len  actual size of the user data in number of bytes
    ///@param[in] expiry     expiry in seconds which specifies expiration
    ///                      time for the key/value pair.
    ///@param[in] replace    if set to true, if same key/value pair already
    ///                      exists,replace it
    ///@param[in] gen_count  generation count
    ///@return               returns number of bytes written on success,
    ///                      -1 on error and appropriate error code is set
    ///
    int nvm_kv_put(int kv_id, int pool_id, nvm_kv_key_t *key, uint32_t key_len,
                   void *value, uint32_t value_len, uint32_t expiry,
                   bool replace, uint32_t gen_count);
    ///
    ///retrieves value associated with the key from KV store
    ///
    ///@param[in]  kv_id      KV store id
    ///@param[in]  pool_id    pool id, if set to 0 operation is done on
    ///                       default pool
    ///@param[in]  key        unique key which is byte array
    ///@param[in]  key_len    size of the key in number of bytes
    ///@param[out] value      user data corresponding to key that needs to be
    ///                       stored
    ///@param[in]  value_len  actual size of the user data in number of bytes
    ///@param[in]  read_exact if set to true read exact bytes that is written
    ///                       on media if value_len is greater than what is
    ///                       written on media, if set to false read value_len
    ///                       bytes
    ///@param[out] key_info   key info of the key/value pair
    ///@return                returns number of bytes deleted on success,
    ///                       -1 on error, appropriate errno is set on error
    ///
    int nvm_kv_get(int kv_id, int pool_id, nvm_kv_key_t *key, uint32_t key_len,
                   void *value, uint32_t value_len, bool read_exact,
                   nvm_kv_key_info_t *key_info);
   ///
   ///deletes key value pair on the KV store
   ///
   ///@param[in]   kv_id     KV store id
   ///@param[in]   pool_id   pool id, if set to 0 operation is done on
   ///                       default pool
   ///@param[in]   key       unique key which is byte array
   ///@param[in]   key_len   size of the key in number of bytes
   ///@return                returns 0 on success, -1 on error
   ///                       appropriate error code is set on error
   ///
    int nvm_kv_delete(int kv_id, int pool_id, nvm_kv_key_t *key,
                      uint32_t key_len);
    ///
    ///sets the iterator to the beginning of a given pool
    ///
    ///@param[in] kv_id    KV store id
    ///@param[in] pool_id  pool id, if set to 0 operation is done on default
    ///                    pool
    ///@return             returns iterator id, a unique id for the created
    ///                    iterator
    ///
    int nvm_kv_begin(int kv_id, int pool_id);
    ///
    ///sets the iterator specified to the next location
    ///in a given pool.
    ///
    ///@param[in] kv_id        KV store id
    ///@param[in] iterator_id  unique id for the current iterator
    ///@return                 0 for next key found, -1 otherwise.
    ///
    int nvm_kv_next(int kv_id, int iterator_id);
    ///
    ///retrieves the key/value pair at current iterator location
    ///in pool.
    ///
    ///@param[in]  kv_id            KV store id
    ///@param[in]  iterator_id      unique id for the current iterator
    ///@param[out] key              unique key, a byte array
    ///@param[out] key_len          size of the key in bytes
    ///@param[out] value            user data corresponding to key
    ///@param[out] value_len        size of user data in bytes
    ///@param[out] key_info         key info of the key/value pair
    ///@return                      writes key to key pointer and same for
    ///                             value. key_info is written as well.
    ///                             0 for success and -1 for failure.
    ///
    int nvm_kv_get_current(int kv_id, int iterator_id, nvm_kv_key_t *key,
                           uint32_t *key_len, void *value,
                           uint32_t value_len, nvm_kv_key_info_t *key_info);
    ///
    ///ends iteration and releases the iterator id to free pool
    ///
    ///@param[in]  id         KV store id
    ///@param[in]  it_id      unique id for the current iterator
    ///@return                returns 0 on success, on error appropriate errno
    ///                       is set and -1 is returned
    ///
    int nvm_kv_iteration_end(int id, int it_id);
    ///
    ///checks if key value pair already exists on the KV store
    ///
    ///@param[in]   kv_id     KV store id
    ///@param[in]   pool_id   pool id, if set to 0 operation is done on default
    ///                       pool
    ///@param[in]   key       unique key which is byte array
    ///@param[in]   key_len   size of the key in number of bytes
    ///@param[out]  key_info  key info of the key/value pair
    ///@return                returns 1 if key already exists else returns 0.
    ///                       In case of any error, -1 is returned and
    ///                       appropriate errno is set.
    ///
    int nvm_kv_exists(int kv_id, int pool_id, nvm_kv_key_t *key,
                      uint32_t key_len, nvm_kv_key_info_t *key_info);
    ///
    ///returns the approximate value length of the key
    ///
    ///@param[in]   id        KV store id
    ///@param[in]   pool_id   pool id, if set to 0 operation is done on default
    ///                       pool
    ///@param[in]   key       unique key which is byte array
    ///@param[in]   key_len   size of the key in number of bytes
    ///@return                returns approximate value length on success
    ///                       and -1 on error with errno set
    ///
    int nvm_kv_get_val_len(int id, int pool_id, nvm_kv_key_t *key,
                           uint32_t key_len);
    ///
    ///gets key/value pairs in one batch operation
    ///
    ///@param[in]     kv_id     KV store id
    ///@param[in]     pool_id   pool id, if set to 0 operation is done on
    ///                         default pool
    ///@param[in,out] kv_iov    vector of key value pair
    ///@param[in]     iov_count length of vector
    ///@return                  returns 0 on success or (-1) on
    ///                         error and appropriate errno is set
    ///
    int nvm_kv_batch_get(int kv_id, int pool_id, nvm_kv_iovec_t *kv_iov,
                         uint32_t iov_count);
    ///
    ///puts key/value pairs in one batch operation
    ///
    ///@param[in] kv_id     KV store id
    ///@param[in] pool_id   pool id, if set to 0 operation is done on
    ///                     deafult pool
    ///@param[in] kv_iov    vector of key value pair
    ///@param[in] iov_count length of vector
    ///@return              returns 0 on success or (-1) on
    ///                     error and appropriate errno is set
    ///
    int nvm_kv_batch_put(int kv_id, int pool_id, nvm_kv_iovec_t *kv_iov,
                         uint32_t iov_count);
    ///
    ///get info about a particular pool
    ///
    ///@param[in]  kv_id      KV store id
    ///@param[in]  pool_id    pool id, if set to 0 operation is done on default
    ///                       pool
    ///@param[out] pool_info  information of the pool in question.
    ///@return                0 on success, -1 on error and appropriate errno
    ///                       is set
    ///
    int nvm_kv_get_pool_info(int kv_id, int pool_id,
                             nvm_kv_pool_info_t *pool_info);
    ///
    ///retrieves metadata information about a KV store
    ///
    ///@param[in]  kv_id       KV store id
    ///@param[out] store_info  structure used to store information about KV
    ///                        store
    ///@return                 returns 0 on success, -1 on error and
    ///                        appropriate error code is set on error
    ///
    int nvm_kv_get_store_info(int kv_id, nvm_kv_store_info_t *store_info);
    ///
    ///retrieves metadata information of a key
    ///
    ///@param[in]  kv_id    KV store id
    ///@param[in]  pool_id  pool id, if set to 0 operation is done on
    ///                     default pool
    ///@param[in]  key      unique key which is byte array
    ///@param[in]  key_len  size of the key in number of bytes
    ///@param[out] key_info structure used to store information about a
    ///                     specific key
    ///@return              returns 0 on success, -1 on error. Appropriate
    ///                     error code is set on error
    ///
    int nvm_kv_get_key_info(int kv_id, int pool_id, nvm_kv_key_t *key,
                            uint32_t key_len, nvm_kv_key_info_t *key_info);
    ///
    ///set the global expiry value of KV store
    ///
    ///@param[in] id      KV store id
    ///@param[in] expiry  global expiry value
    ///
    ///@return            0 on success, -1 on error with appropriate
    ///                   errno set
    ///
    int nvm_kv_set_global_expiry(int id, uint32_t expiry);
    ///
    ///fetches pool_id and associated tag iteratively for all the pools
    ///in a NVMKV store
    ///
    ///@param[in] kv_id       KV store id
    ///@param[in] pool_md     address of contiguous memory of count elements
    ///                       of type nvm_kv_pool_metadata_t allocated by
    ///                       caller
    ///@param[in] count       number of nvm_kv_pool_metadata_t that needs to be
    ///                       read
    ///@param[in] start_count indicates from which pool_id the call should
    ///                       start reading
    ///
    ///@return                successful completion of this call returns total
    ///                       count returned which should be used as
    ///                       start_count in subsequent call to
    ///                       nvm_kv_get_pool_metadata(). The return value,
    ///                       lesser than count, indicates the completion of
    ///                       iteration.
    ///
    int nvm_kv_get_pool_metadata(int kv_id, nvm_kv_pool_metadata_t *pool_md,
                                 uint32_t count, uint32_t start_count);
#ifdef __cplusplus
}
#endif //__cplusplus
#endif //NVM_KV_H_
