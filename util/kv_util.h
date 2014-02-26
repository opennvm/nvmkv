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
#ifndef KV_UTIL_H_
#define KV_UTIL_H_

#include <stdint.h>

///
///utility function used to round up value to multiple of blk_size, if value
///is not multiple of blk_size
///
///@param[in] value     input value that needs to be checked and rounded up
///@param[in] blk_size  block size to which value is rounded up
///@return              rounded up value, multiple of block size
///
inline uint64_t nvm_kv_round_upto_blk(uint64_t value, uint64_t blk_size)
{
    return ((value + (blk_size - 1)) & ~(blk_size - 1));
}
///
///utility function used to mask/clear lower order bits
///
///@param[in] value  input value that needs to be masked
///@param[in] lsb    number of lower order bits that needs to be masked
///@return           value obtained after masking/clearing lower order bits
///
inline uint64_t nvm_kv_mask_lsb(uint64_t value, uint32_t lsb)
{
    return (value & ~(((uint64_t)1 << lsb) - 1));
}
///
///function facilitates rounding of input to 32
///
///@param[in] value     input value that needs to be rounded
///@return              input rounded to 32
///
inline uint32_t round32(uint32_t value)
{
    uint32_t round32_mask = (1 << 5) - 1;
    return ((value + round32_mask) & ~round32_mask);
}
///
///function facilitates division by 8
///
///@param[in] value     input value that needs to be divided
///@return              input divided by 8
///
inline uint32_t bits_per_uint8(uint32_t value)
{
    return (value >> 3);
}
///
///function facilitates division by 32
///
///@param[in] value     input value that needs to be divided
///@return              input divided by 32
///
inline uint32_t bits_per_uint32(uint32_t value)
{
    return (value >> 5);
}
///
///function generates bit_mask to index into bit map
///
///@param[in] value     position to be referred to in the bit map
///@return              mask generated for indexing into bitmap
///
inline uint32_t bit_mask(uint32_t value)
{
    uint32_t bm_unit_size = 1 << 5;
    return (1 << (value % bm_unit_size));
}
#endif //KV_UTIL_H_
