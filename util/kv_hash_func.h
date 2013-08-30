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
#ifndef KV_HASH_FUNC_H_
#define KV_HASH_FUNC_H_

#include <stdint.h>
#include "include/nvm_error.h"
#include "include/kv_macro.h"
///
///enum for all hash types
///
typedef enum
{
    FNV1A,
} hashtype_t;
///
///enum for all collision handling techniques
///
typedef enum
{
    SEQUENTIAL_PROBE,
    POLYNOMIAL_PROBE,
} coltype_t;
///
///class that maintains definitions of hash
///functions FNV1a.
///
class NVM_KV_Hash_Func
{
    public:
        ///
        ///initializes member variables with default values
        ///
        NVM_KV_Hash_Func();
        ///
        ///initializes hash function type and collision handling
        ///
        ///@param[in] hash_type  type of the hash function that will be called
        ///                      by key_hash member function in order to
        ///                      calculate hash
        ///@param[in] col_type   type of collision resolution technique that
        ///                      will be called by resolve_collision method.
        ///@param[in] hash_len   xor fold 64 bit hash value to number of bits
        ///                      specified by hash_len
        ///@param[in] align_bits if align is true then align hash into
        ///                      m_alignmentAddr
        ///@param[in] slice_addr if the key hashed to slice address,
        ///                      redistribute the keys to different address,
        ///                      this is done to carve out first couple of LBAs
        ///                      to store metadata
        ///
        ///@return              0 on success or appropriate error code
        ///
        int initialize(hashtype_t hash_type, coltype_t col_type,
                       uint32_t align_bits, uint32_t slice_addr);
        ///
        ///hash value for a key is computed using hash function
        ///that is set during initialization
        ///
        ///@param[in] key      input key for which hash needs to be computed
        ///@param[in] key_len  size of the input key
        ///@param[in] pool_id  if non-zero pool id is used to calculate hash
        ///                    along with key
        ///@param[in] hash_len xor fold 64 bit hash value to number of bits
        ///                    specified by hash_len
        ///@param[in] align    if true align the hash according to
        ///                    m_alignmentAddr
        ///@return             returns appropriate error or
        ///                    64 bit hash value for the input key
        ///
        uint64_t key_hash(uint8_t *key, uint32_t key_len, uint32_t pool_id,
                          uint32_t hash_len, bool align);
        ///
        ///resolves collision, calculate next hash after
        ///collision to resolve collision
        ///
        ///@param[in] key       input key for which hash needs to be computed
        ///@param[in] key_len   size of the input key
        ///@param[in] pool_id   pool_id
        ///@param[in] prev_hash previous hash value which formed collision
        ///@param[in] ntimes    number of times collision resolutions is
        ///                     called on a key
        ///@param[in] hash_len  xor fold 64 bit hash value to number of bits
        ///                     specified by hash_len
        ///@param[in] align     if true align the hash according to
        ///                     m_alignmentAddr
        ///@return              returns zero on error or
        ///                     64 bit rehash value for the input key
        ///
        uint64_t resolve_coll(uint8_t *key, uint32_t key_len, uint32_t pool_id,
                              uint64_t prev_hash, uint32_t ntimes,
                              uint32_t hash_len, bool align);
        ///
        ///destroys instance of the class
        ///
        ~NVM_KV_Hash_Func();

    protected:
        hashtype_t m_hashFunType; ///< type of hash functions
        coltype_t m_colHandletype;///< type of collision handling method
        uint32_t m_alignmentAddr; ///< align hash obtained
        uint32_t m_sliceAddr;     ///< if key hashes to LBA within slice address redistribute the keys
        ///
        ///fnv1a hash function, info @ http://isthe.com/chongo/tech/comp/fnv/
        ///
        ///@param[in] key      input key for which hash needs to be computed
        ///@param[in] key_len  size of the input key
        ///@param[in] pool_id  if non-zero pool id is used to calculate hash
        ///                    along with key
        ///
        ///@return             64 bit hash value for the input key
        ///
        uint64_t fnv1a(uint8_t *key, uint32_t key_len, uint32_t pool_id);
        ///
        ///resolve collision by sequential probing technique
        ///
        ///@param[in] prev_hash  previous hash value that resulted in collision
        ///@param[in] ntimes     number of times collision occurred for a given
        ///                      key
        ///@param[in] align_left if non zero align the hash to align_left bits
        ///
        ///@return               hash value after applying sequential probing
        ///                      concept
        ///
        uint64_t sequential_probe(uint64_t prev_hash, uint32_t ntimes,
                                  uint32_t align_left);
        ///
        ///resolve collision by quadratic probing technique
        ///
        ///@param[in] prev_hash  previous hash value that resulted in collision
        ///@param[in] ntimes     number of times collision occurred for a given
        ///                      key
        ///@param[in] align_left if non zero align the hash to align_left bits
        ///
        ///@return               hash value after applying quadratic probing
        ///                      concept
        ///
        uint64_t polynom_probe(uint64_t prev_hash, uint32_t ntimes,
                               uint32_t align_left);

    private:
        static const unsigned long long M_FNV_BASIS = 14695981039346656037ULL;
                                                          ///< fnv1a magic number
        static const uint64_t M_FNV_PRIME = 1099511628211;///< magic number used by fnv1a
        static const uint32_t M_TINY_HASH_LEN = 32;       ///< tiny hash length
        //disbale copy constructor and assignment operator
        DISALLOW_COPY_AND_ASSIGN(NVM_KV_Hash_Func);
};
#endif //KV_HASH_FUNC_H_
