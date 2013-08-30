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
#include <sys/uio.h>
#include <unistd.h>
#include <stdlib.h>
#include <new>
#include "src/kv_wrappers.h"
using namespace nvm_wrapper;
//
//wrapper function which internally calls pwritev or
//nvm_batch_atomic_operations based on the config for writing on
//to ioMemory
//
int64_t nvm_wrapper::nvm_writev(nvm_kv_store_device_t *device,
                                nvm_iovec_t *iovec, int iov_count,
                                bool nvm_atomic_op)
{
    int64_t ret_code = NVM_SUCCESS;
    uint32_t fd = device->fd;
    nvm_handle_t handle = device->nvm_handle;

    if (nvm_atomic_op)
    {
        ret_code = nvm_batch_atomic_operations(handle, iovec, iov_count, 0);
    }
    else
    {
        uint64_t offset = 0;
        int first_vector = 0;
        struct iovec *iovs = NULL;

        if (iov_count > device->capabilities.nvm_max_num_iovs)
        {
            return -NVM_ERR_INVALID_INPUT;
        }

        iovs = new(std::nothrow)
            struct iovec[device->capabilities.nvm_max_num_iovs];
        if (iovs == NULL)
        {
            return -NVM_ERR_OUT_OF_MEMORY;
        }

        //first vector will have starting address from where data needs to be
        //written
        offset = iovec[first_vector].iov_lba *
                 device->capabilities.nvm_sector_size;
        for (int k = 0; k < iov_count; k++)
        {
            iovs[k].iov_base = (void *) iovec[k].iov_base;
            iovs[k].iov_len = iovec[k].iov_len;
        }
        ret_code = pwritev(fd, iovs, iov_count, offset);
        delete[] iovs;
    }

    if (ret_code < 0)
    {
        ret_code = -NVM_ERR_IO;
    }
    return ret_code;
}
//
//wrapper function which internally calls preadv
//
int64_t nvm_wrapper::nvm_readv(nvm_kv_store_device_t *device,
                               nvm_iovec_t *iovec,
                               int iov_count)
{
    int64_t ret_code = NVM_SUCCESS;
    uint64_t offset = 0;
    int first_vector = 0;
    struct iovec *iovs = NULL;

    if (iov_count > device->capabilities.nvm_max_num_iovs)
    {
        return -NVM_ERR_INVALID_INPUT;
    }

    iovs = new(std::nothrow)
        struct iovec[device->capabilities.nvm_max_num_iovs];
    if (iovs == NULL)
    {
        return -NVM_ERR_OUT_OF_MEMORY;
    }

    offset = iovec[first_vector].iov_lba *
        device->capabilities.nvm_sector_size;
    for (int k = 0; k < iov_count; k++)
    {
        //check if buffer memory is sector aligned if not
        //return error since preadv returns error if
        //memory is not sector aligned
        if (iovec[k].iov_base &
                (device->capabilities.nvm_sector_size -1))
        {
            delete[] iovs;
            return -NVM_ERR_MEMORY_NOT_ALIGNED;
        }
        iovs[k].iov_base = (void *) iovec[k].iov_base;
        iovs[k].iov_len = iovec[k].iov_len;
    }
    ret_code = preadv(device->fd, iovs, iov_count, offset);
    delete[] iovs;

    if (ret_code < 0)
    {
        ret_code = -NVM_ERR_IO;
    }
    return ret_code;
}
