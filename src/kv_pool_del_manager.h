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
#ifndef KV_POOL_DEL_MANAGER_H_
#define KV_POOL_DEL_MANAGER_H_

#include "src/kv_scanner.h"
#include "util/kv_bitmap.h"

using namespace kv_bitmap;

///
///class that defines an object that will
///scan all key/values on the KV Store and perform
///deletion of keys in pools. This class inherits the
///scanner class
///
class NVM_KV_Pool_Del_Manager: public NVM_KV_Scanner
{
   public:
        ///
        ///initializes variable for KV store object
        ///
        ///@param[in] num_threads number of pool delete threads
        ///@param[in] kv_store    KV store object associated with this scanner
        ///
        NVM_KV_Pool_Del_Manager(int num_threads, NVM_KV_Store *kv_store);
        //
        //
        //
        ~NVM_KV_Pool_Del_Manager();
        ///
        ///initializes the pool deletion manager
        ///
        ///@return  returns 0 on success or appropriate error
        ///
        int initialize();
        ///
        ///the routine that starts pool deletion
        ///
        void* start_thread();
        ///
        ///start traversing through the complete range of key
        ///delete kv pairs that belong to the pool in pool deletion map
        ///
        ///@param[in]   flag if there's a request to delete all user created pools
        ///
        ///@return       return success on successfully deleting keys from pools
        ///              that are in delete map in one pass, returns object not
        ///              found error when end of the key range is reached or
        ///              other appropriate error is returned
        ///
        int start_pool_delete(bool delete_all_pools);

    private:
        ///
        ///delete all keys from the media for the given pool id
        ///
        ///@param[in] pool_id pool_id whose keys to be deleted
        ///@param[in] validate_pool_id_on_media check pool id from the media
        ///@return    NVM_SUCCESS or appropriate error code
        ///
        int delete_pool(int pool_id, bool validate_pool_id_on_media);

        kv_bitmap_t *m_pools_to_delete;   ///<pools in progress in a bitmap
        bool m_validate_pool_id_on_media; ///<need to read pool id from media before deletion
        uint64_t m_usr_data_start_lba;    ///<starting lba of user data
        uint64_t m_usr_data_max_lba;      ///<max lba of user data
        nvm_logical_range_iter_t m_iter;  ///<logical range iterator to walk on pools
};
#endif //KV_POOL_DEL_MANAGER_H
