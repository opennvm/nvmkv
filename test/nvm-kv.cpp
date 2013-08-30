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
#define __STDC_LIMIT_MACROS
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
extern "C"
{
#include <getopt.h>
#include <fcntl.h>
}
#include "nvm-kv.h"
#include <libxml/xpath.h>
#include <libxml/tree.h>

//static member variable definition
KeyList* KeyList::m_pInstance = NULL;
int KeyList::m_incrCounter = 0;

const char KvOptions::M_KV_VERSION_STRING[M_MAX_STR_SIZE] = "1.0";

int KvWorkerThreads::m_job = KV_PUT;
pthread_t KvWorkerThreads::m_threads[] = {0};
KvOptions* KvWorkerThreads::m_pOptions = NULL;

uint64_t KvApi::m_timeDelta[M_MAX_THREADS] = {0};
int KvApi::m_kvId = 0;
uint32_t KvApi::m_sectorSize = 512;
std::vector<int> KvApi::m_poolIdsVector;
KvOptions KvApi::m_options;
//
//@main
//
int main (int argc, char *argv[])
{
    KvOptions options;
    int ret_code = 0;

    if (!options.parseCommandLine(argc, argv))
    {
        ret_code = -1;
        goto end_main;
    }
    KvApi::setOptions(options);
    if (options.m_functionalTest)
    {
        if (!KvTest::testFunctionality())
        {
            fprintf(stderr, "functionality test failed\n");
        }
    }
    if (options.m_smokeTest)
    {
        if (!KvTest::testSmoke(options))
        {
            fprintf(stderr, "smoke test failed\n");
        }
    }
    if (options.m_perfTest)
    {
        if (!KvTest::openKvContainer(options))
        {
            ret_code = -1;
            goto end_main;
        }
        KvTest::testPerf(options);
    }
end_main:
    if (options.m_perfTest)
    {
        KvTest::closeKvContainer(options);
    }
    return ret_code;
}
//
//@openKvContainer - Open the KV store container
//
bool KvTest::openKvContainer(KvOptions &options)
{
    int fd = 0;

    // Get FD for the device
    fd = open(options.m_deviceName, O_RDWR | O_DIRECT);

    if (fd == -1)
    {
        fprintf(stderr, "KvTest::openKvContainer, open failed %s\n",
                strerror(errno));
        return false;
    }

    options.m_fd = fd;

    return true;
}
//
//@closeKvContainer  - close the KV store container
//
void KvTest::closeKvContainer(KvOptions &options)
{
    if (options.m_fd != -1)
    {
        if (close(options.m_fd) < 0)
        {
            // Failed to close the block device
            fprintf(stderr, "KvTest::closeKvContainer, close failed: %s %d\n",
                    strerror(errno),
                    options.m_fd);
        }
    }
}
//
//@testPerf - perf testing of KV store APIs nvm_kv_get, nvm_kv_put.
//            functionality testing
//
void KvTest::testPerf(KvOptions &options)
{
    char *job = NULL;

    fprintf(stdout, "Executing performance tests...\n");

    // Generate keys
    KeyList::instance()->initialize(options.m_numIos, options.m_threads);
    fprintf(stdout, "Done Generating keys\n");

    //parse jobs string
    job = strtok(options.m_jobs, ",");
    if (job != NULL && strcmp(job, "kv_open"))
    {
        fprintf(stderr, "The first job must be \"kv_open\" in order to open "
		"the KV store.\n");
        return;
    }
    while (job != NULL)
    {
        KvWorkerThreads thread_inst;

        thread_inst.setOptions(&options);
        if (strcmp(job, "kv_open") == 0)
        {
            if (KvApi::testKvOpen(options.m_fd) < 0)
            {
                return;
            }
            if (options.m_noPools > 0)
            {
                if (KvApi::testPoolCreate() < 0)
                {
                    return;
                }
                fprintf(stdout, "Done creating pools\n");
                if (KvApi::testPoolTags(options.m_noPools) < 0)
                {
                    fprintf(stdout, "pool tag fetch failed\n");
                    return;
                }
            }
            //right after creating pool, pool status must be valid
            if (options.m_getPoolInfo)
            {
                if (KvApi::testGetAllPoolInfo() < 0)
                {
                    return;
                }
            }
            //try to set expiry after opening KV store, in case
            //expiry is set to global expiry mode
            if (options.m_expiry == KV_GLOBAL_EXPIRY)
            {
                if (KvApi::testSetExpiry() < 0)
                {
                    return;
                }
            }
        }
        if (strcmp(job, "kv_get") == 0)
        {
            fprintf(stdout, "Using a %d-thread-pool to test kv_get\n",
                    options.m_threads);
            thread_inst.createThreads(KV_GET);
            thread_inst.waitAndTerminateThreads();
            KvApi::printStats(KV_GET);
        }
        if (strcmp(job, "kv_get_store_info") == 0)
        {
            //prints KV store information
            if (KvApi::testGetKvStoreInfo() < 0)
            {
                return;
            }
        }
        if (strcmp(job, "kv_put") == 0)
        {
            fprintf(stdout, "Using a %d-thread-pool to test kv_put\n",
                    options.m_threads);
            thread_inst.createThreads(KV_PUT);
            thread_inst.waitAndTerminateThreads();
            KvApi::printStats(KV_PUT);
        }
        if (strcmp(job, "kv_exists") == 0)
        {
            fprintf(stdout, "Using a %d-thread-pool to test kv_exists\n",
                    options.m_threads);
            thread_inst.createThreads(KV_EXISTS);
            thread_inst.waitAndTerminateThreads();
            KvApi::printStats(KV_EXISTS);
        }
        if (strcmp(job, "kv_delete") == 0)
        {
            fprintf(stdout, "Using a %d-thread-pool to test kv_delete\n",
                    options.m_threads);
            thread_inst.createThreads(KV_DELETE);
            thread_inst.waitAndTerminateThreads();
            KvApi::printStats(KV_DELETE);
        }
        if (strcmp(job, "kv_iterate") == 0)
        {
            fprintf(stdout, "Using a %d-thread-pool to test kv_iterate\n",
                    options.m_threads);
            thread_inst.createThreads(KV_ITERATE);
            thread_inst.waitAndTerminateThreads();
            KvApi::printStats(KV_ITERATE);
        }
        if (strcmp(job, "kv_batch_put") == 0)
        {
            fprintf(stdout, "Using a %d-thread-pool to test kv_batch_put\n",
                    options.m_threads);
            thread_inst.createThreads(KV_BATCH_PUT);
            thread_inst.waitAndTerminateThreads();
            KvApi::printStats(KV_BATCH_PUT);
        }
        if (strcmp(job, "kv_close") == 0)
        {
            if (KvApi::testKvClose() < 0)
            {
                return;
            }
        }
        if (strcmp(job, "kv_delete_all") == 0)
        {
            if (KvApi::testDeleteAllKeys() < 0)
            {
                return;
            }
        }
        if (strcmp(job, "kv_pool_delete") == 0)
        {
            if (KvApi::testDeleteAllPools(0) < 0)
            {
                return;
            }
        }
        if (strcmp(job, "kv_pool_delete_all") == 0)
        {
            if (KvApi::testDeleteAllPools(1) < 0)
            {
                return;
            }
        }
        job = strtok(NULL, ",");
    } while (job != NULL);

    return;
}
//
//@testFunctionality - test all functionality of KV store
//
bool KvTest::testFunctionality()
{
    bool ret_val = true;

    return ret_val;
}
//
//@testSmoke - perform smoke test
//
bool KvTest::testSmoke(KvOptions &options)
{
    bool ret_val = true;
    int fd = 0;
    int max_pools = 1024; //Valid values are 1 - 1 Million
    int pool_id = 0;
    int sector_size = 0;
    unsigned int version = 0;
    int kv_id = 0;
    char *key_str = NULL;
    char *key_str2 = NULL;
    char *key = NULL;
    char *value_str = NULL;
    int ret = 0;
    int value_len = 0;
    nvm_kv_key_info_t key_info;
    int it_id = -1;
    int it_id_next = -1;
    int next_element = 0;
    uint32_t key_len = 9;
    nvm_kv_store_info_t store_info;
    nvm_kv_pool_info_t pool_info;
    nvm_kv_pool_tag_t tag;

    fprintf(stdout, "Executing smoke tests...\n");

    //Opening the underneath device
    fd = open(options.m_deviceName, O_RDWR | O_DIRECT);
    if (fd < 0)
    {
        fprintf(stderr, "could not open device %s errno %d\n",
                options.m_deviceName, errno);
        ret_val =  false;
        goto end_smoke;
    }
    fprintf(stdout, "Device id = %d\n", fd);
    // retrieve sector size using ioctl
    if( ioctl(fd, BLKPBSZGET, &sector_size) == -1 )
    {
        fprintf(stderr, "Error,failed to get sector size\n");
        ret_val =  false;
        goto end_smoke;
    }
    //Creating KV Store
    kv_id = nvm_kv_open(fd, version, max_pools, 0);
    if (kv_id < 0)
    {
        fprintf(stderr, "could not create KV store errno %d\n", errno);
        ret_val =  false;
        goto end_smoke;
    }
    fprintf(stdout, "successfully open KV store, kv_id = %d\n", kv_id);
    //retrieve KV store information
    ret = nvm_kv_get_store_info(kv_id, &store_info);
    if (ret < 0)
    {
        fprintf(stderr, "nvm_kv_get_store_info failed, errno %d\n",
                errno);
        ret_val =  false;
        goto end_smoke;
    }
    else
    {
        fprintf(stdout, "KV store information:\n");
        fprintf(stdout, "   version: %u\n", store_info.version);
        fprintf(stdout, "   num_pools: %u\n", store_info.num_pools);
        fprintf(stdout, "   max_pools: %u\n", store_info.max_pools);
        fprintf(stdout, "   expiry_mode: %u\n", store_info.expiry_mode);
        fprintf(stdout, "   num_keys: %lu\n", store_info.num_keys);
        fprintf(stdout, "   free_space: %lu\n", store_info.free_space);
        fprintf(stdout, "nvm_kv_get_store_info succeeded\n");
    }
    strcpy((char *) tag.pool_tag, "tag0");
    pool_id = nvm_kv_pool_create(kv_id, &tag);
    if (pool_id < 0)
    {
        fprintf(stderr, "could not create pool errno %d\n", errno);
        ret_val =  false;
        goto end_smoke;
    }
    if (KvApi::testPoolTags(1, kv_id) < 0)
    {
        ret_val = false;
        goto end_smoke;
    }
    fprintf(stdout, "successfully created pool with pool_id = %d\n", pool_id);

    key_str = new(std::nothrow) char[key_len];
    if (!key_str)
    {
        fprintf(stderr, "memory allocation failed\n");
        ret_val =  false;
        goto end_smoke;
    }
    key_str2 = new(std::nothrow) char[key_len];
    if (!key_str2)
    {
        fprintf(stderr, "memory allocation failed\n");
        ret_val =  false;
        goto end_smoke;
    }
    strcpy(key_str, "abc_test");
    strcpy(key_str2, "xyz_test");

    //Currently nvm_kv_put accepts sector aligned buffer
    //Allocating sector aligned buffer of size 512B;
    //value size could be anything greater than 0 and less than
    //1MiB - 1KiB (1MiB less 1KiB)
    value_len = sector_size;
    if (posix_memalign((void**) &value_str, sector_size,
                       value_len) != 0)
    {
        ret_val = false;
        goto end_smoke;
    }
    memset(value_str, 'a', value_len);

   //Valid values for key_len is 1 bytes to 128 bytes
   //Inserts the key into pool_id
   ret = nvm_kv_put(kv_id, pool_id, (nvm_kv_key_t *) key_str,
                    strlen((char *) key_str), value_str, value_len, 0, false,
                           0);
    if (ret < 0)
    {
        fprintf(stderr, "nvm_kv_put failed errno %d\n", errno);
        ret_val =  false;
        goto end_smoke;
    }
    fprintf(stdout, "nvm_kv_put succeeded\n");

    ret = nvm_kv_get_val_len(kv_id, pool_id, (nvm_kv_key_t *) key_str,
                             strlen((char *) key_str));
    if (ret < 0)
    {
        fprintf(stderr, "nvm_kv_get_val_len failed errno %d\n", errno);
        ret_val =  false;
        goto end_smoke;
    }

    fprintf(stdout, "nvm_kv_get_val_len returned value length as : %d bytes\n",
            ret);
    memset(value_str, '0', value_len);

    ret = nvm_kv_get(kv_id, pool_id, (nvm_kv_key_t *) key_str,
                     strlen((char *) key_str), value_str, value_len,
                     false, &key_info);
    if (ret < 0)
    {
        fprintf(stderr, "nvm_kv_get failed errno %d\n", errno);
        ret_val =  false;
        goto end_smoke;
    }
    fprintf(stdout, "nvm_kv_get succeeded\n");

    ret = nvm_kv_exists(kv_id, pool_id, (nvm_kv_key_t *) key_str,
                        strlen((char *) key_str), &key_info);
    if (ret == 0)
    {
        fprintf(stderr, "nvm_kv_exists: key doesn't exist, errno %d\n", errno);
        ret_val =  false;
        goto end_smoke;
    }
    fprintf(stdout, "nvm_kv_exists: key exists\n");

    ret = nvm_kv_put(kv_id, pool_id, (nvm_kv_key_t *) key_str2,
                strlen((char *) key_str2), value_str, value_len, 0, false, 0);
    if (ret < 0)
    {
        fprintf(stderr, "nvm_kv_put failed errno %d\n", errno);
        ret_val =  false;
        goto end_smoke;
    }
    fprintf(stdout, "nvm_kv_put succeeded\n");

    //KV Iterator
    it_id = nvm_kv_begin(kv_id, pool_id);
    if (it_id < 0)
    {
        fprintf(stderr, "nvm_kv_begin failed, errno %d\n", errno);
        ret_val =  false;
        goto end_smoke;
    }
    fprintf(stdout, "nvm_kv_begin succeeded\n");

    key = new(std::nothrow) char[strlen((char *) key_str)];
    if (!key)
    {
        fprintf(stderr, "memory allocation failed\n");
        ret_val =  false;
        goto end_smoke;
    }
    while (true)
    {
        ret = nvm_kv_get_current(kv_id, it_id, (nvm_kv_key_t *)key, &key_len,
                                 value_str, value_len, &key_info);
        if (ret < 0)
        {
            fprintf(stderr, "nvm_kv_get_current failed errno %d\n", errno);
            ret_val =  false;
            goto end_smoke;
        }
        fprintf(stdout, "nvm_kv_get_current succeeded\n");

        next_element = nvm_kv_next(kv_id, it_id);
        if (next_element != 0)
        {
            if (errno != -NVM_ERR_OBJECT_NOT_FOUND)
            {
                fprintf(stderr, "nvm_kv_next failed %d\n", errno);
                ret_val =  false;
                goto end_smoke;
            }
            break;
        }
        fprintf(stdout, "nvm_kv_next found an element\n");

    }
    ret = nvm_kv_iteration_end(kv_id, it_id);
    if (ret < 0)
    {
        fprintf(stderr, "nvm_kv_iteration_end failed errno %d\n", errno);
        ret_val =  false;
        goto end_smoke;
    }
    it_id_next = nvm_kv_begin(kv_id, pool_id);
    if (it_id < 0)
    {
        fprintf(stderr, "nvm_kv_begin failed, errno %d\n", errno);
        ret_val =  false;
        goto end_smoke;
    }
    //check if the iterator id is recycled
    if (it_id != it_id_next)
    {
        fprintf(stderr, "iterator id is not recycled\n");
        ret_val =  false;
        goto end_smoke;
    }

    //KV Delete
    ret = nvm_kv_delete(kv_id, pool_id, (nvm_kv_key_t *) key_str,
                        strlen((char *) key_str));
    if (ret < 0)
    {
        fprintf(stderr, "nvm_kv_delete: key deletion failed with errno %d\n",
                errno);
        ret_val =  false;
        goto end_smoke;
    }
    fprintf(stdout, "nvm_kv_delete: Deleted key successfully\n");

    ret = nvm_kv_get_pool_info(kv_id, pool_id, &pool_info);
    if (ret < 0)
    {
        fprintf(stderr, "nvm_kv_get_pool_info failed with errno %d\n", errno);
        ret_val =  false;
        goto end_smoke;
    }
    fprintf(stdout, "nvm_kv_get_pool_info succeeded: pool status = %d\n",
            pool_info.pool_status);

    ret = nvm_kv_pool_delete(kv_id, pool_id);
    if (ret < 0)
    {
        fprintf(stderr, "nvm_kv_pool_delete failed with errno %d\n", errno);
        ret_val =  false;
        goto end_smoke;
    }

    //Wait till pool is completely deleted
    while (1)
    {
        ret = nvm_kv_get_pool_info(kv_id, pool_id, &pool_info);
        if (ret < 0)
        {
            fprintf(stderr, "nvm_kv_get_pool_info failed with errno %d\n",
                    errno);
            ret_val =  false;
            goto end_smoke;
        }
        if (pool_info.pool_status == POOL_NOT_IN_USE)
        {
            fprintf(stdout, "pool deleted completely pool status = %d\n",
                    pool_info.pool_status);
            break;
        }
    }
    fprintf(stdout, "nvm_kv_pool_delete succeeded\n");

    //KV close
    ret = nvm_kv_close(kv_id);
    if (ret < 0)
    {
        fprintf(stderr, "nvm_kv_close failed with errno %d\n", errno);
        ret_val =  false;
        goto end_smoke;
    }
    fprintf(stdout, "nvm_kv_close succeeded\n");

end_smoke:
    delete[] key;
    free(value_str);
    delete[] key_str;
    delete[] key_str2;
    close(fd);
    return ret_val;
}
//
//member function definitions for KvApi
//

