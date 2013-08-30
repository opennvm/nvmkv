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
#include "util/kv_sync_list.h"

//
//NVM_KV_Sync_List class definitions
//

//
//creates pthread mutex and thread condition variables
//
NVM_KV_Sync_List::NVM_KV_Sync_List()
{
    pthread_mutex_init(&m_mtx, NULL);
    pthread_cond_init(&m_condition_var, NULL);
}
//
//destroys pthread mutex and thread condition variables
//
NVM_KV_Sync_List::~NVM_KV_Sync_List()
{
    pthread_cond_destroy(&m_condition_var);
    pthread_mutex_destroy(&m_mtx);
}
//
//checks if the entry already exists in the list
//if exists waits until entry gets deleted from the list
//once deleted inserts the entry
//
bool NVM_KV_Sync_List::insert_entry(uint64_t entry, bool *wait)
{
    bool ret_code = true;
    set<uint64_t>::iterator itr;

    *wait = false;
    pthread_mutex_lock(&m_mtx);
    itr = m_list.find(entry);
    while (itr != m_list.end())
    {
        *wait = true;
        pthread_cond_wait(&m_condition_var, &m_mtx);
        itr = m_list.find(entry);
    }
    m_list.insert(entry);
    pthread_mutex_unlock(&m_mtx);

    return ret_code;
}
//
//deletes entry from the list and signal that entry is deleted
//
bool NVM_KV_Sync_List::delete_entry(uint64_t entry)
{
    set<uint64_t>::iterator itr;
    bool ret_code = true;

    pthread_mutex_lock(&m_mtx);
    itr = m_list.find(entry);
    if (itr == m_list.end())
    {
        ret_code = false;
    }
    m_list.erase(itr , m_list.end());
    //signal only if the entry got deleted
    if (ret_code)
    {
        pthread_cond_broadcast(&m_condition_var);
    }
    pthread_mutex_unlock(&m_mtx);
    return ret_code;
}
