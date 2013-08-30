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
#include <stdint.h>
#include "util/kv_bitmap.h"
#include "src/kv_layout.h"
#include "util/kv_util.h"
//
//initializes layout data like key bits, value bits
//
NVM_KV_Layout::NVM_KV_Layout(uint32_t sector_size, uint32_t sparse_addr_bits)
{
    uint32_t max_value_in_sector = 0;
    m_valueBits = 0;
    m_keyBits = 0;
    m_sectBits = 0;

    m_sectorSize = sector_size;
    while ((sector_size = sector_size >> 1))
    {
        m_sectBits++;
    }

    m_inUseBMLba = (nvm_kv_round_upto_blk(sizeof(nvm_kv_store_metadata_t),
                                          m_sectorSize) / m_sectorSize);
    max_value_in_sector = (M_KV_MAX_VALUE_RANGE / m_sectorSize);
    m_poolTagsLba = max_value_in_sector;
    m_maxValueSize = (M_KV_MAX_VALUE_RANGE / 2) - m_sectorSize;
    while ((max_value_in_sector = max_value_in_sector >> 1))
    {
        m_valueBits++;
    }
    m_keyBits = (sparse_addr_bits - m_valueBits);
    m_poolBits = (m_valueBits - 1);
    m_kv_len = (1ULL << sparse_addr_bits);
}
//
//destructor
//
NVM_KV_Layout::~NVM_KV_Layout()
{
}
//
//sets the number of records that will be used for metadata, including
//KV store metadata, pool bitmaps, pool tags
//
void NVM_KV_Layout::set_md_rec(uint64_t md_recs)
{
    uint64_t md_secs = 0;

    m_mdRecs = md_recs;
    md_secs = (md_recs * M_KV_MAX_VALUE_RANGE) / m_sectorSize;
    m_kvUserDataStartLba = md_secs;
}
//
//sets pool deleted bitmap lba
//
void NVM_KV_Layout::set_del_bm_lba(uint32_t lba)
{
    m_delBMLba = lba;
}
//
//
//gets LBA for KV store metadata
//
uint64_t NVM_KV_Layout::get_metadata_lba()
{
    return M_KV_METADATA_LBA;
}
//
//gets LBA for pool in-use bitmap
//
uint64_t NVM_KV_Layout::get_inuse_bm_lba()
{
    return m_inUseBMLba;
}
//
//gets LBA for pool tags on media
//
uint64_t NVM_KV_Layout::get_pool_tags_lba()
{

    return m_poolTagsLba;
}
//
//gets LBA for pool deleted bitmap
//
uint64_t NVM_KV_Layout::get_del_bm_lba()
{
    return m_delBMLba;
}
//
//gets user data start LBA
//
uint64_t NVM_KV_Layout::get_data_start_lba()
{
    return m_kvUserDataStartLba;
}
//
//gets number of bits that represents pool
//
uint32_t NVM_KV_Layout::get_pool_bits()
{
    return m_poolBits;
}
//
//gets number of bits that represents value
//
uint32_t NVM_KV_Layout::get_val_bits()
{
    return m_valueBits;
}
//
//gets number of bits that represents sector size
//
uint32_t NVM_KV_Layout::get_sect_bits()
{
    return m_sectBits;
}
//
//gets number of bits that represents key
//
uint32_t NVM_KV_Layout::get_key_bits()
{
    return m_keyBits;
}
//
//gets maximum value range
//
uint64_t NVM_KV_Layout::get_max_val_range()
{
    return M_KV_MAX_VALUE_RANGE;
}
//
//gets maximum search range in terms of sectors
//
uint32_t NVM_KV_Layout::get_max_search_range()
{
    return (M_KV_MAX_VALUE_RANGE / m_sectorSize);
}
//
//gets magic number used for verification
//
uint64_t NVM_KV_Layout::get_kv_stamp()
{
    return M_KV_STAMP;
}
//
//gets size of KV store
//
uint64_t NVM_KV_Layout::get_kv_len()
{
    return m_kv_len;
}
