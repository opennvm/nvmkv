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
#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <nvm/nvm_kv.h>
#include <nvm/nvm_error.h>

#define MAX_DEV_NAME_SIZE 256
#define SECTOR_SIZE 512
#define KEY_LEN 9
#define VALUE_LEN 512


int main(int argc, char **argv)
{
    char                  device_name[MAX_DEV_NAME_SIZE];
    char                  pool_tag[16];
    char                 *key_str = NULL;
    char                 *key_str2 = NULL;
    char                 *key = NULL;
    char                 *value_str = NULL;
    char                 *value_str2 = NULL;
    nvm_kv_key_info_t     key_info;
    nvm_kv_pool_info_t    pool_info;
    nvm_kv_store_info_t   store_info;
    nvm_kv_pool_metadata_t   pool_md;
    int                   optind = 0;
    int                   ret = 0;
    int                   value_len = 0;
    int                   it_id = -1;
    int                   fd = 0;
    int                   pool_id = 0;
    int                   kv_id = 0;
    int                   next_element;
    uint32_t              key_len = 0;
    uint32_t              version = 0;
    uint64_t              cache_size = 4096;
    bool                  read_exact = false;

    if (argc < 2)
    {
        printf("usage:\n");
        printf("./kv_sample <device_name>\n");
        return -1;
    }

    /* Opening the underneath device. device_name must be /dev/fioX where X can be a - z */
    strncpy(device_name, argv[++optind], MAX_DEV_NAME_SIZE);
    if (strncmp(device_name, "/dev/fio", strlen("/dev/fio")) != 0)
    {
        printf("***** Incorrect device name %s *****\n", device_name);
        return -1;
    }

    fd = open(device_name, O_RDWR | O_DIRECT);
    if (fd < 0)
    {
        printf("could not open file %s errno %d\n", device_name, errno);
        return -1;
    }
    printf("Device id = %d\n", fd);


    /*********************/
    /* Open the KV Store */
    /*********************/
    kv_id = nvm_kv_open(fd, version, NVM_KV_MAX_POOLS,
                        KV_GLOBAL_EXPIRY, cache_size);
    if (kv_id < 0)
    {
        printf("nvm_kv_open: failed, errno = %d\n", errno);
        goto test_exit;
    }
    printf("nvm_kv_open: success\n");
    printf("\tcreated KV store, kv_id = %d\n", kv_id);

    /*********************/
    /* Set the KV Expiry */
    /*********************/

    ret = nvm_kv_set_global_expiry(kv_id, 30);
    if (ret < 0)
    {
        printf("nvm_kv_set_global_expiry: failed, errno = %d\n", errno);
        goto test_exit;
    }
    printf("nvm_kv_set_global_expiry: success\n");


    /********************/
    /* Create a KV pool */
    /********************/
    strncpy(pool_tag, "sample_pool", strlen("sample_pool"));
    pool_id = nvm_kv_pool_create(kv_id, (nvm_kv_pool_tag_t *)pool_tag);
    if (pool_id < 0)
    {
        printf("nvm_kv_pool_create: failed, errno = %d\n", errno);
        goto test_exit;
    }
    printf("nvm_kv_pool_create: success\n");
    printf("\tcreated pool with pool_id = %d and tag = %s\n", pool_id, pool_tag);


    /************************************/
    /* Get the KV store info            */
    /* Note: this is a blocking API and */
    /* could take a while on a large    */
    /* store.                           */
    /************************************/
    ret = nvm_kv_get_store_info(kv_id, &store_info);
    if (ret < 0)
    {
        printf("nvm_kv_get_store_info: failed, errno = %d\n", errno);
        goto test_exit;
    }
    printf("nvm_kv_get_store_info: success\n");
    printf("\tversion              = %lu\n", store_info.version);
    printf("\tnum_pools            = %lu\n", store_info.num_pools);
    printf("\tmax_pools            = %lu\n", store_info.max_pools);
    printf("\texpiry_mode          = %lu\n", store_info.expiry_mode);
    printf("\tglobal_expiry        = %lu\n", store_info.global_expiry);
    printf("\tnum_keys             = %llu\n", store_info.num_keys);
    printf("\tfree_space(in bytes) = %llu\n", store_info.free_space);

    /********************************/
    /* Get the pool metadata info   */
    /********************************/
    ret = nvm_kv_get_pool_metadata(kv_id, &pool_md, 1, pool_id);
    if (ret < 0)
    {
        printf("nvm_kv_get_pool_metadata: failed, errno = %d\n", errno);
        goto test_exit;
    }
    printf("nvm_kv_get_pool_metadata: success ret = %d\n", ret);
    printf("\tpool_id              = %lu\n", pool_md.pool_id);
    printf("\tpool_tag             = %s\n", &pool_md.pool_tag);


    /********************************************/
    /* Create two key buffers of length KEY_LEN */
    /* Note: NVM_KV_MAX_KEY_SIZE 128            */
    /* Fill the key buffers.                    */
    /********************************************/
    key_str = (char *) malloc (sizeof(char) * KEY_LEN);
    if (!key_str)
    {
        printf("memory allocation failed\n");
        goto test_exit;
    }
    key_str2 = (char *) malloc (sizeof(char) * KEY_LEN);
    if (!key_str2)
    {
        printf("memory allocation failed\n");
        goto test_exit;
    }
    strcpy(key_str, "abc_test");
    strcpy(key_str2, "xyz_test");


    /************************************************/
    /* Create two value buffers of length value_len */
    /* The buffers must be sector aligned           */
    /* Note: NVM_KV_MAX_VALUE_SIZE 1047552          */
    /* Fill the value buffers                       */
    /************************************************/
    value_len = VALUE_LEN;
    posix_memalign((void**) &value_str, SECTOR_SIZE, value_len);
    memset(value_str, 'a', value_len);

    posix_memalign((void**) &value_str2, SECTOR_SIZE, value_len);
    memset(value_str2, 'b', value_len);


    /****************************************/
    /* Put the KV pair (key_str, value_str) */
    /****************************************/
    ret = nvm_kv_put(kv_id, pool_id, (nvm_kv_key_t *) key_str,
                    strlen((char *) key_str), value_str, value_len, 0, false, 0);
    if (ret < 0)
    {
        printf("nvm_kv_put: failed, errno = %d\n", errno);
        goto test_exit;
    }
    printf ("nvm_kv_put: success\n");

    /******************************************/
    /* Put the KV pair (key_str2, value_str2) */
    /******************************************/
    ret = nvm_kv_put(kv_id, pool_id, (nvm_kv_key_t *) key_str2,
                    strlen((char *) key_str2), value_str2, value_len, 0, false, 0);
    if (ret < 0)
    {
        printf("nvm_kv_put: failed, errno = %d\n", errno);
        goto test_exit;
    }
    printf("nvm_kv_put: success\n");


    /************************************/
    /* Get the KV store info            */
    /* Note: this is a blocking API and */
    /* could take a while on a large    */
    /* store.                           */
    /************************************/
    ret = nvm_kv_get_store_info(kv_id, &store_info);
    if (ret < 0)
    {
        printf("nvm_kv_get_store_info: failed, errno = %d\n", errno);
        goto test_exit;
    }
    printf("nvm_kv_get_store_info: success\n");
    printf("\tversion              = %lu\n", store_info.version);
    printf("\tnum_pools            = %lu\n", store_info.num_pools);
    printf("\tmax_pools            = %lu\n", store_info.max_pools);
    printf("\texpiry_mode          = %lu\n", store_info.expiry_mode);
    printf("\tglobal_expiry        = %lu\n", store_info.global_expiry);
    printf("\tnum_keys             = %llu\n", store_info.num_keys);
    printf("\tfree_space(in bytes) = %llu\n", store_info.free_space);


    /*****************************************/
    /* Get the value length with key_str     */
    /*****************************************/
    ret = nvm_kv_get_val_len(kv_id, pool_id, (nvm_kv_key_t *) key_str, strlen((char *) key_str));
    if (ret < 0)
    {
        printf("nvm_kv_get_val_len: failed, errno = %d\n", errno);
        goto test_exit;
    }
    printf("nvm_kv_get_val_len: success, len = %d\n", ret);




    /*****************************************/
    /* Get the value associated with key_str */
    /*****************************************/
    ret = nvm_kv_get(kv_id, pool_id, (nvm_kv_key_t *) key_str, strlen((char *) key_str),
                     value_str, value_len, read_exact, &key_info);
    if (ret < 0)
    {
        printf("nvm_kv_get: failed, errno = %d\n", errno);
        goto test_exit;
    }
    printf("nvm_kv_get: success\n");


    /*************************/
    /* Query the Key key_str */
    /*************************/
    ret = nvm_kv_exists(kv_id, pool_id, (nvm_kv_key_t *) key_str, strlen((char *) key_str), &key_info);
    if (ret == 0)
    {
        printf("nvm_kv_exists: key doesn't exist, errno %d\n", errno);
        goto test_exit;
    }
    printf("nvm_kv_exists: key exists\n");


    /***************************************/
    /* Iterate through KV pairs in pool_id */
    /***************************************/
    it_id = nvm_kv_begin(kv_id, pool_id);
    if (it_id < 0)
    {
	    printf("nvm_kv_begin: failed, errno = %d\n", errno);
            goto test_exit;
    }
    printf("nvm_kv_begin: success\n");

    key = (char *)malloc(strlen((char *) key_str));
    if (!key)
    {
        printf("memory allocation failed\n");
        goto test_exit;
    }
    while (true)
    {
        ret = nvm_kv_get_current(kv_id, it_id, (nvm_kv_key_t *)key, &key_len,
                                 value_str, value_len, &key_info);
        if (ret < 0)
        {
            printf("nvm_kv_get_current: failed, errno = %d\n", errno);
            goto test_exit;
        }
        printf("nvm_kv_get_current: success\n");

        next_element = nvm_kv_next(kv_id, it_id);
	if (next_element != 0)
	{
	    if (errno != -NVM_ERR_OBJECT_NOT_FOUND)
	    {
	        printf("nvm_kv_next: failed, errno = %d\n", errno);
	    }
	    break;
	}
        printf("nvm_kv_next: success\n");
    }


    /****************************/
    /* Release the iterator id  */
    /****************************/
    ret = nvm_kv_iteration_end(kv_id, it_id);
    if (ret < 0)
    {
        printf("nvm_kv_iteration_end: failed, errno = %d\n", errno);
        goto test_exit;
    }
    printf("nvm_kv_iteration_end: success\n");


    /**************************/
    /* Delete the key key_str */
    /**************************/
    ret = nvm_kv_delete(kv_id, pool_id, (nvm_kv_key_t *) key_str, strlen((char *) key_str));
    if (ret < 0)
    {
        printf("nvm_kv_delete: failed, errno = %d\n", errno);
        goto test_exit;
    }
    printf("nvm_kv_delete: success\n");


    /*******************************/
    /* Sleep for the Global Expiry */
    /* time and the delete the key */
    /* key_str.                    */
    /* The delete should return a  */
    /* SUCCESS.                    */
    /*******************************/
    printf("sleep for 30 seconds and let keys expire\n");
    sleep(30);
    printf("done\n");


    /***************************/
    /* Delete the key key_str2 */
    /***************************/
    ret = nvm_kv_delete(kv_id, pool_id, (nvm_kv_key_t *) key_str2, strlen((char *) key_str2));
    if (ret < 0)
    {
        printf("nvm_kv_delete: failed, errno = %d\n", errno);
        goto test_exit;
    }
    printf("nvm_kv_delete: success\n");


    /*****************************/
    /* Get the current pool info */
    /*****************************/
    ret = nvm_kv_get_pool_info(kv_id, pool_id, &pool_info);
    if (ret < 0)
    {
        printf("nvm_kv_get_pool_info: failed, errno = %d\n", errno);
        goto test_exit;
    }
    printf("nvm_kv_get_pool_info: success\n");
    printf("\tversion     = %lu\n", pool_info.version);
    printf("\tpool_status = %lu\n", pool_info.pool_status);


    /*****************************/
    /* Delete all KV pairs       */
    /* This could take a while   */
    /* on a large store          */
    /*****************************/
    printf("nvm_kv_delete_all: deleting all KV pairs\n");
    ret = nvm_kv_delete_all(kv_id);
    if (ret < 0)
    {
        printf("nvm_kv_delete_all: failed, errno = %d\n", errno);
        goto test_exit;
    }
    printf("nvm_kv_delete_all: success\n");


    /*****************************/
    /* Delete the current pool   */
    /*****************************/
    ret = nvm_kv_pool_delete(kv_id, pool_id);
    if (ret < 0)
    {
        printf("nvm_kv_pool_delete: failed, errno = %d\n", errno);
        goto test_exit;
    }

    //Wait till pools are completely deleted
    while (1)
    {
        ret = nvm_kv_get_pool_info(kv_id, pool_id, &pool_info);
        if (ret < 0)
        {
            printf("nvm_kv_get_pool_info failed with errno %d\n",
                    errno);
            goto test_exit;
        }
        if (pool_info.pool_status == POOL_NOT_IN_USE)
        {
            printf("pool deleted completely pool status = %d\n",
                    pool_info.pool_status);
            break;
        }
    }
    printf("nvm_kv_pool_delete: success\n");


    /**********************/
    /* Close the KV Store */
    /**********************/
    ret = nvm_kv_close(kv_id);
    if (ret < 0)
    {
        printf("nvm_kv_close: failed, errno = %d\n", errno);
        goto test_exit;
    }
    printf("nvm_kv_close: success\n");


test_exit:
    free(key);
    free(value_str);
    free(value_str2);
    free(key_str);
    free(key_str2);
    close(fd);
    exit(0);
}