//
//@testKvOpen  - opens a kv store and vsl handle
//
int KvApi::testKvOpen(int fd)
{
    // retrieve sector size using ioctl
    if (ioctl(fd, BLKPBSZGET, &m_sectorSize) == -1)
    {
        fprintf(stderr, "Error,failed to get sector size\n");
        return -1;
    }

    m_kvId = nvm_kv_open(fd, m_options.m_apiVersion, m_options.m_maxPools,
                         m_options.m_expiry);
    if (m_kvId < 0)
    {
        fprintf(stderr, "Error, Initializing KV interface,"
                "nvm_kv_open failed with errno %d\n", errno);
        return -1;
    }
    initializePoolVector();

    return 0;

}
//
//@testKvClose  - closes KV store
//
int KvApi::testKvClose()
{
    if (nvm_kv_close(m_kvId) < 0)
    {
        fprintf(stderr, "nvm_kv_close failed errno %d\n", errno);
        return -1;
    }
    return 0;
}
//
//@testPoolCreate  - creates KV store pools
//
int KvApi::testPoolCreate()
{
    if (m_options.m_noPools == 0)
    {
        fprintf(stderr, "number of pools to be created is 0\n");
        return -1;
    }

    for (int k = 0; k < m_options.m_noPools; k++)
    {
        int pool_id = 0;
        char tempstr[16];
        nvm_kv_pool_tag_t tag;

        sprintf(tempstr, "tag%d", k);
        strcpy((char *) tag.pool_tag, tempstr);
        pool_id = nvm_kv_pool_create(m_kvId, &tag);
        if (pool_id == -1)
        {
            fprintf(stderr, "kv_pool_create failed, errno %d\n",
                    errno);
            return -1;
        }
        m_poolIdsVector[k] = pool_id;
    }
    return 0;
}
//
//@testPoolTags - fetches pool tags and make sure if the tags are correct
//
//
int KvApi::testPoolTags(int pool_count, int kv_id)
{
    int k = 0;
    int pool_id = 1;
    nvm_kv_pool_metadata_t *pool_md = NULL;

    if (kv_id == -1)
    {
        kv_id = m_kvId;
    }

    pool_md = new(std::nothrow) nvm_kv_pool_metadata_t[pool_count];
    if (!pool_md)
    {
        return -1;
    }
    if (nvm_kv_get_pool_metadata(kv_id, pool_md, pool_count, pool_id) < 0)
    {
        free (pool_md);
        return -1;
    }
    while (k < pool_count)
    {
        char tempstr[16];
        nvm_kv_pool_tag_t *tag = &pool_md[k].pool_tag;

        sprintf(tempstr, "tag%d", k);
        if (strcmp((char*) tag, tempstr))
        {
            fprintf(stderr, "pool tags are not same %s - %s\n",
                    (char *) tag, tempstr);
            free (pool_md);
            return -1;
        }
        k++;
    }
    delete[] pool_md;
    return 0;
}
//@testDeleteAllPools  - deletes all pools in the KV store
//
int KvApi::testDeleteAllPools(int oneTimeDeletion)
{
    int ret_code = 0;

    for (int k = 0; k < m_options.m_noPools; k++)
    {
        int pool_id = m_poolIdsVector[k];
        if (oneTimeDeletion)
        {
            //pool_id -1 means delete all pools
            pool_id = -1;
        }
        ret_code = testDeletePool(pool_id);
        if (ret_code < 0)
        {
            fprintf(stderr, "delete pool failed, errno %d\n",
                    errno);
            break;
        }
        if (oneTimeDeletion)
        {
            break;
        }
    }
    return ret_code;
}
//
//@testDeletePool  - deletes a pool in the KV store
//
int KvApi::testDeletePool(int pool_id)
{
    int ret_code = 0;

    ret_code = nvm_kv_pool_delete(m_kvId, pool_id);
    if (ret_code == -1)
    {
        fprintf(stderr, "nvm_kv_pool_delete failed, errno %d\n",
                errno);
    }
    while (1)
    {
        if (pool_id == -1)
        {
            pool_id = m_poolIdsVector[m_options.m_noPools - 1];
        }

        ret_code = testGetPoolInfo(pool_id);
        if (ret_code < 0)
        {
            return ret_code;
        }
        if (ret_code == POOL_NOT_IN_USE)
        {
            break;
        }
    }
    fprintf(stderr, "deleted keys in pool %d\n", pool_id);
    return 0;
}
//
//@testGetKvStoreInfo  - gets KV store info
//
int KvApi::testGetKvStoreInfo()
{
    int ret_code = 0;
    nvm_kv_store_info_t store_info;

    ret_code = nvm_kv_get_store_info(m_kvId, &store_info);
    if (ret_code == -1)
    {
        fprintf(stderr, "nvm_kv_get_store_info failed, errno %d\n",
                errno);
    }
    else
    {
        fprintf(stdout, "KV store information:\n");
        fprintf(stdout, "   version: %u\n", store_info.version);
        fprintf(stdout, "   num_pools: %u\n", store_info.num_pools);
        fprintf(stdout, "   max_pools: %u\n", store_info.max_pools);
        fprintf(stdout, "   expiry_mode: %u\n", store_info.expiry_mode);
        fprintf(stdout, "   global_expiry: %u\n", store_info.global_expiry);
        fprintf(stdout, "   num_keys: %lu\n", store_info.num_keys);
        fprintf(stdout, "   free_space: %lu\n", store_info.free_space);
    }
    return ret_code;
}
//
//@testGetAllPoolInfo  - prints pool info of all pools
//
int KvApi::testGetAllPoolInfo()
{
    for (int k = 0; k < m_options.m_noPools; k++)
    {
        int ret_code = 0;
        int pool_id = m_poolIdsVector[k];

        ret_code = testGetPoolInfo(pool_id);
        if (ret_code == -1)
        {
            fprintf(stderr, "nvm_kv_get_pool_info failed, errno %d\n",
                    errno);
            return ret_code;
        }
        fprintf(stdout, "pool %d\n status %d\n", pool_id, ret_code);
    }
    return 0;
}
//
//@testGetPoolInfo  - gets pool info of a pool
//
int KvApi::testGetPoolInfo(int pool_id)
{
    int ret_code = 0;
    nvm_kv_pool_info_t pool_info;

    ret_code = nvm_kv_get_pool_info(m_kvId, pool_id, &pool_info);
    if (ret_code == -1)
    {
        fprintf(stderr, "nvm_kv_get_pool_info failed, errno %d\n",
                errno);
        return ret_code;
    }
    return pool_info.pool_status;
}
//
//@testDeleteAllKeys  - delete all keys in a pool
//
int KvApi::testDeleteAllKeys()
{
    int ret_code = 0;

    ret_code = nvm_kv_delete_all(m_kvId);
    if (ret_code == -1)
    {
        fprintf(stderr, "nvm_kv_delete_all failed, errno %d\n",
                errno);
    }
    return ret_code;
}
//
//tests setting of global expiry
//
int KvApi::testSetExpiry()
{
    int ret_code = 0;

    ret_code = nvm_kv_set_global_expiry(m_kvId, m_options.m_expiryInSecs);
    if (ret_code < 0)
    {
        fprintf(stderr, "nvm_kv_set_global_expiry failed, errno %d\n", errno);
    }

    return ret_code;
}
//
//@testBasicApi - test kv_put, kv_get, kv_exists, kv_delete
//
int KvApi::testBasicApi(int job, int count, int index)
{
    nvm_kv_key_t *key = NULL;
    uint32_t key_len = 0;
    uint64_t start_time_local = 0;
    uint64_t end_time_local = 0;
    char test_char = 'a';
    void *value = NULL;
    int ret_code = 0;
    int error_count = 0;
    nvm_kv_key_info_t key_info;
    //TODO need to extend this to fit bytes and smaller values
    unsigned int value_size = m_options.m_valSize * m_sectorSize;
    int i = 0;

    KeyList::instance()->resetCounter();
    m_timeDelta[index] = 0;


    if (!m_options.m_sectorAligned && (job & KV_PUT))
    {
        value = malloc(value_size);
    }
    else
    {
        if (posix_memalign((void **) &value, m_sectorSize, value_size) != 0)
        {
            ret_code = -1;
            goto end_test_basic_api;
        }
    }
    if (value == NULL)
    {
        fprintf(stderr, "malloc failed\n");
        ret_code = -1;
        goto end_test_basic_api;
    }
    do
    {
        int pool_id = m_poolIdsVector[i];

        for (int j = 0; j < count; j++)
        {
            //get key at jth location within index vector
            key = KeyList::instance()->getKey(index, j, key_len);
            switch (job)
            {
                case KV_PUT:
                    memset(value, test_char, value_size);
                    start_time_local = getMicroTime();
                    ret_code = nvm_kv_put(m_kvId, pool_id, key, key_len, value,
                                          value_size, m_options.m_expiryInSecs,
                                          m_options.m_replacePuts,
                                          m_options.m_genCount);
                    end_time_local = getMicroTime();

                    if (ret_code != value_size)
                    {
                        fprintf(stderr, "kv_put failed, errno (%d)\n", errno);
                    }
                    break;
                case KV_GET:
                    memset(value, 0, value_size);
                    start_time_local = getMicroTime();
                    ret_code = nvm_kv_get(m_kvId, pool_id, key, key_len, value,
                                          value_size, m_options.m_readExact,
                                          &key_info);
                    end_time_local = getMicroTime();
                    if (ret_code != value_size)
                    {
                        if (errno == -NVM_ERR_OBJECT_NOT_FOUND)
                        {
                            fprintf(stderr, "kv_get failed, Key not found %d\n",
                                    ret_code);
                        }
                        else
                        {
                            fprintf(stderr, "kv_get failed, errno %d\n",
                                    ret_code);
                        }
                    }
                    // Don't bother verifying data if the kv_get failed
                    else if (m_options.m_verify)
                    {
                        error_count = KvApi::verifyBuffer((char *) value,
                                                           value_size,
                                                           test_char);
                        if (error_count)
                        {
                            ret_code = -1;
                            goto end_test_basic_api;
                        }
                    }
                    break;
                case KV_EXISTS:
                    start_time_local = getMicroTime();
                    ret_code = nvm_kv_exists(m_kvId, pool_id, key, key_len,
                                             &key_info);
                    end_time_local = getMicroTime();
                    if (errno != -NVM_ERR_OBJECT_NOT_FOUND && \
                            errno != NVM_SUCCESS)
                    {
                        fprintf(stderr, "kv_exist failed, errno %d\n", errno);
                    }
                    if (m_options.m_verify)
                    {
                        fprintf(stdout, "KEY_INFO -  pool-id:%d,"
                                "key length:%d ,""value length:%d,"
                                "expiry:%d, gen count:%d\n",
                                key_info.pool_id, key_info.key_len,
                                key_info.value_len, key_info.expiry,
                                key_info.gen_count);
                    }
                    break;
                case KV_DELETE:
                    start_time_local = getMicroTime();
                    ret_code = nvm_kv_delete(m_kvId, pool_id, key, key_len);
                    end_time_local = getMicroTime();
                    if (ret_code != NVM_SUCCESS)
                    {
                        fprintf(stderr, "kv_delete failed, errno (%d)\n",
                                errno);
                    }
                    break;
            }
            m_timeDelta[index] += end_time_local - \
                                  start_time_local;
            test_char++;
        }
        i++;
    } while (i < m_options.m_noPools);
end_test_basic_api:
    if (!error_count && m_options.m_verify && ((job & KV_GET)))
    {
        fprintf(stderr, "No error found verifying the value of the %d keys\n",
                m_options.m_numIos );
    }
    free(value);
    return ret_code;
}
//
//@testBatchApi - test kv_batch_put
//
int KvApi::testBatchApi(int job, int count, int index)
{
    int ret_code = 0;
    nvm_kv_key_t *key = NULL;
    uint64_t start_time_local = 0;
    uint64_t end_time_local = 0;
    uint32_t key_len = 0;
    char test_char = 'a';
    int i = 0;
    char *batch_buf = NULL; //buffer used for batch put operation
    nvm_kv_iovec_t *iov = NULL;
    uint32_t iovcnt = m_options.m_numIovs;
    unsigned int value_size = m_options.m_valSize * m_sectorSize;

    KeyList::instance()->resetCounter();
    m_timeDelta[index] = 0;
    iov = new(std::nothrow) nvm_kv_iovec_t[iovcnt];
    if (iov == NULL)
    {
        fprintf(stderr, "failed to create nvm_kv_iovec_t object\n");
        ret_code = -1;
        goto end_test_batch_api;
    }
    if (!m_options.m_sectorAligned && (job & KV_BATCH_PUT))
    {
        batch_buf = (char *) malloc(iovcnt * value_size);
    }
    else
    {
        if (posix_memalign((void **) &batch_buf, m_sectorSize,
                           iovcnt * value_size) != 0)
        {
            ret_code = -1;
            goto end_test_batch_api;
        }
    }
    if (batch_buf == NULL)
    {
        fprintf(stderr, "malloc failed\n");
        ret_code = -1;
        goto end_test_batch_api;
    }
    do
    {
        uint64_t key_index = 0;
        int pool_id = m_poolIdsVector[i];

        for (int j = 0; j < count; j++)
        {
            //get key at jth location within index vector
            for (int k = 0; k < iovcnt; k++, key_index++)
            {
                key = KeyList::instance()->getKey(index, key_index,
                                                  key_len);
                iov[k].key = key;
                iov[k].key_len = key_len;
                iov[k].value = (void *) (batch_buf + (k * value_size));
                iov[k].value_len = value_size;
                iov[k].replace = m_options.m_replacePuts;
            }
            switch (job)
            {
                case KV_BATCH_PUT:
                    memset(batch_buf, test_char, iovcnt * value_size);
                    start_time_local = getMicroTime();
                    ret_code = nvm_kv_batch_put(m_kvId, pool_id, iov, iovcnt);
                    end_time_local = getMicroTime();
                    if (ret_code < 0)
                    {
                        fprintf(stderr, "kv_batch_put failed, errno (%d)\n",
                                errno);
                    }
                    break;
            }
            m_timeDelta[index] += end_time_local - \
                                  start_time_local;
            test_char++;
        }
        i++;
    } while (i < m_options.m_noPools);

end_test_batch_api:

    delete[] iov;
    free(batch_buf);
    return ret_code;
}
//
//@testIterator - test kv_begin, kv_get_current, kv_next
//
int KvApi::testIterator(int index)
{
    int ret_code = 0;
    nvm_kv_key_t *key = NULL;
    uint32_t key_len = 0;
    uint64_t start_time_local;
    uint64_t end_time_local;
    void *value = NULL;
    void *value_cmp_buf = NULL;
    int it_id = -1;
    int i = 0;
    int next_element = 0;
    unsigned int value_size = m_options.m_valSize * m_sectorSize;
    nvm_kv_key_info_t key_info;
    uint64_t num_keys = (m_options.m_numIos / m_options.m_threads);

    //in multi threading case number of keys on the media is not same as
    //m_options.m_numIos, so number of keys is recalculated here based on
    //number of threads
    num_keys *= m_options.m_threads;

    m_timeDelta[index] = 0;
    if (posix_memalign((void **) &value, m_sectorSize, value_size) != 0)
    {
        fprintf(stderr, "malloc failed\n");
        ret_code = -1;
        goto end_test_iterator;
    }
    if (posix_memalign((void**) &value_cmp_buf, m_sectorSize,
                       value_size) != 0)
    {
        fprintf(stderr, "malloc failed\n");
        ret_code = -1;
        goto end_test_iterator;
    }
    key = new(std::nothrow) nvm_kv_key_t[M_MAX_KEY_SIZE];
    if (key == NULL)
    {
        fprintf(stderr, "failed to create nvm_kv_key_t object\n");
        ret_code = -1;
        goto end_test_iterator;
    }

    do
    {
        int count = 0;
        int pool_id = m_poolIdsVector[i];

        it_id = nvm_kv_begin(m_kvId, pool_id);
        if (it_id < 0)
        {
            fprintf(stderr, "kv_begin failed, errno %d\n", errno);
            ret_code = -1;
            goto end_test_iterator;
        }
        while (true)
        {
            count++;
            start_time_local = getMicroTime();
            ret_code = nvm_kv_get_current(m_kvId, it_id, key, &key_len, value,
                                          value_size, &key_info);
            end_time_local = getMicroTime();
            m_timeDelta[index] += end_time_local - \
                                  start_time_local;
            if (ret_code != value_size)
            {
                fprintf(stderr, "kv_get_current failed %d\n", errno);
                ret_code = -1;
                goto end_test_iterator;
            }
            if (m_options.m_verify)
            {
                ret_code = nvm_kv_get(m_kvId, pool_id, key, key_len,
                                      value_cmp_buf, value_size,
                                      false, &key_info);
                if (memcmp(value, value_cmp_buf, value_size) != 0)
                {
                    fprintf(stderr, "kv_get_current does not match \
                            kv_get!\n");
                    ret_code = -1;
                    goto end_test_iterator;
                }
            }
            start_time_local = getMicroTime();
            next_element = nvm_kv_next(m_kvId, it_id);
            end_time_local = getMicroTime();
            m_timeDelta[index] += end_time_local - \
                                  start_time_local;
            if (next_element != 0)
            {
                if (errno != -NVM_ERR_OBJECT_NOT_FOUND)
                {
                    fprintf(stderr, "kv_next failed %d\n", errno);
                    ret_code = -1;
                    goto end_test_iterator;
                }
                else
                {
                    if (count != num_keys)
                    {
                        fprintf(stderr, "all keys were not iterated\n");
                        ret_code = -1;
                        goto end_test_iterator;
                    }
                }
                break;
            }
        }
        i++;
    } while (i < m_options.m_noPools);

end_test_iterator:
    delete[] key;
    free(value);
    free(value_cmp_buf);
    return ret_code;
}
//
//@verifyBuffer - verifies if the data of length value_size stored
//                @ptr contains char of value test_char
//
int KvApi::verifyBuffer( char *ptr, int value_size, char test_char)
{
    // Compare read value with original value
    int error_count = 0;

    for (int i = 0; i < value_size; ++i)
    {
        if (*ptr != test_char)
        {
            ++error_count;
        }
        ++ptr;
    }
    if (error_count)
    {
        printf("ERROR comparing value %d bytes incorrect\n", error_count);
    }

    return error_count;
}
//
//@initializePoolVector  - initialize m_poolIdsVector
//
void KvApi::initializePoolVector()
{
    if (m_options.m_noPools == 0)
    {
        m_poolIdsVector.resize(1);
        m_poolIdsVector[0] = 0;
        return;
    }
    m_poolIdsVector.resize(m_options.m_noPools);
    return;
}
//
//@getMicroTime   - gets curernt time in micro seconds
//
int64_t KvApi::getMicroTime(void)
{
    int64_t rc;
#if defined(WIN32)
    LARGE_INTEGER now;
    static int initialized = 0;
    static LARGE_INTEGER freq;
    if (!initialized)
    {
        if (!QueryPerformanceFrequency(&freq))
        {
            fprintf(stderr, "ufn_time_usec(): system does not support\
                             performance timer.\n");
            return 0;
        }
        initialized = 1;
    }
    QueryPerformanceCounter(&now);
    rc = now.QuadPart * M_USEC_PER_SEC / freq.QuadPart;
#elif defined(__OSX__)
    uint64_t now;
    static int initialized = 0;
    static mach_timebase_info_data_t info;
    if (!initialized)
    {
        mach_timebase_info(&info);
        initialized = 1;

    }
    now = mach_absolute_time();
    rc = now * info.numer / info.denom / M_NSEC_PER_PSEC;
#else
    struct timespec ts;
    static int initialized = 0;
    static clockid_t clock_id;
    if (!initialized)
    {
#if !defined(__hpux__)
        if (!clock_getres(CLOCK_MONOTONIC, &ts))
        {
            clock_id = CLOCK_MONOTONIC;
        }
        else
#endif
        {
            clock_id = CLOCK_REALTIME;
        }
        initialized = 1;
    }
    if (clock_gettime(clock_id, &ts))
    {
        fprintf(stderr, "clock_gettime() failed: %s.\n", strerror(errno));
        return 0;
    }
    rc = (uint64_t)ts.tv_sec * M_USEC_PER_SEC + \
         (uint64_t)ts.tv_nsec / M_NSEC_PER_PSEC;
#endif
    return rc;
}
//
//@printStats   - prints statistics for perf tests
//
void KvApi::printStats(int job)
{
    stats_t time_delta = {0};
    double avg_latency = 0;
    double iops = 0;
    double bwidth = 0;
    uint64_t bytes_written = 0;
    int num_pools = 0;

    //check if the number of pools is 0 if so then set it to 1
    num_pools = m_options.m_noPools;
    if (m_options.m_noPools == 0)
    {
        num_pools = 1;
    }

    //value size is always in sector - one more sector is needed to store
    //key and key metadata
    bytes_written = m_options.m_numIos * m_options.m_valSize * \
                    m_sectorSize * num_pools;
    for (int i = 0; i < m_options.m_threads; i++)
    {
        time_delta.time_delta += m_timeDelta[i];
        if (time_delta.max_time_delta < m_timeDelta[i])
        {
            time_delta.max_time_delta = m_timeDelta[i];
        }
    }
    if ((job & KV_BATCH_PUT))
    {
        bytes_written *= m_options.m_numIovs;
    }
    avg_latency = time_delta.time_delta / \
                  (m_options.m_numIos * num_pools);
    iops = (double)(m_options.m_numIos * num_pools) * \
           M_USEC_PER_SEC / (time_delta.max_time_delta);
    bwidth = (double) (bytesToMib(bytes_written)) * \
             M_USEC_PER_SEC / (double) time_delta.max_time_delta;
    switch (job)
    {
        case KV_PUT:
            fprintf(stdout, "KV PUT - time delta = %u, Avg latency = %.0f, "
                    "IOPS = %.0f\n", time_delta.time_delta, avg_latency, iops);
            fprintf(stdout, "KV PUT Data throughput %.2fMB/s\n", bwidth);
            break;
        case KV_GET:
            fprintf(stdout, "KV GET - time delta = %u, Avg latency = %.0f, "
                    "IOPS = %.0f\n", time_delta.time_delta, avg_latency, iops);
            fprintf(stdout, "KV GET Data throughput %.2fMB/s\n", bwidth);
            break;
        case KV_EXISTS:
            fprintf(stdout, "KV EXISTS - time delta = %u, Avg latency = %.0f, "
                    "IOPS = %.0f\n", time_delta.time_delta, avg_latency, iops);
            break;
        case KV_DELETE:
            fprintf(stdout, "KV DELETE - time delta = %u, Avg latency = %.0f, "
                    "IOPS = %.0f\n", time_delta.time_delta, avg_latency, iops);
            fprintf(stdout, "KV DELETE Data throughput %.2fMB/s\n", bwidth);
            break;
        case KV_ITERATE:
            avg_latency = time_delta.time_delta / \
                          (m_options.m_numIos * num_pools * \
                           m_options.m_threads);
            iops = (double)(m_options.m_numIos * num_pools *
                   m_options.m_threads) * M_USEC_PER_SEC  /
                   (time_delta.max_time_delta);
            bwidth = (double) (bytesToMib(bytes_written)) *
                     M_USEC_PER_SEC * m_options.m_threads /
                     (double) time_delta.max_time_delta;
            fprintf(stdout, "KV ITERATOR - time delta = %u, Avg latency = %.0f"
                    " , IOPS = %.0f\n", time_delta.time_delta,
                    avg_latency, iops);
            fprintf(stdout, "KV ITERATOR Data throughput %.2fMB/s\n", bwidth);
            break;
        case KV_BATCH_PUT:
            fprintf(stdout, "KV BATCH PUT - time delta = %u,"
                    " Avg latency = %.0f, IOPS = %.0f\n",
                    time_delta.time_delta, avg_latency, iops);
            fprintf(stdout, "KV BATCH PUT Data throughput %.2fMB/s\n", bwidth);
            break;
    }
}
//
//member function definition for KeyList
//

