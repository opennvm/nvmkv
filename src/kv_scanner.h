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
#ifndef KV_SCANNER_H_
#define KV_SCANNER_H_
#include "src/kv_layout.h"
#include "src/kv_wrappers.h"
#include "util/kv_buffer_pool.h"
#include <pthread.h>

class NVM_KV_Store;

///
///class that defines a scanner object that will
///scan all key/values on the KV Store and perform
///deletion of keys in pools. Initialize the pool
///deletion manager, expiry manager, and asynchronous
///expiry, start, create and exit threads of these classes
///
class NVM_KV_Scanner
{
    public:
        ///
        ///assigns KV store member variable
        ///
        ///@param[in] num_threads number of scanner threads
        ///                       that needs to be created
        ///@param[in] kv_store    The KV store class object
        ///
        NVM_KV_Scanner(int num_threads, NVM_KV_Store *kv_store);
        ///
        ///destroys pthread mutex and thread condition variables
        ///
        virtual ~NVM_KV_Scanner();
        ///
        ///does all necessary allocations and starts scanner
        ///thread by calling start_wrapper function
        ///
        ///@param[in] iter_type  type of the iterator
        ///@return               0 on success or appropriate
        ///                      error
        ///
        int initialize(int iter_type);
        ///
        ///a wrapper around start_thread routine to pass to pthread_create
        ///
        ///@param[in] arg       argument needed for thread handler
        ///
        ///@return              none
        ///
        static void* start_scanner_wrapper(void *arg);
        ///
        ///virtual function which is overridden to start thread routines by
        ///derived classes
        ///
        ///@return             none
        ///
        virtual void* start_thread() = 0;
        ///
        ///sets scanner cancel state, type
        ///
        void set_cancel_state();
        ///
        ///routine that is called when scanner is cancelled
        ///
        ///@param[in]  refrence to mutex
        ///
        static void scanner_cancel_routine(void *arg);
        ///
        ///virtual function which is overridden to exit threads by derived
        ///classes
        ///
        ///@return              none
        ///
        void cleanup_threads();
        ///
        ///scans drive for next available key
        ///
        ///@param[out] key_loc   location of the key filled by the routine
        ///@param[out] read_len  the length of the key filled by the routine
        ///@return               returns 0 on success and fills the information,
        ///                      returns error on failure
        ///
        int perform_scan(uint64_t *key_loc, uint64_t *read_len);
        ///
        ///resets the iterator to the front of the drive
        ///
        ///@return    Returns 0 on success and -1 on failure.
        ///
        int reset_iterator();
        ///
        ///signals the scanner to wake up if asleep
        ///
        virtual void restart_scanner_if_asleep();
        ///
        ///scanner waits on a conditional variable for the trigger
        ///
        virtual void wait_for_trigger();
        ///
        ///locks mutex
        ///
        void lock_mutex();
        ///
        ///unlocks mutex
        ///
        void unlock_mutex();
        ///
        ///This function checks the key for its expiry as well as
        ///fills the pool id the key belongs to. This is used by
        ///both scanner and expiry threads
        ///
        ///@param[in] key_loc   location of a particular key to be
        ///                     checked
        ///@param[out] pool_id  pool id is needed by pool deletion thread
        ///                     to check if the key belongs to particular
        ///                     pool
        ///@return              returns 0 if the key is not expired
        ///                     Returns 1 if the key is expired
        ///                     Returns -1 if any error
        ///
        int is_valid_for_del(uint64_t key_loc, uint32_t *pool_id);
        ///
        ///issues batch trim
        ///
        ///@param[in] iovec      array of keys which are expired
        ///@param[in] iov_count  number of keys which need to be trimmed
        ///
        ///@return               returns 0 if successful, else returns -1
        ///
        int batch_discard(nvm_iovec_t *iovec, uint32_t iov_count);

    protected:
        //disbale copy constructor and assignment operator
        DISALLOW_COPY_AND_ASSIGN(NVM_KV_Scanner);
        ///
        ///gets scanner buffer pool
        ///
        ///@return  returns m_buffer_pool
        ///
        NVM_KV_Buffer_Pool* get_buf_pool();
        ///
        ///gets scanner iovec member variable
        ///
        ///@return  returns m_iovec
        ///
        nvm_iovec_t* get_iovec();
        ///
        ///gets kvstore object
        ///
        ///@return  returns m_kv_store
        ///
        NVM_KV_Store* get_store();
        ///
        ///gets iterator type
        ///
        ///@return  returns m_iter_type
        ///
        int get_iter_type();
        ///
        ///gets mutex associated with scanner
        ///
        ///@return returns m_cond_mtx
        ///
        pthread_mutex_t* get_mutex();
        ///
        ///gets scanner condition variable
        ///
        ///@return returns m_cond_var
        ///
        pthread_cond_t* get_cond_var();

    private:
        static const uint32_t M_MAX_BUFFERS_IN_POOL = 8; ///< maximum number of buffers in buffer pool

        int m_iter_id;                   ///< unique iterator id
        int m_iter_type;                 ///< iterator based on scanner type
        int m_expiry_mode;               ///< expiry mode of the KV store
        pthread_cond_t m_cond_var;       ///< scanner's conditional variable
        pthread_mutex_t m_cond_mtx;      ///< mutex associated with scanner
        pthread_t *m_thread;             ///< scanner thread
        NVM_KV_Store *m_kv_store;        ///< KV store object
        nvm_iovec_t *m_iovec;            ///< Array for batch discard
        NVM_KV_Buffer_Pool m_buffer_pool;///< buffer pool
        int m_num_threads;               ///< number of scanner threads

};
#endif //KV_SCANNER_H_
