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
#include "util/kv_buffer_pool.h"
#include "nvm_error.h"

//definition of static member variables
const uint32_t NVM_KV_Buffer_Pool::M_BUFFER_LIMIT;
//
//NVM_KV_Buffer_Pool class definitions
//

//
//creates pthread mutex and thread condition variables
//
NVM_KV_Buffer_Pool::NVM_KV_Buffer_Pool()
{
    pthread_mutex_init(&m_mtx, NULL);
    pthread_cond_init(&m_condition_var, NULL);
    m_initialized = false;
}
//
//destroys pre-allocated buffer
//
NVM_KV_Buffer_Pool::~NVM_KV_Buffer_Pool()
{
    delete_all_buffers();
    pthread_cond_destroy(&m_condition_var);
    pthread_mutex_destroy(&m_mtx);
}
//
//allocates and initializes buffers
//
int NVM_KV_Buffer_Pool::initialize(uint32_t num_buffers,
                                   uint32_t buffer_limit,
                                   uint32_t buffer_alignment)
{
    pthread_mutex_lock(&m_mtx);
    if (m_initialized)
    {
	pthread_mutex_unlock(&m_mtx);
        return NVM_SUCCESS;
    }

    if (num_buffers > M_MAX_BUFFERS)
    {
	pthread_mutex_unlock(&m_mtx);
        return -NVM_ERR_MAX_LIMIT_REACHED;
    }

    //allocate buffers with size buffer_limit
    //which are aligned to buffer_alignment
    for (int i = 0; i < num_buffers; ++i)
    {
        char *buffer = NULL;

        //pre-allocate buffer_alignment buffer
        if (posix_memalign((void **) &buffer, buffer_alignment,
                           buffer_limit) != 0)
        {
            // delete all buffers allocated so far
            delete_all_buffers();
	    pthread_mutex_unlock(&m_mtx);
            return -NVM_ERR_OUT_OF_MEMORY;
        }
        m_buffers.push_back(buffer);
        m_buffer_sizes.push_back(buffer_limit);
    }
    m_num_buffers = num_buffers;
    m_buffer_limit = buffer_limit;
    m_buffer_alignment = buffer_alignment;
    m_initialized = true;
    pthread_mutex_unlock(&m_mtx);

    return NVM_SUCCESS;
}
//
//returns buffer from the pool if there exists a buffer with
//size equal to or greater than the requested size
//
char* NVM_KV_Buffer_Pool::get_buf(uint32_t size, uint32_t &ret_size)
{
    char *buffer = NULL;
    char *new_buffer = NULL;
    int buffer_size = 0;

    pthread_mutex_lock(&m_mtx);

    while (m_buffers.empty())
    {
        pthread_cond_wait(&m_condition_var, &m_mtx);
    }
    buffer = m_buffers.back();
    buffer_size = m_buffer_sizes.back();

    if (buffer_size < size)
    {
        // Can't reuse that buffer, it's too small. Try to allocate a buffer
        if (posix_memalign((void **) &new_buffer, m_buffer_alignment, size) != 0)
        {
            ret_size = 0;
            pthread_mutex_unlock(&m_mtx);
            return NULL;
        }
        // If allocation succeeds, free the current buffer in pool
        if (buffer)
        {
            free(buffer);
            buffer = NULL;
        }

        buffer = new_buffer;
        buffer_size = size;
    }

    m_buffers.pop_back();
    m_buffer_sizes.pop_back();

    pthread_mutex_unlock(&m_mtx);

    ret_size = buffer_size;
    return buffer;
}
//
//buffer released to the pool
//
void NVM_KV_Buffer_Pool::release_buf(char *buff, uint32_t size)
{
    bool isEmpty = false;
    pthread_mutex_lock(&m_mtx);

    if (m_buffers.empty())
    {
        isEmpty = true;
    }
    m_buffers.push_back(buff);
    m_buffer_sizes.push_back(size);
    //signal only if buffer is empty
    //and is filled with one buffer now
    if (isEmpty)
    {
        pthread_cond_signal(&m_condition_var);
    }
    pthread_mutex_unlock(&m_mtx);
}

void NVM_KV_Buffer_Pool::delete_all_buffers()
{
    for (std::vector<char *>::iterator it = m_buffers.begin();
        it != m_buffers.end(); ++it)
    {
        if (*it != NULL)
        {
            free(*it);
        }
    }
}
