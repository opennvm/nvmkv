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
#ifndef KV_STORE_MGR_H_
#define KV_STORE_MGR_H_
#include "nvm_error.h"
#include "nvm_kv.h"
#include "util/kv_hash_func.h"
#include "util/kv_buffer_pool.h"
#include "src/kv_common.h"
#include "src/kv_store.h"
#include "src/kv_layout.h"
#include <nvm_primitives.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <set>
#include <errno.h>
using namespace std;
///
///singleton class that maintains KV store APIs.
///APIs supported kv_open, kv_pool_create, kv_pool_delete, kv_put, kv_get,
///kv_delete, kv_exists, kv_begin, kv_next, kv_get_current, kv_batch_put,
///kv_get_key_info, kv_get_store_info, kv_get_pool_info, kv_get_val_len,
///kv_delete_all
///
class NVM_KV_Store_Mgr
{
    public:
        ///
        ///creates instance of the class
        ///
        ///@param[in] sync  if set to true synchronizes creation of
        ///                 NVM_KV_Store_Mgr object
        ///
        ///@return          NVM_KV_Store_Mgr object
        ///
        static NVM_KV_Store_Mgr* instance(bool sync);
        ///
        ///creates instance of the class
        ///
        ///
        ///@return          NVM_KV_Store_Mgr object
        ///
        static NVM_KV_Store_Mgr* instance();
        ///
        ///initializes KV store, updates store specific metadata if required
        ///at 0th LBA
        ///
        ///@param[in] id        vsu id/file descriptor for the KV store
        ///@param[in] version   version of the KV store API
        ///@param[in] max_pools maximum number of pools that can be created
        ///                     within KV store, this cannot be changed once
        ///                     store is created
        ///@param[in] expiry    expiry support. Expected values:
        ///                     KV_DISABLE_EXPIRY(0):   Disable the expiry
        ///                     KV_ARBITRARY_EXPIRY(1): Enable arbitraty expiry
        ///                     KV_GLOBAL_EXPIRY(2):    Enable global expiry
        ///@return              KV store id on success or -1 on failure,
        ///                     appropriate error code is set on error
        ///
        int kv_open(int id, uint32_t version, uint32_t max_pools,
                    uint32_t expiry);
        ///
        ///create a new pool with in KV store
        ///
        ///@param[in] kv_id    KV store id
        ///@param[in] pool_tag tag assosiated with each pool, pool can be
        ///                    identified either by pool_id or pool_tag
        ///@return             return pool_id on success or -1 on failure
        ///                    appropriate error code is set on error
        ///
        int kv_pool_create(int kv_id, nvm_kv_pool_tag_t *pool_tag);
        ///
        ///delete pool with in KV store
        ///
        ///@param[in] kv_id      KV store id
        ///@param[in] pool_id    pool id, if set to 0 operation is done on
        ///                      default pool, if set to -1, delete all pools
        ///                      except the default pool
        ///@return               return 0 on success or -1 on failure,
        ///                      appropriate error code is set on error
        ///
        int kv_pool_delete(int kv_id, int pool_id);
        ///
        ///stores key value pair on the KV store
        ///
        ///@param[in] id         KV store id
        ///@param[in] pool_id    pool id, if set to 0 operation is done on
        ///                      default pool
        ///@param[in] key        unique key which is byte array
        ///@param[in] key_len    size of the key in number of bytes
        ///@param[in] value      user data corresponding to key that needs to
        ///                      be stored
        ///@param[in] value_len  actual size of the user data in number of
        ///                      bytes
        ///@param[in] expiry     expiry in seconds which specifies expiration
        ///                      time for the KV pair.
        ///@param[in] replace    if set to true, if same KV pair already
        ///                      exists,replace it
        ///@param[in] gen_count  generation count
        ///@return               returns number of bytes written on success,
        ///                      -1 on error and appropriate error code is set
        ///
        int kv_put(int id, int pool_id, nvm_kv_key_t *key, uint32_t key_len,
                   void *value, uint32_t value_len, uint32_t expiry,
                   bool replace, uint32_t gen_count);
        ///
        ///retireves metadata information about a KV store
        ///
        ///@param[in]  kv_id      KV store id
        ///@param[out] store_info structure used to store information about
        ///                       KV Store
        ///@return                returns 0 on success, -1 on error.
        ///                       appropriate error code is set on error
        ///
        int kv_get_store_info(int kv_id, nvm_kv_store_info_t *store_info);
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
        int kv_get_key_info(int kv_id, int pool_id, nvm_kv_key_t *key,
                            uint32_t key_len, nvm_kv_key_info_t *key_info);
        ///
        ///deletes all the keys from all the pools (including
        ///the default pool) in the given KV store.
        ///
        ///@param[in] id   KV store id
        ///@return         returns 0 on success, -1 on error. Appropriate
        ///                error code is set on error
        ///
        int kv_delete_all(int id);
        ///
        ///clean up in-memory data structures related to the KV store.
        ///KV store data on the drive remains intact.
        ///
        ///@param[in] kv_id  KV store id
        ///@return           returns 0 on success and -1 on failure. Error
        ///                  number is set on failure
        ///
        int kv_close(int id);
        ///
        ///retrieves value associated with the key from KV store
        ///
        ///@param[in]  kv_id      KV store id
        ///@param[in]  pool_id    pool id, if set to 0 operation is done on
        ///                       default pool
        ///@param[in]  key        unique key which is byte array
        ///@param[in]  key_len    size of the key in number of bytes
        ///@param[out] value      user data corresponding to key that needs to
        ///                       be stored
        ///@param[in]  value_len  actual size of the user data in number of
        ///                       bytes
        ///@param[in]  read_exact if set to true read exact bytes that is
        ///                       written on media if value_len is greater
        ///                       than what is written on media, if set to
        ///                       false read value_len bytes
        ///@param[out] key_info   key info of the kv pair
        ///@return                returns number of bytes deleted on success,
        ///                       -1 on error, appropriate errno is set on error
        ///
        int kv_get(int id, int pool_id, nvm_kv_key_t *key, uint32_t key_len,
                   void *value, uint32_t value_len, bool read_exact,
                   nvm_kv_key_info_t *key_info);
        ///
        ///deletes key value pair on the KV store
        ///
        ///@param[in]   id        KV store id
        ///@param[in]   pool_id   pool id, if set to 0 operation is done on
        ///                       default pool
        ///@param[in]   key       unique key which is byte array
        ///@param[in]   key_len   size of the key in number of bytes
        ///@return                returns 0 on success, -1 on error
        ///                       appropriate error code is set on error
        ///
        int kv_delete(int id, int pool_id, nvm_kv_key_t *key,
                      uint32_t key_len);
        ///
        ///puts all kv pair in one batch operation
        ///
        ///@param[in] id        KV store id
        ///@param[in] pool_id   pool id, if set to 0 operation is done on
        ///                     default pool
        ///@param[in] kv_iov    vector of key value pair
        ///@param[in] iov_count number of IOVs in the vector
        ///@return              returns 0 on success or (-1) on
        ///                     error and appropriate errno is set
        ///
        int kv_batch_put(int id, int pool_id, nvm_kv_iovec_t *kv_iov,
                         uint32_t iov_count);
        ///
        ///checks if key value pair already exists on the KV store
        ///
        ///@param[in]  id        KV store id
        ///@param[in]  pool_id   pool id, if set to 0 operation is done on
        ///                      default pool
        ///@param[in]  key       unique key which is byte array
        ///@param[in]  key_len   size of the key in number of bytes
        ///@param[out] key_info  key info of the KV pair
        ///@return               returns 1 if key already exists else returns
        ///                      0. In case of any error, -1 is returned and
        ///                      appropriate errno is set.
        ///
        int kv_exists(int id, int pool_id, nvm_kv_key_t *key, uint32_t key_len,
                      nvm_kv_key_info_t *key_info);
        ///
        ///gets the approximate value length of a key
        ///
        ///@param[in] id        unique id for the KV store
        ///@param[in] pool_id   pool id, if set to 0 operation is done on
        ///                     default pool
        ///@param[in] key       key for which value length is needed
        ///@param[in] key_len   length of the key
        ///@return              returns the approximate value length needed
        ///                     for the key in success. Returns -1 in case of
        ///                     failure with errno set
        ///
        int kv_get_val_len(int id, int pool_id, nvm_kv_key_t *key,
                           uint32_t key_len);
        ///
        ///sets the iterator to the beginning of a given pool
        ///
        ///@param[in] id        unique id for the KV store
        ///@param[in] pool_id   pool id, if set to 0 operation is done on
        ///                     default pool
        //@return               returns iterator id, a unique id for the
        ///                     created iterator
        ///
        int kv_begin(int id, int pool_id);
        ///
        ///sets the iterator to the next location on the logical tree
        ///
        ///@param[in] kv_id    unique id for the KV store
        ///@param[in] pool_id  pool id, if set to 0 operation is done on
        ///                    default pool
        ///@return             returns 0 on success, returns -1 on error and
        ///                    appropriate error code is set
        ///
        int kv_next(int id, int iterator_id);
        ///
        ///retrieves the KV pair at current iterator location
        ///in pool.
        ///
        ///@param[in]  id         unique id for the KV store
        ///@param[in]  it_id      unique id for the current iterator
        ///@param[out] key        unique key, a byte array
        ///@param[out] key_len    size of the key in bytes
        ///@param[out] value      user data corresponding to key
        ///@param[out] value_len  size of user data in bytes
        ///@param[out] key_info   key info of the KV pair
        ///@return                writes key to key pointer and same for
        ///                       value. key_info is written as well.
        ///                       0 for success and -1 for failure.
        ///
        int kv_get_current (int id, int it_id, nvm_kv_key_t *key,
                            uint32_t *key_len, void *value, uint32_t value_len,
                            nvm_kv_key_info_t *key_info);
        ///
        ///ends iteration and releases the iterator id to free pool
        ///
        ///@param[in]  id         unique id for the KV store
        ///@param[in]  it_id      unique id for the current iterator
        ///@return                returns 0 on success, on error appropriate
        ///                       errno is set and -1 is returned
        ///
        int kv_iteration_end(int id, int it_id);
        ///
        ///fetch information about the pool in question.
        ///
        ///@param[in] id         KV store id
        ///@param[in] pool_id    pool id, if set to 0 operation is done on
        ///                      default pool
        ///@param[out] pool_info struct used to store information about pools
        ///@return               0 on success, -1 on error and appropriate
        ///                      errno is set
        ///
        int kv_get_pool_info(int id, int pool_id,
                             nvm_kv_pool_info_t *pool_info);
        ///
        ///set the global expiry value of KV store
        ///
        ///@param[in] id      KV store id
        ///@param[in] expiry  global expiry value
        ///
        ///@return            0 on success, -1 on error with appropriate
        ///                   errno set
        ///
        int kv_set_global_expiry(int id, uint32_t expiry);
        ///
        ///fetches pool_id and associated tag iteratively for all the pools
        ///in a KV store
        ///
        ///@param[in] kv_id       KV store id
        ///@param[in] pool_md     address of contiguous memory of count elements
        ///                       of type nvm_kv_pool_metadata_t allocated by
        ///                       caller
        ///@param[in] count       number of nvm_kv_pool_metadata_t that needs
        ///                       to be read
        ///@param[in] start_count indicates from which pool_id the call should
        ///                       start reading
        ///
        ///@return                successful completion of this call returns
        ///                       total count returned which should be used as
        ///                       start_count in subsequent call to
        ///                       nvm_kv_get_pool_metadata(). The return value,
        ///                       lesser than count, indicates the completion
        ///                       of iteration.
        ///
        int kv_get_pool_metadata(int kv_id, nvm_kv_pool_metadata_t *pool_md,
                                 uint32_t count, uint32_t start_count);

