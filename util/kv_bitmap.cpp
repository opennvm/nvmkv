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
#include <new>
#include "util/kv_bitmap.h"
#include "src/kv_common.h"
#include "util/kv_util.h"
using namespace kv_bitmap;
//
//allocate memory of bit maps
//
kv_bitmap_t* kv_bitmap::bitmap_alloc(uint32_t nbits)
{
    kv_bitmap_t *pbm;

    pbm = (kv_bitmap_t *) calloc(bits_per_uint32(round32(nbits)),
                                 sizeof(kv_bitmap_t));
    return pbm;
}
//
//gets sector in which the pool id belongs
//
uint32_t kv_bitmap::bit_index_sector(uint32_t pool_id, uint32_t sector_bits)
{
    //considering bytes
    sector_bits += 3;

    return (pool_id >> sector_bits);
}

//
//allocate memory for bit maps which is sector aligned
//
kv_bitmap_t* kv_bitmap::bitmap_alloc_aligned(uint32_t nbits,
                                             uint32_t alignment)
{
    kv_bitmap_t *pbm = NULL;
    uint32_t size ;

    size = nvm_kv_round_upto_blk(bits_per_uint8(round32(nbits)), alignment);
    if (posix_memalign((void **) &pbm, alignment, size) != 0)
    {
        return pbm;
    }
    //initially all bits are free
    memset(pbm, 0, size);
    return pbm;
}
//
//deallocate memory allocated for bit map
//
void kv_bitmap::bitmap_free(kv_bitmap_t *pbm)
{
    free(pbm);
}
//
//set bit at given index
//
void kv_bitmap::bitmap_set(kv_bitmap_t *pbm, uint32_t index)
{
    pbm[bits_per_uint32(index)] |= bit_mask(index);
}
//
//clear bit at given index
//
void kv_bitmap::bitmap_clear(kv_bitmap_t *pbm, uint32_t index)
{
    pbm[bits_per_uint32(index)] &= ~bit_mask(index);
}
//
//test if the bit at given index is set
//
uint32_t kv_bitmap::bitmap_test(kv_bitmap_t *pbm, uint32_t index)
{
    return pbm[bits_per_uint32(index)] & bit_mask(index);
}
//
//walk the array of uint32_t checking 32bits at a time.
//
static int bitmap_helper(kv_bitmap_t *pbm, uint32_t nbits,
                         unsigned char action)
{
    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t mask = 0;
    uint32_t is_set = 0;
    uint32_t idx = 0;
    uint32_t bm_unit_size = 1 << 5;
    unsigned char done = 0;
    uint32_t bm_array_idx_full = 0xffffffff;

    for (i = 0; i < bits_per_uint32(round32(nbits)); i++)
    {
        if (pbm[i] != bm_array_idx_full)
        {
            for (j = 0; j < bm_unit_size; j++)
            {
                mask = bit_mask(j);
                is_set = pbm[i] & mask;

                if (action == BM_FIND_FIRST_CLEAR && !is_set)
                {
                    done = 1;
                    break;
                }
                else if (action == BM_FIND_FIRST_SET && is_set)
                {
                    done = 1;
                    break;
                }
                else if (action == BM_FIND_FIRST_CLEAR_AND_SET && !is_set)
                {
                    pbm[i] |= mask;
                    done = 1;
                    break;
                }
            }
            if (done)
            {
                break;
            }
        }
        else if (action == BM_FIND_FIRST_SET)
        {
            done = 1;
            break;
        }
    }

    idx = (i << 5) + j;
    return (done && (idx < nbits)) ? idx : BM_FULL;
}
//
//walk through the bitmap to find first cleared bit
//
int kv_bitmap::bitmap_ffc(kv_bitmap_t *pbm, uint32_t nbits)
{
    return bitmap_helper(pbm, nbits, BM_FIND_FIRST_CLEAR);
}
//
//walk through the bitmap to find first set bit
//
int kv_bitmap::bitmap_ffs(kv_bitmap_t *pbm, uint32_t nbits)
{
    return bitmap_helper(pbm, nbits, BM_FIND_FIRST_SET);
}
//
//walk through the bitmap to find first available bit
//
int kv_bitmap::bitmap_ffa(kv_bitmap_t *pbm, uint32_t nbits)
{
    return bitmap_helper(pbm, nbits, BM_FIND_FIRST_CLEAR_AND_SET);
}
