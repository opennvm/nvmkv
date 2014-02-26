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
#ifndef KV_POOL_MANAGER_H_
#define KV_POOL_MANAGER_H_

#include <map>
#include <set>
#include <pthread.h>
#include <stdint.h>
#include "nvm_kv.h"
#include "src/kv_common.h"
#include "src/kv_layout.h"
#include "src/kv_pool_del_manager.h"
#include "util/kv_hash_func.h"
#include "util/kv_bitmap.h"
#include "util/kv_buffer_pool.h"

using namespace kv_bitmap;
using namespace std;

class NVM_KV_Store;
///
///each entry in pool manager's delete map is of type kv_delete_map_entry_t
///
typedef map<uint64_t, uint32_t> kv_delete_map_entry_t;
///
///type of pool manager's delete map
///
typedef map<uint32_t, kv_delete_map_entry_t * > kv_delete_map_t;
///
///bitmap associated with pool manager
///
typedef struct
{
    kv_bitmap_t *in_use;  ///< bitmap of pool ids that are in use
    kv_bitmap_t *deleted; ///< bitmap of pool ids that are being deleted
    uint32_t bm_len;      ///< length of bitmaps in bytes
    pthread_mutex_t mutex;///< mutex that synchronizes access to bitmaps
} kv_bitmap_info_t;
///
///class that maintains pools information associated with
///a KV store, has pool specific APIs like kv_pool_create, kv_pool_delete
///
class NVM_KV_Pool_Mgr
{
    public:
        ///
        ///initializes bitmaps and bitmap mutex
        ///
        NVM_KV_Pool_Mgr();
        ///
        ///destroys memory related to pool bitmaps
        ///
        ~NVM_KV_Pool_Mgr();
        ///
        ///initializes pool manager data set
        ///
        ///@param[in] kv_device  KV store specific data like sector_size and fds
        ///@param[in] layout     KV store layout objects which stores offset of
        ///                      metadata on media
        ///@param[in] hash_func  KV store hash function object
        ///@param[in] metadata   KV store metatdata object address
        ///
        ///@return               return NVM_SUCCESS or appropriate error
        ///
        int initialize(nvm_kv_store_device_t *kv_device, NVM_KV_Layout *layout,
                       NVM_KV_Hash_Func *hash_func,
                       nvm_kv_store_metadata_t *metatdata,
                       bool restore);
        ///
        ///initializes pool delete scanner
        ///
        ///@param[in] kv_store reference to kvstore object
        ///
        ///@return             returns success or appropriate error code
        ///
        int init_pool_del_scanner(NVM_KV_Store *kv_store);
        ///
        ///load pools' specific information from media onto memory
        ///
        ///@return   return NVM_SUCCESS on success or appropriate error code
        ///
        int restore();
        ///
        ///if max_pools for KV store is less than 2k returns
        ///
        ///@param[in] pool_id pool id as is, else returns hashed pool id
        ///
        ///@return            returns hashed pool id of length pool_bits
        ///
        uint32_t get_poolid_hash(int pool_id);
        ///
        ///gets sector to which the pool tag belongs
        ///
        ///@param[in] pool_id pool_id associated with pool_tag
        ///
        ///@return            return sector to which pool_tag belongs
        ///
        int get_pool_tag_sect(int pool_id);
        ///
        ///checks the status of the given pool_id
        ///
        ///@param[in]   pool_id the pool id
        ///
        ///@return      returns the status whether the pool is in use, not in use,
        ///             in process of deletion, or invalid
        ///
        int check_pool_status(int pool_id);
        ///
        ///check if the pool id is valid
        ///
        ///@return      false if pool id is other than default when no pools
        ///             are created else returns true
        ///
        bool valid_poolid(int pool_id);
        ///
        ///checks if the pool id is in the process of deletion
        ///
        ///@param[in] pool_id       id of the pool to be checked for deletion
        ///
        ///@return                   returns true if the pool is being deleted
        ///                          else returns false
        ///
        bool pool_in_del(int pool_id);
        ///
        ///creates pool, write in_use bitmap to media
        ///
        ///@param[in] pool_tag pool tag associated with pool id
        ///
        ///@return             on success returns valid pool id else returns
        ///                    error
        ///
        int create_pool(nvm_kv_pool_tag_t *pool_tag);
        ///
        ///clears the bit in deleted bitmap for pools to be
        ///deleted, pool_id is put in pool deletion queue. If
        ///pool id provided is -1, then all pools are marked for
        ///deletion and put in queue
        ///
        ///@param[in] pool_id  pool id that needs to be deleted, if set to -1,
        ///                    all pools are considered for deletion except
        ///                    the default pool.
        ///
        ///@return             on success returns 0 else returns -1 on error
        ///
        int delete_pool(int pool_id);
        ///
        ///gets the default pool id constant of this class
        ///
        ///@return    returns M_DEFAULT_POOL_ID
        ///
        int get_default_poolid();
        ///
        ///gets the all pool id constant of this class
        ///
        ///@return      returns M_POOLID_ALL
        ///
        int get_all_poolid();
        ///
        ///set pool tag associated with pool id
        ///
        ///@param[in] pool_id  unique id associated with pool
        ///@param[in] pool_tag unique tag associated with pool id
        ///
        ///@return                    0 on success or appropriate error code
        ///
        int set_pool_tag(int pool_id, nvm_kv_pool_tag_t *pool_tag);
        ///
        ///get pool tag associated with pool id
        ///
        ///@param[in]  pool_id   unique id associated with pool
        ///@param[out] pool_tags address of pool tags in memory
        ///
        ///@return
        ///
        int get_pool_tag(int pool_id, nvm_kv_pool_tag_t *pool_tags);
        ///
        ///gets kvstore object
        ///
        ///@return  kvstore object associated with the pool manager
        ///
        NVM_KV_Store* get_store();
        ///
        ///gets the layout
        ///
        NVM_KV_Layout *get_layout();
        ///
        ///gets the store metadata
        ///
        nvm_kv_store_metadata_t *get_metadata();
        ///
        ///checks if pool deletion thread is running
        ///
        ///@return     returns true if pool deletion thread is running
        ///            else false
        ///
        bool check_pool_del_status();
        ///
        ///sets the status of the pool deletion thread
        ///
        ///@param[in] mode  if true, means that pool deletion thread
        ///                 is running, if false, means that pool deletion
        ///                 thread is not running
        ///
        void set_pool_del_status(bool mode);
        ///
        ///get global mutex shared by pool deletion manager and expiry
        ///manager
        ///
        ///@return          returns global mutex
        ///
        pthread_mutex_t* get_glb_mutex();
        ///
        ///get global condition variable associated with global mutex
        ///
        ///@return          returns global condition variable
        ///
        pthread_cond_t* get_glb_cond_var();
        ///
        ///cancel pool delete scanner
        ///
        void cancel_pool_del_scanner();
        ///
        ///indicate if there are pools to be deleted or not
        ///
        ///@return  there are pools to be deleted or not
        ///
        bool has_pools_to_delete();
        ///
        ///copy out the current pool deletion bits to the bitmap parameter
        ///
        ///@param[in,out] pool_bitmap      bitmap to be set by the function
        ///@param[in,out] delete_all_pools all pools in the bitmap need to
        ///                                be deleted
        ///
        ///@return NVM_SUCCESS or appropriate error code
        ///
        int get_pool_deletion_bitmap(kv_bitmap_t *&pool_bitmap,
                                     bool &delete_all_pools);
        ///
        ///clear certain pool bits in pool bitmaps
        ///
        ///@param[in] clear_pool_bitmap  the pool bits to be cleared at pool
        ///                              bitmaps
        ///
        ///@return    NVM_SUCCESS or appropriate error code
        ///
        int clear_pool_bitmaps(kv_bitmap_t *&clear_pool_bitmap);
        ///
        ///clear the bit in pool bitmaps for the given pool id
        ///
        ///@param[in] pool_id  pool id
        ///
        ///@return    NVM_SUCCESS or appropriate error code
        ///
        int clear_pool_bitmaps(uint32_t pool_id);
    private:
        static const uint32_t M_KVSTORE_METADATA   = (1 << 0);///< update metadata
        static const uint32_t M_KV_BM_INUSE        = (1 << 1);///< update bitmap in use
        static const uint32_t M_KV_BM_DELETED      = (1 << 2);///< update deleted bitmap
        static const uint32_t M_KV_POOL_TAGS       = (1 << 3);///< update pool tags
        static const uint32_t M_DEFAULT_POOL_ID    = 0;       ///< default pool id
        static const int32_t M_POOLID_ALL          = -1;      ///< indicates all pool id
        static const uint32_t M_MAX_BUFFERS_IN_POOL = 1;      ///< max number of
                                                              ///< buffers in
                                                              ///< buffer pool

