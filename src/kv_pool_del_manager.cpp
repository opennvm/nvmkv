//----------------------------------------------------------------------------
// NVMKV
// |- Copyright 2012-2014 Fusion-io, Inc.

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
#include "src/kv_iterator.h"

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
    if (m_pools_to_delete)
    {
        bitmap_free(m_pools_to_delete);
    }
}
//
//initializes the pool deletion manager
//
int NVM_KV_Pool_Del_Manager::initialize()
{
    int ret_code = NVM_SUCCESS;
    NVM_KV_Store *kv_store = get_store();
    nvm_kv_store_device_t *kv_device = kv_store->get_store_device();
    NVM_KV_Layout *kv_layout = kv_store->get_layout();
    uint32_t max_pools = kv_store->get_store_metadata()->max_pools;
    uint32_t sector_size =
            kv_store->get_store_device()->capabilities.nvm_sector_size;
    uint32_t pool_bits = kv_layout->get_pool_bits();
    kv_batch_iterator_t *batch_iter = NULL;

    if ((ret_code = NVM_KV_Scanner::initialize(KV_POOL_DEL_ITER))
        != NVM_SUCCESS)
    {
        return ret_code;
    }

    batch_iter =
        kv_store->get_iter()->get_iter(get_iter_id(), KV_POOL_DEL_ITER);
    m_iter = batch_iter->it;
    m_iter.max_ranges =
        kv_device->capabilities.nvm_max_num_logical_iter_ranges;
    m_iter.ranges = (nvm_block_range_t *) (batch_iter + 1);
    m_iter.reserved = 0;
    m_iter.filters.filter_mask = (1 << pool_bits) - 1;
    //disable the expiry
    m_iter.filters.filter_expiry = 0;

    m_pools_to_delete = bitmap_alloc_aligned(max_pools, sector_size);
    if (!m_pools_to_delete)
    {
        fprintf(stderr, "Error, pool in progress bitmap allocation failed!\n");
        return -NVM_ERR_OUT_OF_MEMORY;
    }

    if (max_pools > (1 << pool_bits))
    {
        m_validate_pool_id_on_media = true;
    }
    else
    {
        m_validate_pool_id_on_media = false;
    }

    m_usr_data_start_lba = kv_layout->get_data_start_lba();
    m_usr_data_max_lba = kv_layout->get_kv_len() - m_usr_data_start_lba;

    return ret_code;
}
//
//the routine for deleting keys in pool
//
void* NVM_KV_Pool_Del_Manager::start_thread()
{
    pthread_mutex_t *glb_mtx = get_store()->get_pool_mgr()->get_glb_mutex();
    NVM_KV_Store* kv_store = get_store();
    NVM_KV_Pool_Mgr* pool_mgr = kv_store->get_pool_mgr();
    bool delete_all_pools = false;
    int ret_code = NVM_SUCCESS;

    set_cancel_state();
    //register function that needs to be called on thread cancellation
    pthread_cleanup_push(&NVM_KV_Scanner::scanner_cancel_routine, get_mutex());
    pthread_cleanup_push(&NVM_KV_Scanner::scanner_cancel_routine, glb_mtx);
    while (true)
    {

        //cancellation point
        pthread_testcancel();
        //check if the pool deletion map is empty, if not
        //return from the wait else start waiting for
        //external trigger
        pthread_mutex_lock(get_mutex());
        while (!pool_mgr->has_pools_to_delete())
        {
            wait_for_trigger();
        }
        //copy out the pool deletion bitmap to m_pools_to_delete bitmap for
        //processing. This operation is protected by pool bitmap mutex.
        pool_mgr->get_pool_deletion_bitmap(m_pools_to_delete, delete_all_pools);
        pthread_mutex_unlock(get_mutex());

        //to yield to pool deletion manager, pool deletion status is set right
        //away
        pool_mgr->set_pool_del_status(true);

        //acquire global lock to synchronize with expiry manager
        pthread_mutex_lock(glb_mtx);
        if ((ret_code = start_pool_delete(delete_all_pools)) != NVM_SUCCESS)
        {
            fprintf(stderr, "Pool deletion encounter error: %d\n", ret_code);
        }

        pool_mgr->set_pool_del_status(false);

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
//go through m_pools_to_delete bitmap to delete each pool
//
int NVM_KV_Pool_Del_Manager::start_pool_delete(bool delete_all_pools)
{
    NVM_KV_Store *kv_store = get_store();
    uint32_t max_pools = kv_store->get_store_metadata()->max_pools;
    int ret_code = NVM_SUCCESS;

    if (delete_all_pools)
    {
        if ((ret_code = delete_pool(-1, m_validate_pool_id_on_media))
            != NVM_SUCCESS)
        {
            return ret_code;
        }
        if ((ret_code =
            kv_store->get_pool_mgr()->clear_pool_bitmaps(m_pools_to_delete))
            != NVM_SUCCESS)
        {
            return ret_code;
        }
     }
    else
    {
        for (uint32_t i = 1; i < max_pools; i++)
        {
            if (bitmap_test(m_pools_to_delete, i))
            {
                if ((ret_code = delete_pool(i, m_validate_pool_id_on_media))
                    != NVM_SUCCESS)
                {
                    return ret_code;
                }
                if ((ret_code =
                    kv_store->get_pool_mgr()->clear_pool_bitmaps(i))
                    != NVM_SUCCESS)
                {
                    return ret_code;
                }
            }
        }
    }

    return ret_code;
}
//
//delete all keys from the media for the given pool id
//
int NVM_KV_Pool_Del_Manager::delete_pool(int pool_id,
                                         bool validate_pool_id_on_media)
{
    uint64_t max_trim_size_per_iov = 0;
    uint64_t trim_len = 0;
    uint32_t num_iovs = 0;
    nvm_block_range_t *current_range = NULL;
    uint32_t num_ranges_found = 0;
    uint32_t num_ranges = 0;
    uint32_t found_pool_id;
    NVM_KV_Store *kv_store = get_store();
    nvm_kv_store_device_t *device = kv_store->get_store_device();
    uint32_t pool_bits = kv_store->get_layout()->get_pool_bits();
    uint32_t pool_mask = (1 << pool_bits) - 1;
    uint32_t pool_hash;
    uint32_t sector_size = device->capabilities.nvm_sector_size;
    int default_pool_id = kv_store->get_pool_mgr()->get_default_poolid();
    nvm_iovec_t *iovec = get_iovec();
    uint64_t iter_filter_mask = 0;
    int ret_code = NVM_SUCCESS;

    max_trim_size_per_iov =
        device->capabilities.nvm_atomic_write_multiplicity *
        device->capabilities.nvm_max_trim_size_per_iov;

    //setup the iterator parameters
    m_iter.range_to_iterate.start_lba = m_usr_data_start_lba;
    m_iter.range_to_iterate.length = m_usr_data_max_lba;
    if (pool_id == -1)
    {
        //if deleting all user created pools using pool id -1,
        //disable filter mask and filter pattern

        //save the old range iterator's filter mask value and
        //set the filter mask to 0 for now
        iter_filter_mask = m_iter.filters.filter_mask;
        m_iter.filters.filter_mask = 0;
        //set filter pattern to 0 as well
        m_iter.filters.filter_pattern = 0;
    }
    else
    {
        m_iter.filters.filter_pattern =
            kv_store->get_pool_mgr()->get_poolid_hash(pool_id);
    }

    //iterate through the whole user data area
    while (true)
    {
        ret_code = nvm_logical_range_iterator(device->nvm_handle, &m_iter);
        if (ret_code == -1)
        {
            ret_code = -NVM_ERR_INTERNAL_FAILURE;
            goto end_kv_delete_all_keys;
        }

        num_ranges_found = ret_code;
        if (num_ranges_found > 0)
        {
            num_ranges = 0;
            current_range = m_iter.ranges;
            while (num_ranges < num_ranges_found)
            {
                if (validate_pool_id_on_media)
                {
                    ret_code = is_valid_for_del(current_range->start_lba,
                                                &found_pool_id);
                    if (ret_code < 0)
                    {
                       goto end_kv_delete_all_keys;
                    }
                    else if ((pool_id != -1 && pool_id != found_pool_id) ||
                            (pool_id == -1 && found_pool_id == default_pool_id))
                    {
                        //if pool id is not -1, skip the key that does not
                        //belong to this pool
                        //or if deleting all user created pools using pool id
                        //-1, skip the key of the default pool
                        num_ranges++;
                        current_range++;
                        continue;
                    }
                }
                else
                {
                    //if deleting all user pools, skip the key belong to the
                    //default pool
                    if (pool_id == -1)
                    {
                        pool_hash = current_range->start_lba & pool_mask;
                        if (pool_hash == default_pool_id)
                        {
                            num_ranges++;
                            current_range++;
                            continue;
                        }
                    }
                }

                //check if trim length is greater than max_trim_size_per_iov
                trim_len = current_range->length * sector_size;
                if (trim_len > max_trim_size_per_iov)
                {
                    fprintf(stderr, "Error: Corrupted key\n");
                    ret_code = -NVM_ERR_INTERNAL_FAILURE;
                    goto end_kv_delete_all_keys;
                }

                iovec[num_iovs].iov_base = 0;
                iovec[num_iovs].iov_len = trim_len;
                iovec[num_iovs].iov_lba = current_range->start_lba;
                iovec[num_iovs].iov_opcode = NVM_IOV_TRIM;
                num_iovs++;
                num_ranges++;
                current_range++;

                //batch delete the keys once the iovec has been filled
                if (num_iovs == device->capabilities.nvm_max_num_iovs)
                {
                    if ((ret_code = kv_store->batch_delete(iovec, num_iovs))
                        != NVM_SUCCESS)
                    {
                        goto end_kv_delete_all_keys;
                    }
                    num_iovs = 0;
                }
            }
        }

        if (num_ranges_found < m_iter.max_ranges)
        {
            if (num_iovs)
            {
                ret_code = kv_store->batch_delete(iovec, num_iovs);
            }
            goto end_kv_delete_all_keys;
        }
    }

end_kv_delete_all_keys:

    if (pool_id == -1)
    {
        //reset the range iterator's filter mask back to original
        m_iter.filters.filter_mask = iter_filter_mask;
    }

    return ret_code;
}
