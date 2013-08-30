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
#include "util/kv_hash_func.h"
#include <stdio.h>
#include <stdlib.h>
//static member variables defination
//
//creates instance of the class and initializes
//member variables with default values
//
NVM_KV_Hash_Func::NVM_KV_Hash_Func()
{
    //default values
    m_hashFunType = FNV1A;
    m_colHandletype = SEQUENTIAL_PROBE;
    m_alignmentAddr = 0;
}
//
//destructor
//
NVM_KV_Hash_Func::~NVM_KV_Hash_Func()
{
}
//
//initializes hash function type, collision handling method
//
int NVM_KV_Hash_Func::initialize(hashtype_t hash_type, coltype_t col_type,
                                 uint32_t align_addr_bits,
                                 uint32_t slice_addr)
{
    m_hashFunType = hash_type;
    m_colHandletype = col_type;
    m_alignmentAddr = align_addr_bits;
    m_sliceAddr = slice_addr;

    return NVM_SUCCESS;
}
//
//hash for a key is computed based on the hash type set
//
uint64_t NVM_KV_Hash_Func::key_hash(uint8_t *key, uint32_t key_len,
                                    uint32_t pool_id, uint32_t hash_len,
                                    bool align)
{
    uint64_t hash = 0;

    if (key == NULL || key_len == 0)
    {
        return 0;
    }
    switch(m_hashFunType)
    {
        case FNV1A:
            hash = fnv1a(key, key_len, pool_id);
            break;
        default:
            return 0;
    }
    //generally hashing algorithms produce 32,64,128..power of 2 hashes
    //for hash value that are not power of two, xor fold to appropriate
    //bits
    if (hash_len)
    {
        if (hash_len > M_TINY_HASH_LEN)
        {
            hash = (hash >> hash_len) ^
                   (hash & (((uint64_t) 1 << hash_len) - 1));
        }
        else
        {
            hash = ((hash >> hash_len) ^ hash) &
                   (((uint64_t) 1 << hash_len) - 1);
        }
    }
    //converting to device/file address space
    if (align)
    {
        uint64_t align_left = m_alignmentAddr - hash_len;
        hash = (hash << align_left);
    }

    return hash;
}
//
//info on fnv1a @ http://isthe.com/chongo/tech/comp/fnv/
//
uint64_t NVM_KV_Hash_Func::fnv1a(uint8_t *key, uint32_t key_len,
                                 uint32_t pool_id)
{
    uint64_t h = M_FNV_BASIS;
    uint32_t len = key_len;

    while (len > 0)
    {
        h ^= (uint8_t) *key;
        h *= M_FNV_PRIME;
        key++;
        len--;
    }
    //calculate hash including pool_id
    if (pool_id)
    {
        uint8_t *pool_id_addr = (uint8_t *) &pool_id;
        uint32_t pool_id_len = sizeof(pool_id);
        while (pool_id_len > 0)
        {
            h ^= (uint8_t) *pool_id_addr;
            h *= M_FNV_PRIME;
            pool_id_addr++;
            pool_id_len--;
        }
    }
    //redistributing hash if the key hashes within slice_addr
    if (h < m_sliceAddr)
    {
        h = h + m_sliceAddr;
    }
    return h;
}
//
//resolves collision, calculate next hash after collision to
//resolve collision
//
uint64_t NVM_KV_Hash_Func::resolve_coll(uint8_t *key, uint32_t key_len,
                                        uint32_t pool_id, uint64_t prev_hash,
                                        uint32_t ntimes, uint32_t hash_len,
                                        bool align)
{
    uint64_t hash = 0;
    uint32_t align_left = 0;

    if (align)
    {
        align_left = m_alignmentAddr - hash_len;
    }

    switch(m_colHandletype)
    {
        case SEQUENTIAL_PROBE:
            hash = sequential_probe(prev_hash, ntimes, align_left);
            break;
        case POLYNOMIAL_PROBE:
            hash = polynom_probe(prev_hash, ntimes, align_left);
            break;
    }
    return hash;
}

//
//resolve collision by sequential probing technique
//
uint64_t NVM_KV_Hash_Func::sequential_probe(uint64_t prev_hash, uint32_t ntimes,
                                            uint32_t align_left)
{
    uint64_t hash = prev_hash +  ((uint64_t) ntimes << align_left);

    hash = hash % ((uint64_t) 1 << (m_alignmentAddr - align_left));
    return hash;
}
//
//resolve collision by quadratic probing technique
//
uint64_t NVM_KV_Hash_Func::polynom_probe(uint64_t prev_hash, uint32_t ntimes,
                                         uint32_t align_left)
{
    uint64_t hash = prev_hash + ( ((uint64_t) ntimes * ntimes) << align_left);

    hash = hash % ((uint64_t) 1 << (m_alignmentAddr - align_left));
    return hash;
}
