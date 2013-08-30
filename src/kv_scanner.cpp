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
#include "src/kv_scanner.h"
#include "src/kv_pool_manager.h"
#include "src/kv_iterator.h"
#include "src/kv_wrappers.h"
#include "src/kv_layout.h"
#include "src/kv_store.h"
#include "util/kv_buffer_pool.h"
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
using namespace nvm_wrapper;
//
//assigns KV store member variable
//
NVM_KV_Scanner::NVM_KV_Scanner(int num_threads,
                               NVM_KV_Store *kv_store)
{
    m_iovec = NULL;
    m_kv_store = kv_store;
    pthread_mutex_init(&m_cond_mtx, NULL);
    pthread_cond_init(&m_cond_var, NULL);
    m_num_threads = num_threads;
    m_thread = NULL;
    m_iter_id = -1;
}
//
//destroys all scanner allocation
//
NVM_KV_Scanner::~NVM_KV_Scanner()
{
    pthread_cond_destroy(&m_cond_var);
    pthread_mutex_destroy(&m_cond_mtx);
    if(m_iter_id >= 0)
    {
        m_kv_store->get_iter()->free_iterator(m_iter_id, m_iter_type);
    }
    delete[] m_iovec;
    delete[] m_thread;
}
//
//does all necessary allocations and starts scanner
//
int NVM_KV_Scanner::initialize(int iter_type)
{
    //allocate iterator and iovs
    if (iter_type > 0)
    {
        int ret_code = 0;
        int max_iovs =
            m_kv_store->get_store_device()->capabilities.nvm_max_num_iovs;

        ret_code = m_kv_store->get_iter()->alloc_iter(iter_type);
        if (ret_code < 0)
        {
            return ret_code;
        }
        m_iter_id = ret_code;
        m_iter_type = iter_type;
        reset_iterator();

        //allocate array of vectors for batch discard
        m_iovec = new(std::nothrow) nvm_iovec_t[max_iovs];
        if (m_iovec == NULL)
        {
            return -NVM_ERR_OUT_OF_MEMORY;
        }
    }

    m_thread = new(std::nothrow) pthread_t[m_num_threads];
    if (m_thread == NULL)
    {
        return -NVM_ERR_OUT_OF_MEMORY;
    }
    for (int i = 0; i < m_num_threads; i++)
    {
        pthread_create(&m_thread[i], NULL,
                       &NVM_KV_Scanner::start_scanner_wrapper, this);
    }

    return NVM_SUCCESS;
}
//
//sets scanner cancel state, type
//
void NVM_KV_Scanner::set_cancel_state()
{
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
}
//
//wrapper around start_thread that is passed to pthread_create
//
void* NVM_KV_Scanner::start_scanner_wrapper(void *param)
{
    return ((NVM_KV_Scanner *) param)->start_thread();
}
//
//exits all threads spawned by the class
//
void NVM_KV_Scanner::cleanup_threads()
{
    //send wakeup for all threads
    pthread_mutex_lock(&m_cond_mtx);
    pthread_cond_broadcast(&m_cond_var);
    pthread_mutex_unlock(&m_cond_mtx);

    for (int i = 0; i < m_num_threads; i++)
    {
        pthread_cancel(m_thread[i]);
        pthread_join(m_thread[i], NULL);
    }
}
//
//cleanup routine that is called when scanner is cancelled
//
void NVM_KV_Scanner::scanner_cancel_routine(void *arg)
{
    pthread_mutex_t *mutex = (pthread_mutex_t *) arg;

    pthread_mutex_unlock(mutex);
    return;
}
//
//scans the drive and returns next key
//
int NVM_KV_Scanner::perform_scan(uint64_t *key_loc, uint64_t *read_len)
{
    int ret_code = 0;

    ret_code = m_kv_store->get_iter()->iterate(m_iter_id, m_iter_type);
    if (ret_code == NVM_SUCCESS)
    {
        ret_code = m_kv_store->get_iter()->get_iter_loc(m_iter_id, key_loc,
                                                        read_len, m_iter_type);
    }
    return ret_code;
}
//
//resets the iterator to the front of the device.
//
int NVM_KV_Scanner::reset_iterator()
{
    int retval = 0;
    uint64_t usr_data_lba = m_kv_store->get_layout()->get_data_start_lba();
    uint64_t MAX_VALID_SECTOR = m_kv_store->get_layout()->get_kv_len() -
                                usr_data_lba;
    int all_poolid = m_kv_store->get_pool_mgr()->get_all_poolid();
    retval = m_kv_store->get_iter()->init_iter(m_iter_id, all_poolid,
        usr_data_lba, MAX_VALID_SECTOR, m_iter_type, 0);
    return retval;
}
//
//checks if the key is expired and fill in pool id
//
int NVM_KV_Scanner::is_valid_for_del(uint64_t key_loc, uint32_t *pool_id)
{
    uint32_t sector_size = m_kv_store->get_sector_size();
    uint32_t ret_buf_size = 0;
    char *buf = NULL;
    uint32_t curtime = 0;
    int ret_code = 0;
    uint32_t expiry;
    int iov_cnt = 1;
    nvm_kv_header_t *hdr = NULL;
    nvm_iovec_t iovec;

    if ((ret_code = m_buffer_pool.initialize(M_MAX_BUFFERS_IN_POOL,
            sector_size, sector_size)) != NVM_SUCCESS)
    {
        return ret_code;
    }

    buf = m_buffer_pool.get_buf(sector_size, ret_buf_size);
    if (buf == NULL)
    {
        ret_code = -NVM_ERR_OUT_OF_MEMORY;
        goto key_del_exit;
    }

    //we just need to read the metadata of the key
    iovec.iov_base = (uint64_t)buf;
    iovec.iov_len = sector_size;
    iovec.iov_lba = key_loc;
    ret_code = nvm_readv(m_kv_store->get_store_device(),
                         &iovec, iov_cnt);
    if (ret_code < 0)
    {
        goto key_del_exit;
    }

    //we need to restore ret_code in order to return 0
    //or 1 for key to be / not to be a candidate for delete
    //as read may set ret_code to > 0 for successful reading
    //of bytes
    ret_code = NVM_SUCCESS;

    hdr = (nvm_kv_header_t *) buf;
    if ((hdr->key_len == 0) ||
        (hdr->key_len > NVM_KV_MAX_KEY_SIZE))
    {
        ret_code = NVM_SUCCESS;
        goto key_del_exit;
    }

    expiry = hdr->metadata.expiry;

    //check if the key is expired
    curtime = time(NULL);
    if ((expiry != 0) && (curtime > expiry))
    {
        ret_code = 1;
    }

    //store the pool id for scanner
    if (pool_id != NULL)
    {
        *pool_id = hdr->pool_id;
    }

key_del_exit:
    if (buf)
    {
        m_buffer_pool.release_buf(buf, ret_buf_size);
    }

    return ret_code;
}
//
//issues discard on keys
//
int NVM_KV_Scanner::batch_discard(nvm_iovec_t *iovec, uint32_t iov_count)
{
    nvm_kv_store_device_t *device = m_kv_store->get_store_device();
    int ret_code = 0;

    ret_code = nvm_writev(device, iovec, iov_count, true);
    if (ret_code != NVM_SUCCESS)
    {
        return -NVM_ERR_IO;
    }

    return ret_code;
}
//
//restarts scanner if it is sleeping
//on the conditional variable.
//
void NVM_KV_Scanner::restart_scanner_if_asleep()
{
    pthread_cond_broadcast(&m_cond_var);
    return;
}
//
//waits on a trigger
//
void NVM_KV_Scanner::wait_for_trigger()
{
    pthread_cond_wait(&m_cond_var, &m_cond_mtx);
    return;
}
//
//gets mutex assosiated with scanner
//
pthread_mutex_t* NVM_KV_Scanner::get_mutex()
{
    return &m_cond_mtx;
}
//
//gets scanner condition variable
//
pthread_cond_t* NVM_KV_Scanner::get_cond_var()
{
    return &m_cond_var;
}
//
//gets scanner buffer pool
//
NVM_KV_Buffer_Pool* NVM_KV_Scanner::get_buf_pool()
{
    return &m_buffer_pool;
}
//
//gets scanner iovec member variable
//
nvm_iovec_t* NVM_KV_Scanner::get_iovec()
{
    return m_iovec;
}
//
//gets kvstore object
//
NVM_KV_Store* NVM_KV_Scanner::get_store()
{
    return m_kv_store;
}
//
//gets iterator type
//
int NVM_KV_Scanner::get_iter_type()
{
    return m_iter_type;
}
//
//locks mutex
//
void NVM_KV_Scanner::lock_mutex()
{
    pthread_mutex_lock(&m_cond_mtx);
}
//
//unlocks mutex
//
void NVM_KV_Scanner::unlock_mutex()
{
    pthread_mutex_unlock(&m_cond_mtx);
}
