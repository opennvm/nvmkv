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
#ifndef KV_STORE_H_
#define KV_STORE_H_
#include "src/kv_layout.h"
#include "src/kv_pool_manager.h"
#include "src/kv_cache.h"
#include <nvm_primitives.h>
#include "util/kv_sync_list.h"

class NVM_KV_Scanner;
class NVM_KV_Iterator;

///
///Instance of this class represents a KV store on ioMemory
///This class holds all KV store related data set like layout, device
///info, poolManager, KV store metadata.
///
class NVM_KV_Store
{
    public:
        ///
        ///initializes KV store device
        ///
        NVM_KV_Store();
        ///
        ///destroys memory allocated for expiry thread, pool manager, KV store layout
        ///
        ~NVM_KV_Store();
        ///
        ///initializes KV store, creates store layout and poolManager
        ///
        ///@param[in] kv_id            KV store id
        ///@param[in] handle           nvm handle to invoke primitives
        ///@param[in] cap              kv store capabilities
        ///@param[in] sparse_addr_bits sparse address bits for KV store
        ///@param[in] max_pools        maximum number of pools that can be
        ///                            created
        ///@param[in] version          KV store version passed by application
        ///@param[in] expiry           Expiry support. Expected values:
        ///                            KV_DISABLE_EXPIRY(0)   - Disable the expiry
        ///                            KV_ARBITRARY_EXPIRY(1) - Enable arbitraty expiry
        ///                            KV_GLOBAL_EXPIRY(2)    - Enable global expiry
        ///@param[in] cache_size       amount of memory (in bytes) to be
        ///                            allocated for the collision cache
        ///
        ///@return                     returns 0 on success or appropriate error
        ///
        int initialize(int kv_id, nvm_handle_t handle, nvm_kv_store_capabilities_t cap,
                       uint32_t sparse_addr_bits, uint32_t max_pools,
                       uint32_t version, uint32_t expiry, uint64_t cache_size);
        ///
        ///fetches the layout object
        ///
        ///@return     address of layout object
        ///
        NVM_KV_Layout* get_layout();
        ///
        ///returns hash function object
        ///
        ///@return     returns m_pHashFunc
        ///
        NVM_KV_Hash_Func* get_hash_func();
        ///
        ///fetches pool manager object
        ///
        ///@return     address of pool manager object
        ///
        NVM_KV_Pool_Mgr* get_pool_mgr();
        ///
        ///fetches collision cache object
        ///
        ///@return     address of collision cache object
        ///
        NVM_KV_Cache* get_cache();
        ///
        ///fetches the KV store device object
        ///
        ///@return     address of KV store device object
        ///
        nvm_kv_store_device_t* get_store_device();
        ///
        ///fetches KV store metadata
        ///
        ///@return     address of KV store metadata
        ///
        nvm_kv_store_metadata_t* get_store_metadata();
        ///
        ///get sector size of the KV store
        ///
        ///@return     returns sector size
        ///
        uint32_t get_sector_size();
        ///
        ///fetches maximum kv pairs that can fit in one
        ///batch operation
        ///
        ///@return    maximum number of kv pairs that can
        ///           fit in one batch request
        ///
        uint32_t get_max_batch_size();
        ///
        ///checks if expiry threads are running
        ///
        ///@return    true if expiry scanners are running
        ///
        bool expiry_status();
        ///
        ///obtain the capability information from the device; validate
        // the capabilities are sufficient to support KV store; initialize the
        ///the internal capability structure
        ///
        ///@param[in]       handle          nvm handle.
        ///@param[in,out]   cap             pointer to the capability object.
        ///                                 The object fields will be
        ///                                 populated when function returns
        ///                                 successfully.
        ///@return                          NVM_SUCCESS,
        ///                                 -NVM_ERR_FEATURE_NOT_SUPPORTED,
        ///                                 -NVM_ERR_INTERNAL_FAILURE
        ///
        static int initialize_capabilities(nvm_handle_t handle,
                                           nvm_kv_store_capabilities_t *cap);
        ///
        ///gets the mode of expiry for the KV store
        ///
        ///@return     Returns the mode of expiry which can be:
        ///            KV_DISABLE_EXPIRY(0)   - Disable the expiry
        ///            KV_ARBITRARY_EXPIRY(1) - Arbitrary expiry
        ///            KV_GLOBAL_EXPIRY(2)    - Global expiry
        ///
        uint32_t get_expiry();
        ///
        ///fetches the number of sectors to be deleted based upon sector_size.
        ///
        ///@return     number of sectors to be deleted
        ///
        uint64_t get_del_sec_count();
        ///
        ///fetches iterator object which stores all iterators of KV store
        ///
        ///@return     returns iterator object
        ///
        NVM_KV_Iterator* get_iter();
        ///
        ///persists the KV store metadata
        ///
        ///@return     returns -1 on error and 0 on success
        ///
        int persist_kv_metadata();
        ///
        ///create and initialize expiry related scanners and pool deletion
        ///scanner
        ///
        ///@return              returns 0 if successful, else -1
        ///
        int init_scanners();
        ///
        ///getter function of asynchronous expiry instance
        ///
        ///@return              returns m_pAsyncExpiry
        ///
        NVM_KV_Scanner* get_async_expiry_thread();
        ///
        ///getter function of expiry instance
        ///
        ///@return              returns m_pExpiryThread
        ///
        NVM_KV_Scanner* get_expiry_thread();
        ///
        ///insert lba to the safe lba list
        ///
        ///@param[in]  lba    lba to be inserted
        ///@param[out] wait   is set to true if thread waited while inserting
        ///@return            returns true if entry got inserted successfully
        ///                   else returns false
        ///
        bool insert_lba_to_safe_list(uint64_t lba, bool *wait);
        ///
        ///deletes lba from the safe lba list
        ///
        ///@param[in] entry  entry to be deleted
        ///@return           returns true if entry got deleted successfully
        ///                  else returns false
        ///
        bool delete_lba_from_safe_list(uint64_t lba);
        ///
        ///delete all user data from the media and the cache
        ///
        ///@return      NVM_SUCCESS or appropriate error code
        ///
        int delete_all();
        ///
        ///delete the data blocks within the given range in a brute-force way
        ///from the media
        ///
        ///@param[in]  start_lba     lba that needs to be trimmed
        ///@param[in]  delete_len value length that needs to be trimmed
        ///@return     NVM_SUCCESS or appropriate error code
        ///
        int delete_range(uint64_t start_lba, uint64_t delete_len);
        ///
        ///delete all keys within the given range using logical range iterator
        ///from the media
        ///
        ///@param[in]  start_lba   starting lba of the range
        ///@param[in]  delete_len length of the range
        ///@return                 NVM_SUCCESS or appropriate error code
        ///
        int delete_all_keys(uint64_t start_lba, uint64_t delete_len);
        ///
        ///batch delete protected by safe lba list. The default operation deletes
        ///the keys from both the media and the cache.
        ///
        ///@param[in] iovec             array of keys to be deleted
        ///@param[in] iov_count         number of keys which need to be deleted
        ///@param[in] delete_from_cache delete the keys from cache if the flag
        ///                             if the flag is true
        ///
        ///@return               returns 0 if successful, else returns -1
        ///
        int batch_delete_sync(nvm_iovec_t *iovec, uint32_t iov_count,
                              bool delete_from_cache = true);
        ///
        ///batch delete without safe lba list protection. The function deletes
        ///the keys from the media only.
        ///
        ///@param[in] iovec             array of keys to be deleted
        ///@param[in] iov_count         number of keys which need to be deleted
        ///
        ///@return               returns 0 if successful, else returns -1
        ///
        int batch_delete(nvm_iovec_t *iovec, uint32_t iov_count);