//
//@destructor
//
KeyList::~KeyList()
{
    if (m_pInstance)
    {
        delete m_pInstance;
        m_pInstance = NULL;
    }
}
//
//@generateKeys - keys that generate random keys
//
void KeyList::generateKey(nvm_kv_key_t *key, int key_len)
{
    for (int i = 0; i < key_len; i++)
    {
        key[i] = rand() % 256;
    }
    return;
}
//
//@fillKeyList - fills m_keys vectors with random keys
//
void KeyList::fillKeyList()
{
    //if number of keys is greater than 10 million
    //generate keys incrementally
    for (int i = 0; i < m_numbThreads; i++)
    {
        for (int j = 0; j < (m_numbKeys / m_numbThreads); j++)
        {
            nvm_kv_key_t *key;
            int key_len = rand() % 123 + 5;
            key = (nvm_kv_key_t *) malloc(key_len);
            generateKey(key, key_len);
            m_keys[i][j]= key;
            m_keySize[i][j] = key_len;
        }
    }
}
//
//@getKey  -  gets key from ith vector located at jth position
//
nvm_kv_key_t* KeyList::getKey(int i, int j, uint32_t &key_len, int inc_counter)
{
    nvm_kv_key_t *key = NULL;
    if (m_incremental)
    {
        pthread_mutex_lock(&m_mtx);
        m_incrCounter++;
        inc_counter = m_incrCounter;
        if (!(inc_counter % M_MAX_RAND_KEYS))
        {
            fprintf(stdout, "KeyList::getKey, Key is (%d)\n", m_incrCounter);
        }
        pthread_mutex_unlock(&m_mtx);
        key_len = sizeof(inc_counter);
        key = (nvm_kv_key_t *) &inc_counter;
    }
    else
    {
        key = m_keys[i][j];
        key_len = m_keySize[i][j];
    }
    return key;
}
//
//@initialize   - initialize member variables like m_numbKeys m_numbThreads
//
void KeyList::initialize(int numb_keys, int numb_threads)
{
    int i = 0;

    m_numbKeys = numb_keys;
    m_numbThreads = numb_threads;
    //if the number of ios is greater than M_MAX_RAND_KEYS
    //generate keys incrementally
    if (m_numbKeys > M_MAX_RAND_KEYS)
    {
        fprintf(stdout, "KeyList::initialize, Incremental is set to true\n");
        m_incremental = true;
    }
    else
    {
        m_incremental = false;

        m_keys.resize(numb_threads);
        m_keySize.resize(numb_threads);
        for (i = 0; i < numb_threads; i++)
        {
            m_keys[i].resize(numb_keys / numb_threads);
            m_keySize[i].resize(numb_keys / numb_threads);
        }
        fillKeyList();
    }
}
//
//@instance - creates instance of the class
//
KeyList * KeyList::instance()
{
    if (m_pInstance == NULL)
    {
        m_pInstance = new KeyList();
    }
    return m_pInstance;
}
//
//@member function definations for class KvWorkerThreads
//