        //disbale copy constructor and assignment operator
        DISALLOW_COPY_AND_ASSIGN(NVM_KV_Pool_Mgr);
        ///
        ///persists changes on the media depending on the combination
        ///of bitmaps/metadata
        ///
        ///@param[in] type       Can be one or the combination of the following:
        ///                      KV_BM_DELETED    - The deleted bitmap
        ///                      KV_BM_INUSE      - In-use bitmap
        ///                      KVSTORE_METADATA - Metadata
        ///@param[in] pool_index Pool id representing the bit in the bitmap
        ///                      Valid combinations of the pool index and type
        ///                      can be one of the following:
        ///                      1. pool_index = -1/any,
        ///                         type = KVSTORE_METADATA
        ///                         Only metadata will be written on the media
        ///                      2. pool_index = -1, type = KV_BM_INUSE or
        ///                                                 KV_BM_DELETED
        ///                         Complete bitmaps will be written. If the
        ///                      3. pool_index = <num>, type = KV_BM_INUSE or
        ///                                                    KV_BM_DELETED
        ///                      The pool index in specified bitmaps will
        ///                      be written.
        ///                      (Metadata flag does not consider pool index
        ///                      and hence if set in type, metadata will be
        ///                      written on the media independent of what pool
        ///                      id is.)
        ///
        ///@return               returns 0 on success and appropriate error
        ///                      code on error
        ///
        int persist_store_info(uint32_t type, int pool_index);
        ///
        ///persists pool tags on media
        ///
        ///@param[in]     iov   address of iov used for writing
        ///
        ///@return              0 on success or appropriate error code
        ///
        int persist_pool_tags(nvm_iovec_t *iov);
        ///
        ///get sector size of the underlying device.
        ///
        ///@return           returns sector size
        ///
        uint32_t get_sector_size();

        kv_bitmap_info_t m_bitmaps;               ///< data stucture which holds in-use and deleted pools bitmap

        nvm_kv_store_device_t *m_pKvDevice;       ///< reference to KV store device
        nvm_kv_store_metadata_t *m_pStoreMetadata;///< reference to KV store metadata
        NVM_KV_Layout *m_pLayout;                 ///< reference to KV store layout
        NVM_KV_Hash_Func *m_pHashFunc;            ///< reference to hash function object
        nvm_kv_pool_tag_t *m_pPoolTags;           ///< pool_tags in memory
        kv_tags_map_t m_tags_map;                 ///< map that stores association of <pool tag, pool id>
        uint32_t           m_poolTagSize;         ///< size of entire pool tags
        NVM_KV_Buffer_Pool m_buffer_pool;         ///< buffer pool used to allocate memory
        NVM_KV_Pool_Del_Manager *m_pPoolDelThread;///< scanner that deletes pool
        bool            m_pool_del_status;        ///< pool deletion thread status
        pthread_mutex_t m_glb_mtx;                ///< global mutex shared by pool deletion and expiry
        pthread_cond_t  m_glb_cond;               ///< global conditional variable associated with global mutex

};
#endif //KV_POOL_MANAGER_H_
