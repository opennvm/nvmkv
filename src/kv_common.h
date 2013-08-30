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
#ifndef KV_COMMON_H_
#define KV_COMMON_H_
#include "util/kv_util.h"
#include "include/kv_macro.h"
#include <nvm_types.h>
#include <nvm_primitives.h>
#include <map>
#include <string>
#include <string.h>
using namespace std;

///
///KV store capabilities
///
typedef struct
{
    uint64_t nvm_max_num_iovs;               ///< max number of atomic IOVs
                                             ///< supported by the device
    uint64_t nvm_atomic_write_multiplicity;  ///< each atomic vector is a
                                             ///< multiple of this many bytes
    uint64_t nvm_max_write_size_per_iov;     ///< max size of an atomic write
                                             ///< vector in units of
                                             ///< nvm_atomic_write_multiplicity
    uint64_t nvm_max_trim_size_per_iov;      ///< max size of an atomic trim
                                             ///< vector in units of
                                             ///< nvm_atomic_write_multiplicity
    uint64_t nvm_sector_size;                ///< sector size in bytes
    uint64_t nvm_max_num_logical_iter_ranges;///< max number of logical
                                             ///< iterator ranges
} nvm_kv_store_capabilities_t;
///
///metadata associated with each key in KV store
///
typedef struct __attribute__((packed))
{
    uint64_t num_key;  ///< total number of keys in the KV store
    uint32_t gen_count;///< generation count
    uint32_t expiry;   ///< expiry in seconds associated with each key
} nvm_kv_key_metadata_t;

static_assert((sizeof(nvm_kv_key_metadata_t) % 8 == 0),
                    "nvm_kv_key_metadata_t is not byte aligned");
///
///metadata associated with entire KV store
///
typedef struct __attribute__((packed))
{
    uint64_t kv_store_id;   ///< KV store id
    uint64_t kv_store_stamp;///< unique id used for verification
    uint64_t num_keys;      ///< total number of keys on KV store
    uint32_t vsu_id;        ///< vsu id associated with KV store
    uint32_t version;       ///< customer store version
    uint32_t max_pools;     ///< maximum number of pools that can be created in
                            ///< KV store, this cannot be changed once KV store
                            ///< is created.
    uint32_t max_key_size;  ///< maximum key size supported
    uint32_t max_value_size;///< maximum value size supported
    uint32_t total_no_pools;///< total number of pools
    uint32_t kv_revision;   ///< KV store API revision number
    uint32_t expiry_mode;   ///< KV store expiry mode. Expected values are:
                            ///< KV_DISABLE_EXPIRY(0)   - Disable the expiry
                            ///< KV_ARBITRARY_EXPIRY(1) - Arbitrary expiry
                            ///< KV_GLOBAL_EXPIRY(2)    - Global expiry
    uint32_t global_expiry; ///< Global expiry value
    uint32_t reserved;      ///< reserved field for padding
} nvm_kv_store_metadata_t;

static_assert((sizeof(nvm_kv_store_metadata_t) % 8 == 0),
              "nvm_kv_store_metadata_t is not byte aligned");

///
///header associated with each key
///
typedef struct __attribute__((packed))
{
    uint32_t metadata_len;         ///< length of KV key metadata in bytes
    uint32_t key_len;              ///< length of the key in bytes
    uint32_t value_len;            ///< length of the value in bytes
    uint32_t value_offset;         ///< exact offset of value within entire
                                   ///< KV pair in bytes
    uint32_t pool_id;              ///< pool id to which KV pair belong
    uint32_t reserved;             ///< reserved field for padding
    nvm_kv_key_metadata_t metadata;///< metadata associated with the KV pair
} nvm_kv_header_t;

static_assert((sizeof(nvm_kv_header_t) % 8 == 0),
              "nvm_kv_header_t is not byte aligned");

///
///stores fd and other device related information in memory
///
typedef struct
{
    uint32_t fd;                             ///< device fd
    nvm_handle_t nvm_handle;                 ///< NVM Handle to invoke
                                             ///< primitives
    nvm_kv_store_capabilities_t capabilities;///< KV store capabilities
} nvm_kv_store_device_t;

static_assert((sizeof(nvm_kv_store_device_t) % 8 == 0),
              "nvm_kv_store_device_t is not byte aligned");
///
///entry to queue used for asynchronous deletes,
///stores maximum number of IOVs which media can support
///
typedef struct
{
    uint32_t iov_count;         ///< number of IOVs in iovec_entry
    uint32_t reserved;          ///< reserved field for padding
    nvm_iovec_t* iovec_entry;   ///< array of IOVs
} nvm_iovec_block_t;

///
///enum declarations for all KV API's
///
enum nvm_kv_api
{
    KVCREATE,
    KVPUT,
    KVGET,
    KVDELETE,
    KVPOOLCREATE,
    KVGETPOOLINFO,
    KVPOOLDELETE,
    KVDELETEALL,
    KVCLOSE,
    KVEXISTS,
    KVBATCHPUT,
    KVBEGIN,
    KVNEXT,
    KVGETCURRENT,
    KVITEREND,
    KVGETSTOREINFO,
    KVGETKEYINFO,
    KVDELETEWRAPPER,
    KVGETVALLEN,
    KVSETEXPIRY,
    KVGETEXPIRY,
    KVGETPOOLMETADATA
};
///
///A private wrapper around nvm_logical_range_iter_t that allows tracking
///of current position in batch fetched iterator requests
///
typedef struct
{
    uint32_t pool_id;           ///< pool for which this iterator is created
    uint32_t pool_hash;         ///< pool hash for the given pool_id
    uint64_t num_ranges_found;  ///< number of ranges found by the iterator
    uint64_t pos;               ///< iterator position in fetched ranges
    nvm_logical_range_iter_t it;///< NVM iterator variable
} kv_batch_iterator_t;
///
///enum declaration for iterator types
///
typedef enum nvm_kv_iterator_type
{
    KV_REGULAR_ITER = 1,  ///< general KV store iterator
    KV_POOL_DEL_ITER, ///< pool deletion iterator
    KV_ARB_EXP_ITER,  ///< arbitrary expiry iterator
    KV_GLB_EXP_ITER   ///< global expiry iterator
} nvm_kv_iterator_type_t;
///
///enum used in checking which input parameter needs to be
///validated for KV APIs
///
typedef enum
{
    KV_VALIDATE_ID = 1,      ///< validate input id
    KV_VALIDATE_KEY = 2,     ///< validate all key related params
    KV_VALIDATE_VALUE = 4,   ///< validate all value related params
    KV_VALIDATE_BATCH = 8,   ///< validate all params related to batch APIs
    KV_VALIDATE_POOL_ID = 16 ///< validate pool id
} kv_validation_t;
///
///data structure type that holds pool tags
///
typedef map<string, int> kv_tags_map_t;
#endif //KV_COMMON_H_
