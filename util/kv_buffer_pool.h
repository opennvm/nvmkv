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
#ifndef KV_BUFFER_POOL_H_
#define KV_BUFFER_POOL_H_

#include "include/kv_macro.h"
#include <vector>
#include <pthread.h>
#include <stdint.h>
///
///class that maintains pool of buffer, buffer pool is used to
///prevent calling malloc for each KV store API calls like kv_get and kv_put requests
///
class NVM_KV_Buffer_Pool
{
    public:
        static const uint32_t M_MAX_BUFFERS = 4096;
        static const uint32_t M_BUFFER_LIMIT = 4096;
        static const uint32_t M_BUFFER_PAGE_ALIGN = 4096;

        ///
        ///creates pthread mutex and thread condition variables
        ///
        NVM_KV_Buffer_Pool();
        ///
        ///destroys all pre-allocated buffer
        ///
        ~NVM_KV_Buffer_Pool();
        ///
        ///pre-allocates and initialize buffers within buffer pool
        ///
        ///@param[in] num_buffers   number of buffers that needs to be
        ///                         allocated in buffer pool
        ///@param[in] buffer_limit  size of each buffer in buffer pool
        ///@param[in] buffer_align  buffer is allocated with this alignmnet
        ///
        ///@return                  NVM_SUCCESS on successfully initializing
        ///                         buffers or return appropriate error code
        ///
        int initialize(uint32_t num_buffers, uint32_t buffer_limit,
                       uint32_t buffer_align);
        ///returns buffer from the pool if there exists a
        ///buffer with size equal to or greater than the requested size
        ///
        ///@param[in]  size     requested size of the buffer
        ///@param[out] ret_size o/p, actual size of buffer provided to the
        ///                     requester
        ///@return              pointer to the buffer
        ///
        char* get_buf(uint32_t size, uint32_t &ret_size);
        ///
        ///returns maximum size of the buffer
        ///
        ///@return  maximum size of he buffer
        ///
        uint32_t get_buf_size_limit()
        {
            return m_buffer_limit;
        }
        ///
        ///buffer released to the pool
        ///
        ///@param[in] buff address of the buffer that was released
        ///@param[in] size actual size of the buffer that is released
        ///@return         none
        ///
        void release_buf(char *buff, uint32_t size);

    private:
        //disbale copy constructor and assignment operator
        DISALLOW_COPY_AND_ASSIGN(NVM_KV_Buffer_Pool);
        ///
        ///delete all buffers in the pool
        ///
        void delete_all_buffers();

        pthread_mutex_t m_mtx;
        pthread_cond_t  m_condition_var;
        std::vector<char *> m_buffers;
        std::vector<uint32_t> m_buffer_sizes;
        uint32_t m_num_buffers;
        uint32_t m_buffer_limit;
        uint32_t m_buffer_alignment;
        bool m_initialized;
};
#endif //KV_BUFFER_POOL_H_
