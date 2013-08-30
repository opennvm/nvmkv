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
#include <stdio.h>
#include "src/kv_async_expiry.h"
#include "src/kv_store.h"
#include "util/kv_sync_list.h"

//
//initializes the member variables for asynchronous deletion
//
NVM_KV_Async_Expiry::NVM_KV_Async_Expiry(int num_threads, NVM_KV_Store *kv_store)
                    :NVM_KV_Scanner(num_threads, kv_store)
{
}
//
//destroys async expiry threads
//
NVM_KV_Async_Expiry::~NVM_KV_Async_Expiry()
{
    while (!m_queue.empty())
    {
        nvm_iovec_block_t *iov_blk = m_queue.front();
        m_queue.pop();
        delete iov_blk;
    }
}
//
//function performs asynchronous deletion, it deletes
//maximum number of iovecs which can be supported by the media
//by using nvm_batch_atomic_operations
//
void* NVM_KV_Async_Expiry::start_thread()
{
    bool wait = false;
    uint64_t max_iovecs = get_store()->get_store_device()->
        capabilities.nvm_max_num_iovs;
    nvm_iovec_block_t *iovec_block = NULL;
    nvm_iovec_t *iovec_entry = NULL;
    bool insert_lba[max_iovecs];
    bool async_del = true;

    set_cancel_state();
    //register function that needs to be called on thread cancellation
    pthread_cleanup_push(&NVM_KV_Scanner::scanner_cancel_routine, get_mutex());

    while (async_del)
    {
        pthread_mutex_lock(get_mutex());
        iovec_entry = NULL;

        //cancellation point
        pthread_testcancel();
        //keep waiting till queue is not empty
        while (m_queue.empty() || m_queue.front()->iov_count <
                max_iovecs)
        {
            wait_for_trigger();
        }

        iovec_block = m_queue.front();
        iovec_entry = m_queue.front()->iovec_entry;
        m_queue.pop();
        //unlock and start deleting
        pthread_mutex_unlock(get_mutex());

        //deletion of iovecs dependent only on popping from queue
        //Insertion of iovec in the sync map
        for (int i = 0; i < max_iovecs; i++)
        {
            insert_lba[i] = false;
            if (!get_store()->insert_lba_to_safe_list(
                        iovec_entry[i].iov_lba, &wait))
            {
                fprintf(stderr, "Error inserting the iovec entry\
                        in the sync map\n");
            }
            insert_lba[i] = true;
        }
        //deleting the iovecs
        nvm_writev(get_store()->get_store_device(), iovec_entry, max_iovecs, true);

        for (int i = 0; i < max_iovecs; i++)
        {
            if (insert_lba[i])
            {
                get_store()->delete_lba_from_safe_list(
                        iovec_entry[i].iov_lba);
            }
        }

        delete iovec_block->iovec_entry;
        delete iovec_block;
    }

    pthread_cleanup_pop(0);
    return NULL;
}
//
//this function updates the queue with keys, which are expired,
//this enables asynchronous, when the max number of iovecs that
//are supported by media are reached, that particular block of
//iovecs is handed over to start_thread function to perform deletion
//
int64_t NVM_KV_Async_Expiry::update_expiry_queue(uint64_t key_loc)
{
    bool iovec_present = false;
    int64_t ret_code = 0;
    uint64_t iov_count = 0;
    uint64_t max_iovecs = get_store()->get_store_device()->
        capabilities.nvm_max_num_iovs;
    nvm_iovec_block_t *iovec_block = NULL;
    nvm_iovec_t *iovec_entry = NULL;
    uint64_t trim_len = get_store()->get_layout()->get_max_val_range() /
        get_store()->get_sector_size();

    pthread_mutex_lock(get_mutex());

    if (m_queue.empty() ||
        ((iovec_block = m_queue.back())->iov_count == max_iovecs))
    {
        iovec_block = new(std::nothrow) nvm_iovec_block_t;
        if (iovec_block == NULL)
        {
            ret_code = -NVM_ERR_OUT_OF_MEMORY;
            pthread_mutex_unlock(get_mutex());
            return ret_code;
        }

        iovec_block->iov_count = 0;
        iovec_block->iovec_entry = new(std::nothrow) nvm_iovec_t[max_iovecs];
        if (iovec_block->iovec_entry == NULL)
        {
            ret_code = -NVM_ERR_OUT_OF_MEMORY;
            pthread_mutex_unlock(get_mutex());
            delete iovec_block;
            return ret_code;
        }

        m_queue.push(iovec_block);
    }

    iovec_entry = iovec_block->iovec_entry;
    iov_count = iovec_block->iov_count;

    //check to ensure that same key is not inserted twice in the queue
    for (int j = 0; j < iov_count; j++)
    {
        if (iovec_entry[j].iov_lba == key_loc)
        {
            iovec_present = true;
            break;
        }
    }

    if (!iovec_present)
    {

        iovec_entry[iov_count].iov_base = 0;
        iovec_entry[iov_count].iov_len = trim_len;
        iovec_entry[iov_count].iov_lba = key_loc;
        iovec_entry[iov_count].iov_opcode = NVM_IOV_TRIM;
        iovec_block->iov_count++;

        //starts deletion of iovec block from queue
        if (m_queue.front()->iov_count == max_iovecs)
        {
            restart_scanner_if_asleep();
        }
    }

    pthread_mutex_unlock(get_mutex());
    return 0;
}
