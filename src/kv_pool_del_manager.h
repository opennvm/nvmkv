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
        ///the routine that starts pool deletion
        ///
        void* start_thread();
        ///
        ///start traversing through the complete range of key
        ///delete kv pairs that belong to the pool in pool deletion map
        ///
        ///@return       return success on successfully deleting keys from pools
        ///              that are in delete map in one pass, returns object not
        ///              found error when end of the key range is reached or
        ///              other appropriate error is returned
        ///
        int start_pool_delete();
        ///
        ///function specific to deletion of keys in a pool
        ///
        ///@param[in] key_loc  location of key to delete
        ///@param[in] len      length of region to be deleted.
        ///@return             Returns 0 on success and appropriate
        ///                    error on failure.
        ///
        int delete_key_in_pool(uint64_t key_loc, uint64_t len);

};
#endif //KV_POOL_DEL_MANAGER_H
