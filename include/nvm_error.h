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
/// \file nvm_error.h
/// \brief NVMKV Error Codes Header File.

#ifndef NVM_ERROR_H_
#define NVM_ERROR_H_

#ifdef __cplusplus
extern "C" {
#endif

///
/// NVM Error Codes
///
typedef enum
{
    NVM_SUCCESS = 0,                           ///< SUCCESS
    NVM_ERR_INVALID_HANDLE = 1,                ///< INVALID HANDLE
    NVM_ERR_DEVICE_NOT_FOUND = 2,              ///< DEVICE NOT FOUND
    NVM_ERR_OUT_OF_MEMORY = 6,                 ///< OUT OF MEMORY
    NVM_ERR_INTERNAL_FAILURE = 11,             ///< INTERNAL FAILURE
    NVM_ERR_CAPABILITIES_NOT_SUPPORTED = 17,   ///< CAPABILITY NOT SUPPORTED
    NVM_ERR_NOT_IMPLEMENTED = 18,              ///< NOT IMPLEMENTED
    NVM_ERR_FEATURE_NOT_SUPPORTED = 19,        ///< FEATURE NOT SUPPORTED
    NVM_ERR_OPERATION_NOT_SUPPORTED = 25,      ///< OPERATION NOT SUPPORTED
    NVM_ERR_OBJECT_EXISTS = 26,                ///< OBJECT EXISTS
    NVM_ERR_IO = 35,                           ///< IO ERROR
    NVM_ERR_INVALID_INPUT = 36,                ///< INVALID INPUT
    NVM_ERR_OBJECT_NOT_FOUND = 85,             ///< OBJECT NOT FOUND
    NVM_ERR_MEMORY_NOT_ALIGNED = 86,           ///< MEMORY NOT ALIGNED
    NVM_ERR_MAX_LIMIT_REACHED = 87,            ///< MAX LIMIT REACHED
} nvm_error_code_t;

#ifdef __cplusplus
}
#endif

#endif //NVM_ERROR_H_
