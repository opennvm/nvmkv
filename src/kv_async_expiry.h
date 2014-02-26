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
#ifndef KV_ASYNC_EXPIRY_H
#define KV_ASYNC_EXPIRY_H
#include <queue>
#include "src/kv_wrappers.h"
#include "src/kv_scanner.h"
#include <nvm_primitives.h>
using namespace nvm_wrapper;

class NVM_KV_Store;

///
///This class is responsible for doing asynchronous deletion of key value pairs
///If expiry is set to either global expiry or Arbitrary expiry, when the keys
///are accessed during API calls like kv_get, kv_get_current and if these keys
///are expired, they are opportunistically deleted. If deletion is done along
///with the API calls it will affect the performance of these APIs.
///Hence they are deleted asynchronously in this class. This class maintains
///a queue and threads which do API calls will submit the keys into this queue
///for deletion if the keys are expired. Worker threads spawned by this class
///reads the request from the queue and deletes them using
///nvm_batch_atomic_operations
///
class NVM_KV_Async_Expiry : public NVM_KV_Scanner
{
    public:
        ///
        ///Initialises mutex, conditional variable, trim length,
        ///kv_device and the thread.
        ///
        ///@param[in] num_threads number of async threads
        ///@param[in] kv_store    KV store object associated with this scanner
        ///
        NVM_KV_Async_Expiry(int num_threads, NVM_KV_Store *kv_store);
        ///
        ///destroys async expiry threads
        ///
        ~NVM_KV_Async_Expiry();
        ///
        ///function performs asynchronous deletion, it deletes
        ///maximum number of iovecs which can be supported by the media
        ///by using nvm_atomic_batch_operations
        ///
        ///@return                  none
        ///
        void* start_thread();
        ///
        ///this function updates the queue with keys, which are expired,
        ///this enables asynchronous, when the max number of iovecs that
        ///are supported by media are reached, that particular block of
        ///iovecs is handed over to start_thread function to perform deletion
        ///
        ///@param[in] key_loc start loction of the key to be updated
        ///@param[in] kv_hdr  pointer to the header of the KV pair
        ///
        ///@return                  returns 0 on success or return appropriate
        ///                         error
        ///
        int64_t update_expiry_queue(uint64_t key_loc, nvm_kv_header_t *kv_hdr);

    private:
        std::queue<nvm_iovec_block_t *> m_queue;  ///< queue for storing blocks of iovecs

};

#endif //KV_ASYNC_EXPIRY_H