//
//@createThreads - create threads and allow them to handle the work
//
void KvWorkerThreads::createThreads(int job)
{
    m_job  = job;
    for (int i = 0; i < m_pOptions->m_threads; ++i)
    {
        pthread_create(&m_threads[i], NULL, KvWorkerThreads::workerThread,
                       NULL);
    }
}
//
//@workerThread - a newly created thread start work from here
//                each thread splits number of ios
//
void * KvWorkerThreads::workerThread(void *param)
{
    int local_index = 0;
    pthread_t id = pthread_self();
    int num_itr = 0;

    for (int i = 0; i < m_pOptions->m_threads; i++)
    {
        if (m_threads[i] == id)
        {
            local_index = i;
            break;
        }
    }
    num_itr = m_pOptions->m_numIos / m_pOptions->m_threads;
    if ((m_job == KV_BATCH_PUT))
    {
        num_itr /= m_pOptions->m_numIovs;
        KvApi::testBatchApi(m_job, num_itr, local_index);
    }
    if ((m_job == KV_ITERATE))
    {
        KvApi::testIterator(local_index);
    }
    if ((m_job == KV_PUT) || (m_job == KV_GET) || \
        (m_job == KV_DELETE) || m_job == KV_EXISTS)
    {
        KvApi::testBasicApi(m_job, num_itr, local_index);
    }
    return NULL;
}
//
//@waitAndTerminateThreads - wait till all threads finish and do clean up
//
void KvWorkerThreads::waitAndTerminateThreads( )
{
    // Wait for all work to be consumed
    for (int t = 0; t < m_pOptions->m_threads; ++t)
    {
        void *whatever;
        pthread_join(m_threads[t], &whatever);
    }
}
//
//@setOptions  - set m_pOptions
//
void KvWorkerThreads::setOptions(KvOptions *options)
{
    m_pOptions = options;
}
//
//member function definations for KvOptions
//

