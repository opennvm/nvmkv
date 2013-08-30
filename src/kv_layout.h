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
#ifndef KV_LAYOUT_H_
#define KV_LAYOUT_H_

#include <stdint.h>
#include "src/kv_common.h"

///
///This class holds data regarding Logical layout of the KV store
///

//
//                   +-----------------------+ <- start
//                   | kv_store_metadata_t   |
//                   +-----------------------+ <- end of 1 sector
//                   | pool_bitmap_in_use    |
//                   +-----------------------+
//                   | pool_bitmap_deleted   |
//                   +-----------------------+ <-4096 sector
//                   | pool tags             |
//                   +-----------------------+
//                   |          .            |
//                   |          .            |
//                   +-----------------------+
//                   | KV store user data    |
//                   |          .            |
//                   |          .            |
//                   |          .            |
//                   +-----------------------+ <- end
//
class NVM_KV_Layout
{
    public:
        ///
        ///initializes layout data like key bits, value bits
        ///
        NVM_KV_Layout(uint32_t sector_size, uint32_t sparse_addr_bits);
        ///
        ///destructor
        ///
        ~NVM_KV_Layout();
        ///
        ///sets pool deleted bitmap LBA
        ///
        ///@param[in] lba deleted bitmap LBA
        ///
        ///@return        none
        ///
        void set_del_bm_lba(uint32_t lba);
        ///
        ///gets LBA for KV store metadata
        ///
        ///@return   returns KV store metadata LBA
        ///
        uint64_t get_metadata_lba();
        ///
        ///gets LBA for pool in-use bitmap
        ///
        ///@return    returns pool in-use bitmap LBA
        ///
        uint64_t get_inuse_bm_lba();
        ///
        ///gets LBA for pool deleted bitmap
        ///
        ///@return    returns pool deleted bitmap LBA
        ///
        uint64_t get_del_bm_lba();
        ///
        ///gets LBA for pool tags on media
        ///
        ///@return    returns pool tags LBA
        ///
        uint64_t get_pool_tags_lba();
        ///
        ///gets user data start LBA
        ///
        ///@return      returns user data start LBA
        ///
        uint64_t get_data_start_lba();
        ///
        ///gets number of bits that represents pools
        ///
        ///@return      returns number of bits
        ///
        uint32_t get_pool_bits();
        ///
        ///gets number of bits that represents value
        ///
        ///@return      returns number of bits
        ///
        uint32_t get_val_bits();
        ///
        ///gets number of bits that represents key
        ///
        ///@return       returns number of bits
        ///
        uint32_t get_key_bits();
        ///
        ///gets number of bits that represents sector size
        ///
        ///@return       returns number of bits that represents sector size
        ///
        uint32_t get_sect_bits();
        ///
        ///gets maximum value range
        ///
        ///@return      returns maximum value range
        ///
        uint64_t get_max_val_range();
        ///
        ///gets maximum search range in sectors
        ///
        ///@return     returns search range in sectors
        ///
        uint32_t get_max_search_range();
        ///
        ///gets magic number used for verification
        ///
        ///@return     - 64 bit stamp number
        ///
        uint64_t get_kv_stamp();
        ///
        ///gets KV store size
        ///
        ///@return    returns KV store size
        ///
        uint64_t get_kv_len();
        ///
        ///sets the number of records that will be used for metadata, including
        ///KV store metadata, pool bitmaps, pool tags
        ///
        ///@param[in] md_recs  number of records that is used for metadata
        ///
        ///@return             none
        ///
        void set_md_rec(uint64_t md_recs);

    private:
        static const uint64_t M_KV_METADATA_LBA = 0;         ///< KV store metadata LBA
        static const uint64_t M_KV_MAX_VALUE_RANGE = 2097152;///< maximum value size
        static const uint64_t M_KV_STAMP = 858623104131;     ///< magic number used for KV store verification

        uint64_t m_inUseBMLba;        ///< LBA for pool in-use bitmap
        uint64_t m_delBMLba;          ///< LBA for pool deleted bitmap
        uint64_t m_poolTagsLba;       ///< LBA associated with pool tags
        uint64_t m_kvUserDataStartLba;///< LBA pointing to start of user data
        uint32_t m_sectorSize;        ///< sector size
        uint32_t m_sectBits;          ///< bits representing sector size
        uint32_t m_valueBits;         ///< LSB of LBA that represents value
        uint32_t m_keyBits;           ///< MSB of LBA that represents key
        uint64_t m_maxValueSize;      ///< maximum value size
        uint32_t m_poolBits;          ///< LSB of LBA that represents pools
        uint64_t m_kv_len;            ///< total size of KV store
        uint64_t m_mdRecs;            ///< number of starting records that stores metadata
};

#endif //KV_LAYOUT_H_
