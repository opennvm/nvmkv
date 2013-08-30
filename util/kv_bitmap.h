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
#ifndef KV_BITMAP_H_
#define KV_BITMAP_H_

#include <stdint.h>

namespace kv_bitmap
{
    enum
    {
        BM_FIND_FIRST_CLEAR   = 1,
        BM_FIND_FIRST_SET      = 2,
        BM_FIND_FIRST_CLEAR_AND_SET = 3
    };

    typedef uint32_t kv_bitmap_t;
    ///
    ///gets sector in which the pool id belongs
    ///
    ///@param[in] pool_id     unique id of the pool
    ///@param[in] sector_bits number of bits representing sector of the device
    ///
    ///@return                sector number to which pool belongs
    ///
    uint32_t bit_index_sector(uint32_t pool_id, uint32_t sector_bits);
    ///
    ///allocates memory for bit map
    ///
    ///@param[in] nbits number of bits that needs to be allocated
    ///@return          return address of bitmap
    ///
    kv_bitmap_t* bitmap_alloc(uint32_t nbits);
    ///
    ///allocate memory for bit maps along with alignment
    ///
    ///@param[in] nbits     number of bits that needs to be allocated
    ///@param[in] alignment allocated memory needs to be aligned at this offset
    ///@return              return address of bitmap
    ///
    kv_bitmap_t* bitmap_alloc_aligned(uint32_t nbits, uint32_t alignment);
    ///
    ///deallocate memory allocated for bit map
    ///
    ///@param[in] pbm address of the bit map that needs to be freed
    ///@return        none
    ///
    void bitmap_free(kv_bitmap_t * pbm);
    ///
    ///test if the bit at given index is set
    ///
    ///@param[in] pbm   address of the bit map
    ///@param[in] index location of the bit within bit map
    ///@return          0 if the bit is unset or 1 if bit is set
    ///
    uint32_t bitmap_test(kv_bitmap_t *pbm, uint32_t index);
    ///
    ///bitmap_set - set bit at given index
    ///
    ///@param[in] pbm   address of the bit map
    ///@param[in] index location of the bit within bit map
    ///@return          none
    ///
    void bitmap_set(kv_bitmap_t *pbm, uint32_t index);
    ///
    ///clear bit at given index
    ///
    ///@param[in] pbm   address of the bit map
    ///@param[in] index location of the bit within bit map
    ///@return    none
    ///
    void bitmap_clear(kv_bitmap_t *pbm, uint32_t index);
    ///
    ///walk through the bitmap to find first cleared bit
    ///
    ///@param[in] pbm   address of bitmap
    ///@param[in] nbits number of bits in the bitmap
    ///@return          index of the first bit cleared on success or
    ///                 -1 if bitmap is full
    ///
    int bitmap_ffc(kv_bitmap_t *pbm, uint32_t nbits);
    ///
    ///walk through the bitmap to find first set bit
    ///
    ///@param[in] pbm   address of bitmap
    ///@param[in] nbits number of bits in the bitmap
    ///@return          index of the first bit set on success or -1 if bitmap
    ///                 is full
    ///
    int bitmap_ffs(kv_bitmap_t *pbm, uint32_t nbits);
    ///
    ///walk through the bitmap to find first available bit
    ///
    ///@param[in] pbm   address of bitmap
    ///@param[in] nbits number of bits in the bitmap
    ///@return          index of the first bit available on success or -1 if
    ///                 bitmap is full
    ///
    int bitmap_ffa(kv_bitmap_t *pbm, uint32_t nbits);
    ///
    ///error value when bitmap is full
    ///
    const int BM_FULL = -1;
}
#endif //KV_BITMAP_H_