    private:
        static const uint32_t M_KV_REVISION = 1;  ///< internal revision of KV store
        static const uint32_t M_CAP_COUNT   = 10;  ///< number of NVM capabilities
        static const uint32_t M_MAX_BUFFERS_IN_POOL = 1; ///< max number of
                                                         ///< buffers in buffer
                                                         ///< pool

        //disable copy constructor and assignment operator
        DISALLOW_COPY_AND_ASSIGN(NVM_KV_Store);

        NVM_KV_Pool_Mgr *m_pPoolManager;          ///< pool manager object
        NVM_KV_Layout *m_pKvLayout;               ///< address of KV store layout object
        NVM_KV_Hash_Func *m_pHashFunc;            ///< hash functions object
        NVM_KV_Cache *m_cache;                    ///< instance of collision cache
        nvm_kv_store_device_t *m_pKvDevice;       ///< address of KV store device object
        nvm_kv_store_metadata_t *m_pStoreMetadata;///< metadata object for KV store
        uint32_t m_meta_data_buf_len;             ///< length of the buffer holding the meta data
        NVM_KV_Scanner *m_pAsyncExpiry;           ///< reference of async expiry object
        NVM_KV_Scanner *m_pExpiryThread;          ///< reference of expiry object
        uint64_t m_deleteSectorCount;             ///< number of sectors to be deleted per key
        NVM_KV_Iterator *m_iter;                  ///< all iterators in KV store
        NVM_KV_Buffer_Pool m_buffer_pool;         ///< buffer pool used by kvstore to memory allocation
        NVM_KV_Sync_List m_safe_lba_list;         ///< instance of safe LBA list
        bool m_exp_status;                        ///< status of expiry scanners
};
#endif //KV_STORE_H_
