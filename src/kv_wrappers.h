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
#ifndef KV_WRAPPERS_H
#define KV_WRAPPERS_H

#include <stdint.h>
#include "src/kv_common.h"
#include "nvm_kv.h"
#include "nvm_error.h"
#include <nvm_types.h>
#include <nvm_primitives.h>

namespace nvm_wrapper
{
    ///
    ///wrapper function which internally calls pwritev or
    ///nvm_batch_atomic_operations based on the config for writing on
    ///to ioMemory
    ///
    ///@param[in] device          pointer to the device object
    ///@param[in] iovec           io vectors that holds vectored write data
    ///@param[in] iov_count       number of io vectors passed in
    ///@param[in] nvm_batch_op    if set to true nvm_batch_atomic_operations
    ///                           is called
    ///                           for writing else pwritev system call is made
    ///@return                    return 0 on success
    ///                           return error code on failure
    ///
    int64_t nvm_writev(nvm_kv_store_device_t *device,
                       nvm_iovec_t *iov, int iov_count,
                       bool nvm_batch_op);
    ///
    ///wrapper function which internally calls readv, could be enhanced to have
    ///nvm_read once supported
    ///
    ///@param[in]     kv_device     pointer to the device object
    ///@param[in,out] iovec         io vectors that holds data read
    ///@param[in]     iov_count     number of io vectors passed in
    ///@return                      return 0 on success
    ///                             return error code on failure
    ///
    int64_t nvm_readv(nvm_kv_store_device_t *device,
                      nvm_iovec_t *iov, int iov_count);
}
#endif //KV_WRAPPERS_H
