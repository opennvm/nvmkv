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
#ifndef KV_SYNC_LIST_
#define KV_SYNC_LIST_

#include <set>
#include <pthread.h>
#include <stdint.h>
#include <include/kv_macro.h>
using namespace std;
///
///This class implements sychronized list which holds type uint64_t values
///It provides insert and delete functions on the list.
///Insert and Delete are thread safe. This class uses STL sets and adds
///thread safety on top of it
///
class NVM_KV_Sync_List
{
    public:
        ///
        ///creates pthread mutex and thread condition variables
        ///
        NVM_KV_Sync_List();
        ///
        ///destroys pthread mutex and thread condition variables
        ///
        ~NVM_KV_Sync_List();
        ///
        ///checks if the entry already exists in the list, if exists waits
        ///until entry gets deleted from the list, once deleted inserts
        ///the entry
        ///
        ///@param[in]  entry  entry to be inserted
        ///@param[out] wait   is set to true if thread waited while inserting
        ///@return            returns true if entry got inserted successfully
        ///                   else returns false
        ///
        bool insert_entry(uint64_t entry, bool *wait);
        ///
        ///deletes entry and signals that entry is deleted
        ///
        ///@param[in] entry  entry to be deleted
        ///@return           returns true if entry got deleted successfully
        ///                  else returns false
        ///
        bool delete_entry(uint64_t entry);

    private:
        //disbale copy constructor and assignment operator
        DISALLOW_COPY_AND_ASSIGN(NVM_KV_Sync_List);

        pthread_mutex_t m_mtx;          ///< pthread mutex for thread safety
        pthread_cond_t  m_condition_var;///< pthread condition variable
        set<uint64_t> m_list;     ///< set that stores entry
};
#endif //KV_SYNC_LIST_
