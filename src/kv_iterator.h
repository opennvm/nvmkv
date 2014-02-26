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
#ifndef KV_ITERATOR_H
#define KV_ITERATOR_H

#include "src/kv_layout.h"
#include "src/kv_common.h"

///
///instance of this class represents iterator for a KV store.
///this class provides all functionality of iterator like:
///1. allocate iterator and initialize iterator params like
///   search_range and search_length
///2. advance the iterator to next position in logical range
///3. fetches lba and data length at the current iterator position
///4. free up iterator index
///
class NVM_KV_Iterator
{
    public:
        ///
        ///KV store and other member variable are initialized
        ///
        ///@param[in] kv_store    KV store object
        ///@param[in] pool_bits   lsb that represents pool in LBA
        ///@param[in] max_pools   maximum pools supported by KV store
        ///@param[in] all_poolid  pool id that represents all pools
        ///
        NVM_KV_Iterator(NVM_KV_Store *kv_store, uint32_t pool_bits,
                        uint32_t max_pools, int all_poolid);
        ///
        ///destroys all iterators with in KV store
        ///
        ~NVM_KV_Iterator();
	///
	///initialization of the NVM_KV_Iterator object after it's created
        ///
	///
	///@return returns 0 on success or appropriate error
	///
        int initialize();
	///
        ///allocate iterator, set the iterator at first available location
        ///within iterator list
        ///
        ///@param[in] iter_type  type of the iterator that needs to be
        ///                      allocated
        ///@param[in] pool_id    pool id to which iterator should be
        ///                      initialized
        ///@param[in] pool_hash  pool hash of the pool id, default is set
        ///                      to 0
        ///@return               returns index of the iterator within iterator
        ///                      list or returns error if the iterator list
        ///                      is full
        ///
        int alloc_iter(int iter_type, int pool_id, uint32_t pool_hash);
        ///
        ///initializes iterator param every time the function is called
        ///
        ///@param[in] it_id          index of the iterator with in iterator
        ///                          list
        ///@param[in] search_base    start location of the iterator
        ///@param[in] search_length  search length of the iterator
        ///@param[in] iter_type      type of the iterator, if it is regular
        ///                          one or iterator used by scanner
        ///@return                   returns 0 on success, or returns
        ///                          appropriate error code
        ///
        int init_iter(int it_id, uint64_t search_base, uint64_t search_length,
                      int iter_type);
        ///
        ///get the iterator object for the given iterator type and id
        ///
        ///@param[in] iter_id       unique identifier of iterator
        ///@param[in] iter_type     type of the iterator, if it is regular
        ///                         one or iterator used by scanner
        ///@return                  returns pointer to kv_batch_iterator_t
        ///                         object or NULL if not found
        ///
        kv_batch_iterator_t* get_iter(int iter_id, int iter_type);
        ///
        ///sets iterator to next location with in a pool
        ///
        ///@param[in] it_id         unique identifier of iterator
        ///@param[in] iter_type     type of the iterator, if it is regular
        ///                         one or iterator used by scanner
        ///@return                  returns 0 on success or error on failure
        ///
        int iter_over_pool(int it_id, int iter_type);
        ///
        ///increment the given iterator forward one position on logical tree
        ///
        ///@param[in] it_id     index of the iterator with in iterator
        ///                     list
        ///@param[in] iter_type type of the iterator, if it is regular
        ///                     one or iterator used by scanner
        ///@return              returns 0 on success, or returns appropriate
        ///                     error code
        ///
        int iterate(int it_id, int iter_type);
        ///
        ///fetch iterator position on the logical tree and also retrieve data
        ///length
        ///
        ///@param[in]  it_id     unique iterator id
        ///@param[out] loc       location of the iterator on logical tree(LBA)
        ///@param[out] len       length of the data written on media
        ///                      represented by LBA
        ///@param[in]  iter_type type of the iterator, if it is regular
        ///                      one or iterator used by scanner
        ///@return               return 0 on success or return appropriate error
        ///
        int get_iter_loc(int it_id, uint64_t *lba, uint64_t *len,
                         int iter_type);
        ///
        ///frees the iterator at index it_id in the iterator list
        ///
        ///@param[in] it_id     index of the iterator with in iterator
        ///                     list
        ///@param[in] iter_type type of the iterator, if it is regular
        ///                     one or iterator used by scanner
        ///@return              returns 0 on success or appropriate error
        ///
        int free_iterator(int it_id, int iter_type);
        ///
        ///count the number of contiguous ranges on the media
        ///
        ///@param[in] search_base    start location for the search
        ///@param[in] search_length  length to search
        ///@return                   returns number of contiguous ranges
        ///                          found with in the search length or
        ///                          returns appropriate error
        ///
        int64_t count_ranges(uint64_t search_base, uint64_t search_length);

    private:
        static const int M_NO_ITR = NVM_KV_MAX_ITERATORS + 3;  ///< iterators
                                                               ///< of KV store
        static const int M_POOL_DEL_ITR = NVM_KV_MAX_ITERATORS;///< pool
                                                               ///< deletion
                                                               ///< iterator
                                                               ///< index
        static const int M_ARB_EXP_ITR = M_POOL_DEL_ITR + 1;   ///< expiry
                                                               ///< iterator
                                                               ///< index
        static const int M_GLB_EXP_ITR = M_ARB_EXP_ITR + 1;    ///< global
                                                               ///< expiry
                                                               ///< iterator
                                                               ///< index
        static const uint32_t M_MAX_BUFFERS_IN_POOL = 128;     ///< max number of
                                                               ///< buffers in
                                                               ///< buffer pool


        //disable copy constructor and assignment operator
        DISALLOW_COPY_AND_ASSIGN(NVM_KV_Iterator);
        ///
        ///fetch pool id from media at a given location
        ///
        ///@param[in] key_loc      LBA of the entry
        ///@return                 returns pool_id on success, returns
        ///                        appropriate error code on error
        ///
        int get_poolid(uint64_t key_loc);
        ///
        ///validates iterator id, if iterator type is regular iterator id
        ///should be with in 0 - NVM_KV_MAX_ITERATORS, if iterator type is other
        ///than regular iterator id can be M_POOL_DEL_ITR or M_ARB_EXP_ITR
        ///
        ///@param[in] it_id      iterator id
        ///@param[in] iter_type  type of iterator
        ///@return               0 on valid iterator id or invalid error
        ///
        int validate_iter(int it_id, int iter_type);

        NVM_KV_Store *m_p_kv_store;           ///< KV store object
        kv_batch_iterator_t *m_iter[M_NO_ITR];///< all iterators of KV store,
                                              ///< includes scanner iterators
                                              ///< as well
        pthread_mutex_t m_mtx_iter;           ///< mutex for iterator list
        uint32_t m_pool_mask;                 ///< bit mask used to get pool
                                              ///< hash from lba
        uint32_t m_max_pools;                 ///< maximum pools supported by
                                              ///< KV store
        uint32_t m_pool_bits;                 ///< number of bits that
                                              ///< represents pools in lba
        uint32_t m_all_pool_id;               ///< id that represents all pools

        NVM_KV_Buffer_Pool m_buffer_pool;     ///< buffer pool
        bool m_initialized;                   ///< flag make sure the iterator
                                              ///< object is initialized only once
};
#endif //KV_ITERATOR_H