//
//@parseCommandLine   - parses command line arguments
//
bool KvOptions::parseCommandLine(int argc, char *argv[])
{
    int option_char = 0;

    while ((option_char = getopt(argc, argv, "hv")) != EOF)
    {
        switch (option_char)
        {
            case 'v':
                fprintf(stdout, "%s \n", M_KV_VERSION_STRING);
                exit(0);
            case 'h':
                usage(basename(argv[0]));
                exit(0);
            case '?':
                if (isprint(optopt))
                {
                    fprintf(stderr, "unknown option - %c\n", optopt);
                }
                return false;
        }

    }
    if (argc != 2)
    {
        return false;
    }
    strcpy(m_confFile, argv[1]);
    if (!parseConfFile(m_confFile))
    {
        return false;
    }

    return true;
}
bool KvOptions::parseConfFile(char *file_name)
{
    xmlDoc *doc = NULL;
    xmlXPathContext *xpathCtx = NULL;
    xmlXPathObject *xpathObjDevice = NULL;
    xmlXPathObject *xpathObjTest = NULL;
    xmlXPathObject *xpathObjT = NULL;
    xmlXPathObject *xpathObjJ = NULL;
    xmlXPathObject *xpathObjE = NULL;
    xmlXPathObject *xpathObjP = NULL;
    xmlXPathObject *xpathObjIo = NULL;
    xmlNode *node = NULL;
    xmlAttr *attr = NULL;
    bool ret_val = true;

    if (!file_name)
    {
        fprintf(stdout, "Invalid config file \n");
        ret_val = false;
        goto end_parse;
    }
    //starting to parse config file
    xmlInitParser();
    doc = xmlParseFile(file_name);
    if (!doc)
    {
        fprintf(stdout, "parsing xml file failed\n");
        ret_val = false;
        goto end_parse;
    }
    xpathCtx = xmlXPathNewContext(doc);
    if (!xpathCtx)
    {
        fprintf(stdout, "parsing xml context failed\n");
        ret_val = false;
        goto end_parse;
    }
    //read all device information
    xpathObjDevice = xmlXPathEvalExpression ((xmlChar *) "//nvmKV/device",
                                             xpathCtx);
    if (xpathObjDevice->nodesetval->nodeNr ==  0)
    {
        fprintf(stderr, "device not configured\n");
        ret_val = false;
        goto end_parse;
    }
    node = xpathObjDevice->nodesetval->nodeTab[0];
    attr = node->properties;
    while (attr)
    {
        if (strcmp((const char*) attr->name, "name") == 0)
        {
            strcpy(m_deviceName, (const char*) attr->children->content);
            fprintf(stdout, "device name %s\n", m_deviceName);
        }

        attr = attr->next;
    }
    //read all test information
    xpathObjTest = xmlXPathEvalExpression ((xmlChar *) "//nvmKV/test",
                                           xpathCtx);
    if (xpathObjTest->nodesetval->nodeNr ==  0)
    {
        fprintf(stdout, "test not configured\n");
        ret_val = false;
        goto end_parse;
    }
    node = xpathObjTest->nodesetval->nodeTab[0];
    attr = node->properties;
    while (attr)
    {
        if (strcmp((const char*) attr->name, "smoke") == 0)
        {
            if (strcmp((const char*) attr->children->content, "true") == 0)
            {
                m_smokeTest = true;
            }
        }
        if (strcmp((const char*) attr->name, "perf") == 0)
        {
            if (strcmp((const char*) attr->children->content, "true") == 0)
            {
                m_perfTest = true;
            }
        }
        if (strcmp((const char*) attr->name, "functional") == 0)
        {
            if (strcmp((const char*) attr->children->content, "true") == 0)
            {
                m_functionalTest = true;
            }
        }
        attr = attr->next;
    }
    //all parsing for perf is done below
    //if perf is false return
    if (!m_perfTest)
    {
        ret_val = true;
        goto end_parse;
    }
    //read all perf/io information
    xpathObjIo = xmlXPathEvalExpression ((xmlChar *) "//nvmKV/perf/io",
                                         xpathCtx);
    if (xpathObjIo->nodesetval->nodeNr ==  0)
    {
        fprintf(stdout, "perf/io not configured\n");
        ret_val = false;
        goto end_parse;
    }
    node = xpathObjIo->nodesetval->nodeTab[0];
    attr = node->properties;
    while (attr)
    {
        if (strcmp((const char*) attr->name, "valueSize") == 0)
        {
            m_valSize = atoi((const char*) attr->children->content);
        }
        if (strcmp((const char*) attr->name, "valueUnits") == 0)
        {
            if (strcmp((const char*) attr->children->content, "sector") == 0)
            {
                m_unitsInSector = true;
            }
        }
        if (strcmp((const char*) attr->name, "kvCount") == 0)
        {
            m_numIos = atoi((const char*) attr->children->content);
        }
        if (strcmp((const char*) attr->name, "batchSize") == 0)
        {
            m_numIovs = atoi((const char*) attr->children->content);
        }
        attr = attr->next;
    }
    //read all perf/thread information
    xpathObjT = xmlXPathEvalExpression ((xmlChar *) "//nvmKV/perf/threads",
                                        xpathCtx);
    if (xpathObjT)
    {
        if (xpathObjT->nodesetval->nodeNr > 0)
        {
            node = xpathObjT->nodesetval->nodeTab[0];
            attr = node->properties;
            while (attr)
            {
                if (strcmp((const char*) attr->name, "count") == 0)
                {
                    m_threads = atoi((const char*) attr->children->content);
                    if (m_threads == 0)
                    {
                        m_threads = 1;
                    }
                }
                attr = attr->next;
            }
            fprintf(stdout, "thread %d\n", m_threads);
        }
    }
    xpathObjP = xmlXPathEvalExpression ((xmlChar *) "//nvmKV/perf/pools",
                                        xpathCtx);
    if (xpathObjP)
    {
        if (xpathObjP->nodesetval->nodeNr > 0)
        {
            node = xpathObjP->nodesetval->nodeTab[0];
            attr = node->properties;
            while (attr)
            {
                if (strcmp((const char*) attr->name, "maxPools") == 0)
                {
                    m_maxPools = atoi((const char*) attr->children->content);
                }
                if (strcmp((const char*) attr->name, "poolCount") == 0)
                {
                    m_noPools = atoi((const char*) attr->children->content);
                }
                if (strcmp((const char*) attr->name, "getPoolInfo") == 0)
                {
                    if (strcmp((const char*) attr->children->content,
                               "true") == 0)
                    {
                        m_getPoolInfo = true;
                    }
                }
                attr = attr->next;
            }
        }
        if (m_maxPools < m_noPools)
        {
            fprintf(stderr,"max_pools should be greater than pool_count\n");
            ret_val = false;
            goto end_parse;
        }
    }

    xpathObjE = xmlXPathEvalExpression ((xmlChar *) "//nvmKV/perf/expiry",
                                        xpathCtx);
    if (xpathObjE)
    {
        if (xpathObjE->nodesetval->nodeNr > 0)
        {
            node = xpathObjE->nodesetval->nodeTab[0];
            attr = node->properties;
            while (attr)
            {
                if (strcmp((const char*) attr->name, "method") == 0)
                {
                    m_expiry = atoi((const char*) attr->children->content);
                    if (m_expiry > KV_GLOBAL_EXPIRY)
                    {
                        fprintf(stderr, "Error, invalid expiry method");
                        ret_val = false;
                        goto end_parse;
                    }
                }
                if (strcmp((const char*) attr->name, "expiryInSecs") == 0)
                {
                    //store per key expiry only for arbitrary expiry
                    m_expiryInSecs = atoi((const char*)
                                           attr->children->content);
                }
                attr = attr->next;
            }
        }
    }
    xpathObjJ = xmlXPathEvalExpression ((xmlChar *) "//nvmKV/perf/jobs",
                                        xpathCtx);
    if (xpathObjJ->nodesetval->nodeNr == 0)
    {
        fprintf(stderr,"jobs string not defined");
        ret_val = false;
        goto end_parse;
    }
    node = xpathObjJ->nodesetval->nodeTab[0];
    attr = node->properties;
    while (attr)
    {
        if (strcmp((const char*) attr->name, "jobString") == 0)
        {
            if (strlen((const char*) attr->children->content) < M_MAX_STR_SIZE)
            {
                strcpy(m_jobs, (const char*) attr->children->content);
            }
            else
            {
                fprintf(stderr,"invalid job string\n");
                ret_val = false;
                goto end_parse;
            }
        }
        if (strcmp((const char*) attr->name, "verify") == 0)
        {
            if (strcmp((const char*) attr->children->content, "true") == 0)
            {
                m_verify = true;
            }
        }
        if (strcmp((const char*) attr->name, "replacePuts") == 0)
        {
            if (strcmp((const char*) attr->children->content, "true") == 0)
            {
                m_replacePuts = true;
            }
        }
        if (strcmp((const char*) attr->name, "readExact") == 0)
        {
            if (strcmp((const char*) attr->children->content, "true") == 0)
            {
                m_readExact = true;
            }
        }
        if (strcmp((const char*) attr->name, "genCount") == 0)
        {
            m_genCount = atoi((const char*) attr->children->content);
        }
        attr = attr->next;
    }
end_parse:
    xmlXPathFreeObject(xpathObjDevice);
    xmlXPathFreeObject(xpathObjTest);
    xmlXPathFreeObject(xpathObjT);
    xmlXPathFreeObject(xpathObjJ);
    xmlXPathFreeObject(xpathObjE);
    xmlXPathFreeObject(xpathObjP);
    xmlXPathFreeObject(xpathObjIo);
    xmlXPathFreeContext(xpathCtx);
    xmlFreeDoc(doc);
    return ret_val;
}
//
//@usage    - usage on the tool
//
bool KvOptions::usage(const char *name)
{
    fprintf(stderr, "Fusion-io %s utility\n", name);
    fprintf(stderr, "usage: %s <conf file> | -h| -v\n", name);
    fprintf(stderr, "\t -v   version\n");
    fprintf(stderr, "\t -h   help\n");
    return 0;
}
