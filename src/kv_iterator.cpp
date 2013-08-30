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
#include <stdlib.h>
#include <string.h>

#include "src/kv_store_mgr.h"
#include "src/kv_iterator.h"
#include "src/kv_wrappers.h"
#include "util/kv_buffer_pool.h"
#include "nvm_error.h"
using namespace nvm_wrapper;
//
//KV store device and other member variables are initialized
//
NVM_KV_Iterator::NVM_KV_Iterator(NVM_KV_Store *kv_store,
                                 uint32_t pool_bits, uint32_t max_pools,
                                 int all_poolid)
{
    m_p_kv_store  = kv_store;
    m_pool_bits = pool_bits;
    m_pool_mask = (1 << m_pool_bits) - 1;
    m_max_pools = max_pools;
    m_all_pool_id = all_poolid;
    m_initialized = false;
    pthread_mutex_init(&m_mtx_iter, NULL);
    for (int i = 0; i< M_NO_ITR; i++)
    {
        m_iter[i] = NULL;
    }
}
//
//
//
NVM_KV_Iterator::~NVM_KV_Iterator()
{
    pthread_mutex_destroy(&m_mtx_iter);
    for (int i = 0; i < M_NO_ITR; i++)
    {
        if (m_iter[i])
        {
            free(m_iter[i]);
        }
    }

}
///
///initialization of the NVM_KV_Iterator object after it's created
///
int NVM_KV_Iterator::initialize()
{
    int ret_code = NVM_SUCCESS;

    pthread_mutex_lock(&m_mtx_iter);
    if (m_initialized)
    {
        pthread_mutex_unlock(&m_mtx_iter);
        return ret_code;
    }
    if ((ret_code = m_buffer_pool.initialize(M_MAX_BUFFERS_IN_POOL,
         NVM_KV_Buffer_Pool::M_BUFFER_LIMIT,
         NVM_KV_Buffer_Pool::M_BUFFER_PAGE_ALIGN)) != NVM_SUCCESS)
    {
        pthread_mutex_unlock(&m_mtx_iter);
        return ret_code;
    }
    m_initialized = true;

    pthread_mutex_unlock(&m_mtx_iter);

    return ret_code;
}
//
//allocate iterator, set the iterator at first available location within
//iterator list
//
int NVM_KV_Iterator::alloc_iter(int iter_type)
{
    int ret_code = NVM_SUCCESS;
    kv_batch_iterator_t *batch_iter = NULL;
    nvm_kv_store_device_t *kv_device = NULL;

    kv_device = m_p_kv_store->get_store_device();
    batch_iter = (kv_batch_iterator_t *) malloc(sizeof(kv_batch_iterator_t)
                 + kv_device->capabilities.nvm_max_num_logical_iter_ranges
                 * sizeof(nvm_block_range_t));
    if (batch_iter == NULL)
    {
        ret_code =  -NVM_ERR_OUT_OF_MEMORY;
        goto end_alloc_iter;
    }

    if (iter_type == KV_REGULAR_ITER)
    {
        pthread_mutex_lock(&m_mtx_iter);
        for (int i = 0; i< NVM_KV_MAX_ITERATORS; i++)
        {
            if (m_iter[i] == NULL)
            {
                m_iter[i] = batch_iter;
                pthread_mutex_unlock(&m_mtx_iter);
                return i;
            }
        }
        pthread_mutex_unlock(&m_mtx_iter);
        ret_code = -NVM_ERR_MAX_LIMIT_REACHED;
    }
    else if (iter_type == KV_POOL_DEL_ITER)
    {
        if (m_iter[M_POOL_DEL_ITR])
        {
            ret_code =  -NVM_ERR_OBJECT_EXISTS;
            goto end_alloc_iter;
        }
        m_iter[M_POOL_DEL_ITR] = batch_iter;
        return M_POOL_DEL_ITR;
    }
    else if (iter_type == KV_ARB_EXP_ITER)
    {
        if (m_iter[M_ARB_EXP_ITR])
        {
            ret_code =  -NVM_ERR_OBJECT_EXISTS;
            goto end_alloc_iter;
        }
        m_iter[M_ARB_EXP_ITR] = batch_iter;
        return M_ARB_EXP_ITR;
    }
    else if (iter_type == KV_GLB_EXP_ITER)
    {
        if (m_iter[M_GLB_EXP_ITR])
        {
            ret_code =  -NVM_ERR_OBJECT_EXISTS;
            goto end_alloc_iter;
        }
        m_iter[M_GLB_EXP_ITR] = batch_iter;
        return M_GLB_EXP_ITR;
    }

end_alloc_iter:
    free (batch_iter);
    return ret_code;
}
//
//initializes iterator param every time this function is called
//
int NVM_KV_Iterator::init_iter(int it_id, int pool_id, uint64_t search_base,
                               uint64_t search_length, int iter_type,
                               uint32_t pool_hash)

