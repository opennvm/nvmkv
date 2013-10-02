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
#include <new>
#include <sys/time.h>
#include "src/kv_store_mgr.h"
#include "src/kv_expiry_manager.h"
//
//initialize expiry manager
//
NVM_KV_Expiry_Manager::NVM_KV_Expiry_Manager(int num_threads,
    NVM_KV_Store *kv_store) : NVM_KV_Scanner(num_threads, kv_store)
{
    m_last_seen_capacity = 0.0;
    m_no_expire_count = 0;
}
//
//destroys expiry manager
//
NVM_KV_Expiry_Manager::~NVM_KV_Expiry_Manager()
{
}
//
//routine that starts the scanner pass and discards
//key-value pair that are expired
//
void* NVM_KV_Expiry_Manager::start_thread()
{
    pthread_mutex_t *glb_mtx =
        get_store()->get_pool_mgr()->get_glb_mutex();
    bool expiry = true;

    set_cancel_state();
    //register function that needs to be called on thread cancellation
    pthread_cleanup_push(&NVM_KV_Scanner::scanner_cancel_routine, glb_mtx);
    //Global mutex is used to protect expiry manager
    //and pool delete manager from not deleting same addresses
    pthread_mutex_lock(glb_mtx);
    while (expiry)
    {
        //cancellation point
        pthread_testcancel();
        while (get_store()->get_pool_mgr()->check_pool_del_status()
              || !expire_keys())
        {
            wait_for_trigger();
        }
        start_expiry();
    }
    pthread_mutex_unlock(glb_mtx);
    pthread_cleanup_pop(0);
    return NULL;
}
//
//start traversing through the complete range of keys
//delete kv pairs that are expired
//
int NVM_KV_Expiry_Manager::start_expiry()
{
    uint64_t key_loc = 0;
    uint64_t read_len = 0;
    uint64_t discard_len = 0;
    uint32_t iov_count = 0;
    nvm_iovec_t *iovec = get_iovec();
    int ret_code = 0;
    nvm_kv_store_capabilities_t capabilities;
    uint64_t max_trim_size_per_iov = 0;

    capabilities = get_store()->get_store_device()->capabilities;
    max_trim_size_per_iov =
        capabilities.nvm_atomic_write_multiplicity *
        capabilities.nvm_max_trim_size_per_iov;

    while (!get_store()->get_pool_mgr()->check_pool_del_status() &&
           ((ret_code = perform_scan(&key_loc, &read_len)) == NVM_SUCCESS))
    {
        if (get_iter_type() != KV_GLB_EXP_ITER)
        {
            //check key if expired
            ret_code = is_valid_for_del(key_loc, NULL);
            if (ret_code < 0)
            {
                break;
            }
            else if (ret_code == 0)
            {
                continue;
            }
        }
        //check if read_len is greater than max_trim_size_per_iov
        discard_len = nvm_kv_round_upto_blk(read_len,
                max_trim_size_per_iov);
        if (discard_len > max_trim_size_per_iov)
        {
            //there is some problem with length of key
            fprintf(stderr, "Error: Corrupted key\n");
            ret_code = -NVM_ERR_INTERNAL_FAILURE;
            break;
        }
        else
        {
            //if expired, put in batch for discarding
            iovec[iov_count].iov_base = 0;
            iovec[iov_count].iov_len = discard_len;
            iovec[iov_count].iov_lba = key_loc;
            iovec[iov_count].iov_opcode = NVM_IOV_TRIM;
            iov_count++;
        }
        if (iov_count == capabilities.nvm_max_num_iovs)
        {
            ret_code = batch_discard(iovec, iov_count);
            if (ret_code < 0)
            {
                break;
            }
            iov_count = 0;
        }
    }
    if (ret_code == -NVM_ERR_OBJECT_NOT_FOUND)
    {
        ret_code = NVM_SUCCESS;
    }
    //issue discards on any of the keys in buffer
    //only when the end is reached
    if (iov_count && ret_code == NVM_SUCCESS)
    {
        ret_code = batch_discard(iovec, iov_count);
    }
    reset_iterator();
    return ret_code;
}
//
//expiry scanner checks for the heuristics to make sure expiry pass
//needs to be started, if heuristics is met returns true else returns
//false
//
bool NVM_KV_Expiry_Manager::expire_keys()
{
    nvm_capacity_t capacity_info;
    int ret_code = 0;
    bool ret_val = false;
    double percent_used = 0;

    //check the % used of the physical capacity
    ret_code = nvm_get_capacity(get_store()->get_store_device()->nvm_handle,
                                &capacity_info);
    if (ret_code >= 0)
    {
        percent_used = ((double)capacity_info.used_phys_capacity/
                (double)capacity_info.total_phys_capacity) * 100;
        if (m_last_seen_capacity && (percent_used > m_last_seen_capacity))
        {
            m_no_expire_count = 0;
            //if percentage is greater than M_TRIGGER_NEXT of last
            //triggering percentage, return true
            if ((percent_used - m_last_seen_capacity) > M_TRIGGER_NEXT)
            {
                ret_val = true;
            }
        }
        else if (percent_used == m_last_seen_capacity)
        {
            if (m_no_expire_count > M_NO_EXPIRY_LIMIT)
            {
                m_no_expire_count = 0;
                ret_val = false;
            }
            else
            {
                m_no_expire_count++;
                ret_val = true;
            }
        }
        else if (percent_used >= M_TRIGGER_PERCENT)
        {
            m_no_expire_count = 0;
            //if percentage is > M_TRIGGER_PERCENT
            ret_val = true;
        }
    }
    m_last_seen_capacity = percent_used;
    return ret_val;
}
//
//expiry scanner goes on timed wait, it will be either
//triggered by pool deletion manager or when it completes
//sleep
//
void NVM_KV_Expiry_Manager::wait_for_trigger()
{
    struct timespec   ts;
    struct timeval    tp;
    pthread_mutex_t *glb_mtx = get_store()->get_pool_mgr()->
        get_glb_mutex();
    pthread_cond_t *glb_cond_var = get_store()->get_pool_mgr()->
        get_glb_cond_var();

    gettimeofday(&tp, NULL);
    //convert from timeval to timespec
    ts.tv_sec  = tp.tv_sec;
    ts.tv_nsec = tp.tv_usec * 1000;
    ts.tv_sec += M_TIME_INTERVAL;
    pthread_cond_timedwait(glb_cond_var, glb_mtx, &ts);
    return;
}
//
//restarts expiry scanner if it is sleeping
//on the conditional variable.
//
void NVM_KV_Expiry_Manager::restart_scanner_if_asleep()
{
    pthread_cond_t *glb_cond_var = get_store()->get_pool_mgr()->
        get_glb_cond_var();

    pthread_cond_broadcast(glb_cond_var);
}
