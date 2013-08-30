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
#ifndef KV_EXPIRY_MANAGER_H_
#define KV_EXPIRY_MANAGER_H_

#include <unistd.h>
#include "src/kv_scanner.h"

///
///class that defines a expiry thread object
///which is responsible for arbitrary expiry
///of keys in the KV store. This class
///inherits class KvScanner as most of the
///functionality done by expiry thread is common
///as that done by the scanner.
///
class NVM_KV_Expiry_Manager : public NVM_KV_Scanner
{
    public:
        ///
        ///initializes variable for KV store object
        ///
        ///@param[in] num_threads number of expiry threads
        ///@param[in] kv_store    the KV store object associated with
        ///                       this expiry thread
        ///
        NVM_KV_Expiry_Manager(int num_threads, NVM_KV_Store *kv_store);
        //
        //destroys expiry manager allocations
        //
        ~NVM_KV_Expiry_Manager();
        ///
        ///This function starts expiry thread
        ///
        void* start_thread();
        ///
        ///starts traversing through the complete range of keys
        ///delete kv pairs that are expired
        ///
        int start_expiry();
        ///
        ///expiry scanner checks for the heuristics to make sure expiry pass
        ///needs to be started, if heuristics is not met scanner will be put to
        ///sleep
        ///
        void wait_for_trigger();
        ///
        ///restarts expiry scanner if it is sleeping
        ///
        void restart_scanner_if_asleep();
        ///
        ///expiry scanner checks for the heuristics to make sure expiry pass
        ///needs to be started, if heuristics is met returns true else returns
        ///false
        ///
        ///@return        returns true if scanner should start deleting expired keys
        ///               else returns false
        ///
        bool expire_keys();

    private:
        static const int M_TIME_INTERVAL = 86400;  ///< Time interval to trigger expiry (in seconds)
        static const int M_TRIGGER_PERCENT = 25;   ///< First trigger will be given after drive is 25% filled
        static const int M_TRIGGER_NEXT = 5;       ///< Subsequent capacity triggers after 5%
        double m_last_seen_capacity;               ///< Last seen percentage of capacity
};
#endif //KV_EXPIRY_MANAGER_H_