{
    nvm_logical_range_iter_t *it = NULL;
    kv_batch_iterator_t *batch_iter = NULL;
    nvm_kv_store_device_t *kv_device = NULL;
    int ret_code = NVM_SUCCESS;

    kv_device = m_p_kv_store->get_store_device();

    ret_code = validate_iter(it_id, iter_type);
    if (ret_code < 0)
    {
        return ret_code;
    }

    batch_iter = m_iter[it_id];
    if (batch_iter == NULL)
    {
        return -NVM_ERR_INVALID_INPUT;
    }

    it = &(batch_iter->it);
    if (it == NULL)
    {
        return -NVM_ERR_INVALID_INPUT;
    }
    it->range_to_iterate.start_lba = search_base;
    it->range_to_iterate.length = search_length;
    it->max_ranges =
        kv_device->capabilities.nvm_max_num_logical_iter_ranges;
    it->ranges = (nvm_block_range_t *) (batch_iter + 1);

    //fill in the filter information
    it->filters.filter_pattern = pool_hash;
    if (pool_hash == 0)
    {
        //for zero pool hash don't apply filter
        //iterate over all keys
        it->filters.filter_mask = 0;
    }
    else
    {
        it->filters.filter_mask = m_pool_mask;
    }

    //set the pool id in the iterator
    batch_iter->pool_id = pool_id;
    batch_iter->pool_hash = pool_hash;
    batch_iter->pos = 0;
    batch_iter->num_ranges_found = 0;

    return NVM_SUCCESS;
}
//
//sets iterator to next valid position within a pool
//
int NVM_KV_Iterator::iter_over_pool(int it_id, int iter_type)
{
    int ret_code = NVM_SUCCESS;
    int pool_id = 0;
    uint32_t pool_hash = 0;
    int32_t found_pool_id = 0;
    uint32_t found_pool_hash = 0;
    uint64_t key_loc = 0;
    kv_batch_iterator_t *batch_iter = NULL;
    nvm_logical_range_iter_t *it = NULL;

    ret_code = validate_iter(it_id, iter_type);
    if (ret_code < 0)
    {
        return ret_code;
    }

    batch_iter = m_iter[it_id];
    if (batch_iter == NULL)
    {
        return -NVM_ERR_INVALID_INPUT;
    }
    pool_id = batch_iter->pool_id;
    pool_hash = batch_iter->pool_hash;
    it = &(batch_iter->it);
    if (it == NULL)
    {
        return -NVM_ERR_INVALID_INPUT;
    }

    //loop until we find a key from that belongs to  pool id,
    //or reach the end of the drive.
    while (1)
    {
        //increment iterator position
        ret_code = iterate(it_id, iter_type);
        if (ret_code < 0)
        {
            goto iter_over_pool_exit;
        }

        //get pool hash for current key/value
        key_loc = it->ranges[batch_iter->pos].start_lba;
        found_pool_hash = key_loc & m_pool_mask;

        //if hash match, check actual pool id if necessary
        if ((pool_hash == found_pool_hash) || (pool_id == m_all_pool_id))
        {
            //if max_pools <= 2K pools, hash == id, so we are done
            if (m_max_pools <= (1 << m_pool_bits))
            {
                break;
            }

            found_pool_id = get_poolid(key_loc);
            if (found_pool_id < 0)
            {
                ret_code = found_pool_id;
                goto iter_over_pool_exit;
            }

            //pool id matches, so done
            if (found_pool_id == pool_id)
            {
                break;
            }
        }
    }

iter_over_pool_exit:
    return ret_code;
}
//
//moves the given iterator forward one data range
//
int NVM_KV_Iterator::iterate(int it_id, int iter_type)
{
    int ret_code = 0;
    nvm_logical_range_iter_t *it = NULL;
    kv_batch_iterator_t *batch_iter = NULL;
    nvm_kv_store_device_t *kv_device = NULL;

    kv_device = m_p_kv_store->get_store_device();

    ret_code = validate_iter(it_id, iter_type);
    if (ret_code < 0)
    {
        return ret_code;
    }

    batch_iter = m_iter[it_id];

    if (batch_iter == NULL)
    {
        return -NVM_ERR_INVALID_INPUT;
    }

    it = &(batch_iter ->it);
    if (it == NULL)
    {
        return -NVM_ERR_INVALID_INPUT;
    }

    //Increment Iterator Position
    if (batch_iter->pos + 1 < batch_iter->num_ranges_found)
    {
        batch_iter->pos += 1;
    }
    else
    {
        if (iter_type == KV_GLB_EXP_ITER)
        {

            nvm_kv_store_metadata_t *metadata =
                                    m_p_kv_store->get_store_metadata();
            if (metadata == NULL)
            {
                return -NVM_ERR_INTERNAL_FAILURE;
            }
            if (metadata->global_expiry == 0)
            {
                return -NVM_ERR_INTERNAL_FAILURE;
            }

            //If a global expiry iterator, fill in the
            //expiry filter
            it->filters.filter_expiry = time(NULL) - metadata->global_expiry;
        }
        else
        {
            it->filters.filter_expiry = 0;
        }
        batch_iter->pos = 0;
        ret_code = nvm_logical_range_iterator(kv_device->nvm_handle, it);
        if (ret_code < 0)
        {
            return -NVM_ERR_INTERNAL_FAILURE;
        }
        //If we have reached the end of the drive
        if (ret_code == 0)
        {
            return -NVM_ERR_OBJECT_NOT_FOUND;
        }
        //return value of nvm_logical_range_iterator is number of ranges found
        batch_iter->num_ranges_found = ret_code;
    }

    return NVM_SUCCESS;
}
//
//fetch iterators position on the logical tree and also retrieve data length
//
int NVM_KV_Iterator::get_iter_loc(int it_id, uint64_t *lba, uint64_t *len,
                                  int iter_type)
{
    nvm_logical_range_iter_t *it = NULL;
    kv_batch_iterator_t *batch_iter = NULL;
    int ret_code = 0;

    if (!lba || !len)
    {
        return -NVM_ERR_INVALID_INPUT;
    }

    ret_code = validate_iter(it_id, iter_type);
    if (ret_code < 0)
    {
        return ret_code;
    }

    batch_iter = m_iter[it_id];
    if (batch_iter == NULL)
    {
        return -NVM_ERR_INVALID_INPUT;
    }

    it = &(batch_iter ->it);
    if (it == NULL)
    {
        return -NVM_ERR_INVALID_INPUT;
    }

    *lba = it->ranges[batch_iter->pos].start_lba;
    *len = it->ranges[batch_iter->pos].length;

    return NVM_SUCCESS;
}
//
//frees the iterator at given location
//
int NVM_KV_Iterator::free_iterator(int it_id, int iter_type)
{
    int ret_code = 0;

    if ((ret_code = validate_iter(it_id, iter_type)) < 0)
    {
        return ret_code;
    }
    pthread_mutex_lock(&m_mtx_iter);
    if (m_iter[it_id] != NULL)
    {
        free(m_iter[it_id]);
        m_iter[it_id] = NULL;
    }
    pthread_mutex_unlock(&m_mtx_iter);
    return ret_code;
}
//
//count the number of contiguous ranges on the media
//
int64_t NVM_KV_Iterator::count_ranges(uint64_t search_base,
                                      uint64_t search_length)
{
    int64_t ret_code = 0;
    nvm_logical_range_iter_t *it = NULL;
    nvm_kv_store_device_t *kv_device = NULL;
    int64_t num_ranges = 0;

    kv_device = m_p_kv_store->get_store_device();
    it = (nvm_logical_range_iter_t *) malloc(sizeof(nvm_logical_range_iter_t)
                 + kv_device->capabilities.nvm_max_num_logical_iter_ranges
                 * sizeof(nvm_block_range_t));
    if (it == NULL)
    {
        ret_code =  -NVM_ERR_OUT_OF_MEMORY;
        goto end_count_ranges;
    }

    it->range_to_iterate.start_lba = search_base;
    it->range_to_iterate.length = search_length;
    it->max_ranges =
        kv_device->capabilities.nvm_max_num_logical_iter_ranges;
    it->ranges = (nvm_block_range_t *) (it + 1);
    //don't apply filters, for counting iterate over all keys
    it->filters.filter_mask = 0;
    it->filters.filter_expiry = 0;

    do
    {
        ret_code = nvm_logical_range_iterator(kv_device->nvm_handle, it);
        if (ret_code < 0)
        {
            ret_code = -NVM_ERR_INTERNAL_FAILURE;
            goto end_count_ranges;
        }
        //return value of nvm_logical_range_iterator is number of ranges found
        num_ranges += ret_code;
    } while (ret_code);

    ret_code = num_ranges;

end_count_ranges:
    free (it);
    return ret_code;
}
//
//fetch exact pool id from flash at given location.
//
int NVM_KV_Iterator::get_poolid(uint64_t key_loc)
{
    int32_t ret_code = NVM_SUCCESS;
    char *buf = NULL;
    uint32_t ret_buf_size = 0;
    nvm_kv_header_t *hdr = NULL;
    nvm_kv_store_device_t *kv_device = NULL;
    int ret_read_len = 0;
    int pool_id = 0;
    nvm_iovec_t iovec; //num of IOVs will not exceed 1
    int iov_count = 1;
    uint32_t sector_size = 0;

    kv_device = m_p_kv_store->get_store_device();
    sector_size = kv_device->capabilities.nvm_sector_size;

    buf = m_buffer_pool.get_buf(sector_size, ret_buf_size);
    if ((ret_buf_size < sector_size ) || buf == NULL)
    {
        ret_code = -NVM_ERR_OUT_OF_MEMORY;
        goto kv_get_pool_id_exit;
    }

    hdr = (nvm_kv_header_t *) buf;
    iovec.iov_base = (uint64_t) buf;
    iovec.iov_len = sector_size;
    iovec.iov_lba = key_loc;
    ret_read_len = nvm_readv(kv_device, &iovec, iov_count);
    if (ret_read_len < 0)
    {
        ret_code = ret_read_len;
        goto kv_get_pool_id_exit;
    }

    pool_id = hdr->pool_id;
    ret_code = pool_id;

kv_get_pool_id_exit:
    if (ret_buf_size)
    {
        m_buffer_pool.release_buf(buf, ret_buf_size);
    }
    return ret_code;
}
//
//validates iterator id, if iterator type is regular iterator id
//should be with in 0 - NVM_KV_MAX_ITERATORS, if iterator type is other
//than regular iterator id can be M_POOL_DEL_ITR or M_ARB_EXP_ITR
//
int NVM_KV_Iterator::validate_iter(int it_id, int iter_type)
{
    if (iter_type == KV_REGULAR_ITER)
    {
        if (it_id < 0 || it_id >= NVM_KV_MAX_ITERATORS)
        {
            return -NVM_ERR_INVALID_INPUT;
        }
    }
    else if (iter_type == KV_ARB_EXP_ITER)
    {
        if (it_id != M_ARB_EXP_ITR)
        {
            return -NVM_ERR_INVALID_INPUT;
        }
    }
    else if (iter_type == KV_POOL_DEL_ITER)
    {
        if (it_id != M_POOL_DEL_ITR)
        {
            return -NVM_ERR_INVALID_INPUT;
        }
    }
    else
    {
        if (it_id != M_GLB_EXP_ITR)
        {
            return -NVM_ERR_INVALID_INPUT;
        }
    }

    return NVM_SUCCESS;
}
