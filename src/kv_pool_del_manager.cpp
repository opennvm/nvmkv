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
#include "src/kv_store_mgr.h"
#include "src/kv_pool_del_manager.h"

//
//initializes pool deletion manager related data
//
NVM_KV_Pool_Del_Manager::NVM_KV_Pool_Del_Manager(int num_threads,
                                                 NVM_KV_Store *kv_store)
                        :NVM_KV_Scanner(num_threads, kv_store)
{
}
//
//destroys pool deletion manager threads
//
NVM_KV_Pool_Del_Manager::~NVM_KV_Pool_Del_Manager()
{
}
//
//the routine for deleting keys in pool
//
void* NVM_KV_Pool_Del_Manager::start_thread()
{
    bool pool_delete = true;
    pthread_mutex_t *glb_mtx =
        get_store()->get_pool_mgr()->get_glb_mutex();

    set_cancel_state();
    //register function that needs to be called on thread cancellation
    pthread_cleanup_push(&NVM_KV_Scanner::scanner_cancel_routine, get_mutex());
    pthread_cleanup_push(&NVM_KV_Scanner::scanner_cancel_routine, glb_mtx);
    while (pool_delete)
    {
        int ret_code = NVM_SUCCESS;

        //cancellation point
        pthread_testcancel();
        //check if the pool deletion map is empty, if not
        //return from the wait else start waiting for
        //external trigger
        pthread_mutex_lock(get_mutex());
        while (!get_store()->get_pool_mgr()->pool_del_in_progress())
        {
            wait_for_trigger();
        }
        pthread_mutex_unlock(get_mutex());
        //to yield to pool deletion manager, pool deletion status is set right
        //away
        get_store()->get_pool_mgr()->set_pool_del_status(true);
        //acquire global lock to synchronize with expiry manager
        pthread_mutex_lock(glb_mtx);
        while (ret_code == NVM_SUCCESS)
        {
            ret_code = start_pool_delete();
        }
        get_store()->get_pool_mgr()->set_pool_del_status(false);
        //trigger expiry thread
        if (get_store()->expiry_status())
        {
            get_store()->get_expiry_thread()->restart_scanner_if_asleep();
        }
        pthread_mutex_unlock(glb_mtx);
    }
    pthread_cleanup_pop(0);
    pthread_cleanup_pop(0);
    return NULL;
}
//
//start traversing through the complete range of keys
//delete kv pairs that belong to the pool in pool deletion map
//
int NVM_KV_Pool_Del_Manager::start_pool_delete()
{
    uint64_t key_loc = 0;
    uint64_t read_len = 0;
    uint64_t discard_len = 0;
    uint32_t iov_count = 0;
    int ret_code = 0;
    nvm_kv_store_capabilities_t capabilities;
    uint64_t max_trim_size_per_iov = 0;
    nvm_iovec_t *iovec = get_iovec();

    capabilities = get_store()->get_store_device()->capabilities;


    max_trim_size_per_iov =
        capabilities.nvm_atomic_write_multiplicity *
        capabilities.nvm_max_trim_size_per_iov;
    while ((ret_code = perform_scan(&key_loc, &read_len)) == NVM_SUCCESS)
    {
        if (delete_key_in_pool(key_loc, read_len))
        {
            //check if read_len is greater than max_trim_size_per_iov
            discard_len =
                nvm_kv_round_upto_blk(read_len, max_trim_size_per_iov);
            if (discard_len < max_trim_size_per_iov)
            {
                //there is some problem with length of key
                fprintf(stderr, "Error: Corrupted key\n");
                ret_code= -NVM_ERR_INTERNAL_FAILURE;
                break;
            }
            else
            {
                //key is candidate for discard
                iovec[iov_count].iov_base = 0;
                iovec[iov_count].iov_len = discard_len;
                iovec[iov_count].iov_lba = key_loc;
                iovec[iov_count].iov_opcode = NVM_IOV_TRIM;
                iov_count++;
            }
        }
        if (iov_count == capabilities.nvm_max_num_iovs)
        {
            ret_code = batch_discard(iovec, iov_count);
            iov_count = 0;
        }
    }
    //If end is reached and if there are remaining discards in iovs
    //flush the discards and reset iterator
    //for any other error reset iterator return from here
    reset_iterator();
    if (ret_code == -NVM_ERR_OBJECT_NOT_FOUND)
    {
        //issue the remaining discards
        if (iov_count)
        {
            batch_discard(iovec, iov_count);
            iov_count = 0;
        }

        //Update the inflight deletes with knowledge that one pass is done
        get_store()->get_pool_mgr()->update_del_map();
        //If all pools in the map are not deleted with one pass
        //return success so that next pass is started again
        if (get_store()->get_pool_mgr()->pool_del_in_progress())
        {
            return NVM_SUCCESS;
        }
    }
    iov_count = 0;
    return ret_code;
}
//
//deletes the given key/value if key is in a pool marked for deletion
//
int NVM_KV_Pool_Del_Manager::delete_key_in_pool(uint64_t key_loc, uint64_t len)
{
    int ret_code = NVM_SUCCESS;
    uint32_t pool_bits = get_store()->get_pool_mgr()->get_layout()->\
                         get_val_bits() - 1;
    uint32_t pool_mask = (1 << pool_bits) - 1;
    uint32_t pool_hash = key_loc & pool_mask;
    uint32_t found_pool_id = 1;
    bool poolid_hashed = true;

    //pool_hash is obtained from the key_loc and checked if the pool_hash
    //is under deletion, if pool_hash is under deletion, check if the maximum
    //number of pools supported is more that the pool bits, if so read pool_id
    //from media
    if (get_store()->get_pool_mgr()->pool_in_del(pool_hash, poolid_hashed))
    {
        if (get_store()->get_pool_mgr()->get_metadata()->max_pools >
            (1 << pool_bits))
        {
            poolid_hashed = false;
            //for the case where the pools are greater than 2K
            //read the pool id from media as well as check if key
            //is expired delete in either of the case
            ret_code = is_valid_for_del(key_loc, &found_pool_id);
            if ((ret_code < 0) ||
                 !get_store()->get_pool_mgr()-> \
                 pool_in_del(found_pool_id, poolid_hashed))
            {
                //the key does not belong to this pool and hence exit
                return NVM_SUCCESS;
            }
        }
        //key is eligible for deletion
        ret_code = 1;
    }

    return ret_code;
}