    private:
        static pthread_mutex_t m_mtx;                 ///< mutex used for synchronizing creation of this class
        static const bool M_DEBUG_FLAG = false;       ///< flag for debugging
        static NVM_KV_Store_Mgr *m_pInstance;         ///< instance of KV store
        static const uint32_t M_MAX_COLLISION = 8;    ///< maximum number of
                                                      ///< times collision is
                                                      ///< resolved
        static const uint32_t M_KV_HEADER_SIZE = sizeof(nvm_kv_header_t);///<KV
                                                      ///< header size
        //disbale copy constructor and assignment operator
        DISALLOW_COPY_AND_ASSIGN(NVM_KV_Store_Mgr);
        ///
        ///Initializes mutex
        ///
        NVM_KV_Store_Mgr();
        ///
        ///destroys mutex and clears m_kvMap
        ///
        ~ NVM_KV_Store_Mgr();
        ///
        ///kvstore manager initialization
        ///
        ///@return  returns 0 on success or appropriate error
        ///
        int initialize();
        ///
        ///validation of parameters passed into APIs like kvGet, kvPut is done.
        ///num_arg, device_info and fd are mandatory required arguments that
        ///needs to be passed.
        ///
        ///@param[in]     flag      flag to check which input param needs to be
        ///                         validated
        ///@param[in]     id        KV store id
        ///@param[in,out] kv_store  device info related to KV id. An output
        ///                         parameter in case KV_VALIDATE_ID is set; if
        ///                         not, an input parameter
        ///@param[in]     pool_id   pool id, if set to 0 operation is done on
        ///                         default pool
        ///@param[in]     key       unique key
        ///@param[in]     key_len   size of the key in number of bytes
        ///@param[in]     value     user data corresponding to key that needs
        ///                         to be stored
        ///@param[in]     value_len actual size of the user data in number of
        ///                         bytes
        ///@param[in]     kv_iov    io vector with batch kv pair that needs to
        ///                         be read or written
        ///@param[in,out] iov_count input  - number of IOVs in the vector.
        ///                         output - number of IOVs that were
        ///                                  successfully validated
        ///@return                  returns success if all input paramteres are
        ///                         successfully validated error otherwise
        ///
        int kv_internal_validation(int flag, int id, NVM_KV_Store **kv_store,
                                   int pool_id, nvm_kv_api api,
                                   nvm_kv_key_t *key, uint32_t key_len,
                                   void *value, uint32_t value_len,
                                   nvm_kv_iovec_t *kv_iov, int iov_count);
        ///
        /// prints statistic for kv_get and kv_put API
        ///
        ///@return     none
        ///
        void kv_print_stats();
        ///
        ///compute the number of sparse address
        ///bits based on file/device size
        ///
        ///@param[in] size   size of file / device
        ///@return           returns the number of sparse address bits
        ///
        int comp_sparse_addr_bits(uint64_t size, uint32_t sector_size);
        ///
        ///reset statistic counters
        ///
        ///@return  none
        ///
        void reset_stats();
        ///
        ///wrapper function to issue range_exist over all possible
        ///key collisions and handle according to requirement of
        ///issuing API. Is currently used by kv_exists and kv_get_val_len
        ///If called by kv_exists, will issue read on successful range exist
        ///call. If called by kv_get_val_len, will update the value length
        ///on successful range_exist call
        ///
        ///@param[in]  kv_store    pointer to the KV store object
        ///@param[in]  pool_id     pool_id, if set to 0 operation is done on
        ///                        default pool
        ///@param[in]  key         key for which value length is needed
        ///@param[in]  key_len     length of the key
        ///@param[in]  api         API which is calling this wrapper
        ///@param[out] max_val_len approximate value length of the key returned
        ///                        used when called by kv_get_val_len
        ///@param[out] hdr         header of the key read when called by kv_exists
        ///@return                 returns 0 on success and error when failed
        ///
        int kv_range_exist_wrapper(NVM_KV_Store *kv_store, int pool_id,
                                   nvm_kv_key_t *key,
                                   uint32_t key_len, nvm_kv_api api,
                                   int *max_val_len, nvm_kv_header_t **hdr);
        ///
        ///internal API which deletes key value pair on
        ///the KV store and synchronizes all delete operations
        ///
        ///@param[in] pool_id   pool id, if set to 0 operation is done on
        ///                     default pool
        ///@param[in] key       unique key which is byte array
        ///@param[in] key_len   size of the key in number of bytes
        ///@param[in] kv_store  Pointer of the KV store to be operated on
        ///@return              returns 0 on success,or appropriate error code
        ///                     on error
        ///
        int kv_delete_sync(int pool_id, nvm_kv_key_t *key,
                           uint32_t key_len, NVM_KV_Store *kv_store);
        ///
        ///this is internal API used only by kvPut to trim entry before replace
        ///
        ///@param[in] del_lba   lba that needs to be trimed
        ///@param[in] hdr       KV header for the obtaining key_len and
        ///@param[in] kv_store  Pointer of the KV store to be operated on
        ///@return              NVM_SUCCESS or appropriate error code
        ///
        int kv_delete_internal(uint64_t del_lba, nvm_kv_header_t *hdr,
                               NVM_KV_Store *kv_store);
        ///
        ///wrapper function called to issue discard on specific range
        ///
        ///@param[in] id    KV store id to validate
        ///@return          returns 0 on success or appropriate error
        ///
        int kv_del_wrapper(int id);
        ///
        ///issues exist call before writing on to the LBA, also fetch next
        ///available LBA
        ///
        ///@param[in]  kv_store      KV store info
        ///@param[in]  key           unique key, input to APIs
        ///@param[in]  key_len       size of the key in number of bytes
        ///@param[in]  pool_id       pool id
        ///@param[in]  value_len     actual size of the user data in number of
        ///                          bytes
        ///@param[in]  buf           buffer that can be used internally
        ///@param[in]  replace       from KV store APIs to check if the value can
        ///                          be replaced
        ///@param[out] trim_len      holds the value length that needs to be
        ///                          trimmed, if the key is being updated, else
        ///                          trim_len is set to 0 for new keys
        ///@param[out] key_hash_val  from this function which will be used
        ///                          in APIs like kv_put
        ///@param[out] insert_lba    if true lba is inserted into thread safe
        ///                          list, should be removed once writing on to
        ///                          the LBA location
        ///@param[in]  lba_list      list of LBAs that were already scanned,
        ///                          in case of batch operation
        ///@return                   returns 0 on success -1 on error
        ///
        int kv_gen_lba(NVM_KV_Store *kv_store, nvm_kv_key_t *key,
                       uint32_t key_len, int pool_id, uint32_t value_len,
                       char *buf, bool replace, uint32_t *trim_len,
                       uint64_t &key_hash_val, bool &insert_lba,
                       map<uint64_t, nvm_kv_iovec_t*> *lba_list);
        ///
        ///internal API called to check wether to use
        ///user buffer or buffer from bufferPool
        ///
        ///@param[in] sum_write_len sum of write length which is key_len +
        ///                         value_len + kv_header_size
        ///@param[in] sector_size   sector size of the device
        ///
        bool kv_use_user_buf(uint64_t sum_write_len,
                             uint32_t sector_size);
        ///
        ///internal function that queries media for the regions of
        ///contiguous data in a given range
        ///
        ///@param[in]  fd            fd for the KV store in question
        ///@param[out] found_range   discovered range
        ///@param[in]  search_range  range to be searched for
        ///                          contiguous data range
        ///@return                   0 if range does not exists, 1 if range
        ///                          exists or appropriate error code in case
        ///                          of error scenarios
        ///
        int kv_range_exists(int fd, nvm_block_range_t *found_range,
                            nvm_block_range_t *search_range);
        ///
        ///helper method used to read KV pair for a specific key from media
        ///
        ///
        ///@param[out] header     KV header which would be populated by
        ///                       this method
        ///@param[in]  pool_id    pool id, if set to 0 operation is done on
        ///                       default pool
        ///@param[in]  kv_store   pointer of the KV store to be operated on
        ///@param[in]  key        unique key which is byte array
        ///@param[in]  key_len    size of the key in number of bytes
        ///@param[out] value      user data corresponding to key that needs to
        ///                       be read
        ///@param[in]  value_len  actual size of the user data in number of
        ///                       bytes
        ///@param[out] key_hash   hash of the key (lba)
        ///@param[out] buf        buffer used to store KV header and value (if
        ///                       value_offset is less than 1 sector)
        ///@param[in]  read_exact if set to true, kv_range_exists will be called
        ///                       to know the exact range length and read from
        ///                       media is done based on the range length
        ///                       returned
        ///@return                returns size of blocks read or
        ///                       appropriate error.
        ///
        int kv_read_media(nvm_kv_header_t **header, int pool_id,
                          NVM_KV_Store *kv_store, nvm_kv_key_t *key,
                          uint32_t key_len, void *value,
                          uint32_t value_len, uint64_t *key_hash_val,
                          char *buf, bool read_exact);
        ///
        ///helper method used to allocate buffer before
        ///write, used by APIs like put, batch_put
        ///
        ///@param[in]  kv_device         pointer to the device
        ///@param[in]  key_len           size of the key in number of bytes
        ///@param[in]  value             user passed data buffer
        ///@param[in]  value_len         actual size of user data in number of
        ///                              bytes
        ///@param[out] buf               buffer allocated for write operation
        ///@param[out] buf_len           legth of buffer allocated, used for
        ///                              freeing up the buffer
        ///@param[out] iov_count         number of IOVs that are required for
        ///                              vectored operation
        ///@param[out] user_buffer_usage this param is filled in this function,
        ///                              to tell the caller function whether to
        ///                              use user buffer or not
        ///@param[out] value_offset      offset where value exactly starts
        ///                              within KV pair
        ///@return                       returns 0 on success or error code on
        ///                              error.
        ///
        int kv_alloc_for_write(nvm_kv_store_device_t *kv_device,
                               uint32_t key_len,
                               void *value, uint32_t value_len, char **buf,
                               uint32_t *buf_len, uint32_t *iov_count,
                               bool *user_buffer_usage,
                               uint32_t *value_offset);
        ///
        ///helper method used to preprocess buffer before write, used by APIs
        ///like kv_put
        ///
        ///@param[in] pool_id           pool id to which the key belongs
        ///@param[in] key               unique key which is byte array
        ///@param[in] key_len           size of the key in number of bytes
        ///@param[in] value             user passed data buffer
        ///@param[in] value_len         actual size of user data in number of
        ///                             bytes
        ///@param[in] buf               buffer allocated for write operation
        ///@param[in] kv_device         pointer to the device
        ///@param[in] iovec             vectors which is been filled up for
        ///                             writing
        ///@param[in] user_buffer_usage if set to true user_buffer is used else
        ///                             buffer from buffer pool is used
        ///@param[in] expiry            expiry in seconds which specifies
        ///                             expiration time for the KV pair.
        ///@param[in] gen_count         generation count
        ///@param[in] key_hash_val      LBA calculated for the write
        ///@param[in] value_offset      offset where value exactly starts
        ///                             within KV pair
        ///@return                      returns 0 on success or error code on
        ///                             error.
        ///
        int kv_process_for_write(int pool_id, nvm_kv_key_t *key,
                                 uint32_t key_len, void *value,
                                 uint32_t value_len, char *buf,
                                 nvm_kv_store_device_t *kv_device,
                                 nvm_iovec_t *iovec,
                                 bool user_buffer_usage,
                                 uint32_t expiry, uint32_t gen_count,
                                 uint64_t key_hash_val, uint32_t value_offset);
        ///
        ///helper method used to pre-process IOVs for trim, used in APIs like
        ///kv_put, kv_batch_put to trim extra sector incase of replace
        ///
        ///@param[in]  value_len   length in bytes that needs to be over
        ///                        written
        ///@param[in]  trim_len    length in bytes that needs to be trimmed
        ///@param[out] iovec       address of IOVs passed by caller
        ///@param[out] iovec_count number of IOVs filled by this function
        ///@param[in]  lba         lba of already existing key
        ///@param[in]  kv_device   pointer to the underlying device
        ///@return                 return 0 on success or return appropriate
        ///                        error code
        ///
        int kv_process_for_trim(uint32_t value_len, uint32_t trim_len,
                                nvm_iovec_t *iovec, uint32_t *iovec_count,
                                uint64_t lba,
                                nvm_kv_store_device_t *kv_device);
        ///
        ///this is internal API used only by public functions like kv_delete
        ///
        ///@param[in]  kv_device   pointer to the device object
        ///@param[in]  del_lba     lba that needs to be trimmed
        ///@param[in]  discard_len value length that needs to be trimmed
        ///@return                 NVM_SUCCESS or appropriate error code
        ///
        static int kv_del_range(nvm_kv_store_device_t *kv_device,
                                uint64_t del_lba,
                                uint64_t discard_len);
        ///
        ///internal API used for checking if the key is expired or not.
        ///If it is, delete is issued on that perticular key
        ///
        ///@param[in] key       unique key which is byte array
        ///@param[in] kv_store  Pointer of the KV store to be operated on
        ///@param[in] hdr       KV header to obtain key_len, value_len and
        ///                     expiry time
        ///@param[in] key_loc   key location that needs to be deleted if the
        ///                     key is expired
        ///@param[in] del_exp   if set to true will delete expired keys, if
        ///                     set to false will not delete expired keys
        ///@return              NVM_SUCCESS or appropriate error code
        ///
        int kv_expire(nvm_kv_key_t *key, NVM_KV_Store *kv_store,
                      nvm_kv_header_t *hdr, uint64_t *key_loc,
                      bool del_exp);


        NVM_KV_Buffer_Pool m_buffer_pool;        ///< buffer pool
        map<int, NVM_KV_Store*> m_kvMap;         ///< holds all instances of
                                                 ///< KV store mapped with store id
        pthread_mutex_t m_mtx_open;              ///< mutex used in kv_open

};

#endif //KV_STORE_MGR_H_
