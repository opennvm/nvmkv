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
#include "nvm_kv.h"
#include "nvm_error.h"
#include "nvm_primitives.h"
#include <vector>
#include <stdint.h>
#include <string>
using namespace std;
#define BLKPBSZGET _IO(0x12,123) //for ioctl to get sector size
//
//@jobtype_t - enum for all KV job types
//
typedef enum
{
    KV_PUT = 1,
    KV_GET,
    KV_EXISTS,
    KV_DELETE,
    KV_ITERATE,
    KV_BATCH_PUT,
} jobtype_t;
//
//@stats_t - structure that holds units for time_delta, max_time_delta,
//           avg_latency
//
typedef struct
{
    uint32_t time_delta;
    uint32_t max_time_delta;
} stats_t;
//
//@KvOptions             - class used to parse and store all tool's
//                         command-line options
//
class KvOptions
{
    public:
    //
    //@constructor
    //
    KvOptions()
    {
        m_threads  = 1;
        m_noPools  = 0;
        m_maxPools = 0;
        m_getPoolInfo = false;
        m_smallValue = 0;
        m_functionalTest = false;
        m_perfTest = false;
        m_smokeTest = false;
        m_replacePuts = false;
        m_readExact = false;
        m_verify = false;
        m_mixedLoad = false;
        m_expiry = KV_DISABLE_EXPIRY;
        m_sectorAligned = true;
        m_unitsInSector = false;
        m_expiryInSecs = 0;
        m_apiVersion = 0;
        m_numIos = 0;
        m_fd = -1;
    }
    //
    //@parseCommandLine   - parses command line arguments
    //@return             - true on success and false on error reading
    //                      inputs from command line
    //
    bool parseCommandLine(int argc, char *argv[]);
    //
    //@parseConfFile   - parses config file
    //@return          - true on success and false on error parsing
    //                   config file
    //
    bool parseConfFile(char *file_name);
    //
    //@usage    - usage of the tool
    //@return   - true on success
    //
    bool usage(const char *name);
    //
    //member constants
    //@M_MAX_STRING_LEN    - maximum length of character array
    //@M_KV_VERSION_STRING - KV store API version
    //
    static const int M_MAX_STR_SIZE = 512;
    static const char M_KV_VERSION_STRING[M_MAX_STR_SIZE];
    //
    //@member variables
    //    m_deviceName       - device name
    //    m_valSize          - value size in terms of sectors or bytes
    //    m_numIos           - total number of ios (key/value pair count)
    //    m_verify           - if set to true kv_get needs to be verified
    //    m_threads          - number of thread
    //    m_smokeTest        - if set to true smoke tests are executed
    //    m_functionalTest   - if set to true functional tests are executed
    //    m_perfTest         - if set to true performance tests are executed
    //    m_replacePuts      - if set to true set replace flag to true in puts
    //    m_readExact        - if set to true read exact value_len that is
    //                         written on media in kv_get
    //    m_genCount         - version number of the applications
    //    m_unitsInSector    - if true value size is in units of sectors
    //    m_numIovs          - number of vectors (# of puts, gets) in each
    //                         batch request
    //    m_mixedLoad        - if set to true performance testing is done by
    //                         executing mixed kv_get, kv_put, kv_exists load
    //    m_noPools          - number of pools
    //    m_maxPools         - Maximum number of pools
    //    m_getPoolInfo      - if set to true pool info is queried
    //    m_expiry           - Expiry support:
    //                         KV_DISABLE_EXPIRY(0)   - Disable expiry
    //                         KV_ARBITRARY_EXPIRY(1) - Arbitrary expiry
    //                         KV_GLOBAL_EXPIRY(2)    - Global expiry
    //   m_expiryInSecs      - expiry in seconds
    //   m_sectorAligned     - user buffer sent in kv_put and kv_batch_put need
    //                         to be sector aligned
    //   m_smallValue        - this value if non-zero represents value size
    //                         less than a sector
    //   m_confFile          - path to config file
    //   m_jobs              - jobs that needs to be executed in order, each
    //                         job name is separated by ','
    //   m_apiVersion        - version of the KV store APIs
    //
    char m_confFile[M_MAX_STR_SIZE];
    char m_deviceName[M_MAX_STR_SIZE];
    bool m_smokeTest;
    bool m_perfTest;
    bool m_functionalTest;
    unsigned int m_valSize;
    int m_numIos;
    bool m_verify;
    int m_threads;
    bool m_replacePuts;
    bool m_readExact;
    int m_genCount;
    int m_numIovs;
    bool m_mixedLoad;
    uint32_t m_noPools;
    uint32_t m_maxPools;
    bool m_getPoolInfo;
    uint32_t m_expiry;
    uint32_t m_expiryInSecs;
    int m_fd;
    bool m_sectorAligned;
    uint32_t m_smallValue;
    bool m_unitsInSector;
    char m_jobs[M_MAX_STR_SIZE];
    int m_apiVersion;
};
//
//@KeyList               - class used to generate random keys and iterate
//                         through the list. For multiple threads, vector of vectors
//                         is generated, so that each thread has its own vector
//                         of keys.
//
class KeyList
{
    public:
        //
        //@destructor
        //
        ~KeyList();
        //
        //@fillKeyList - fills m_keys vectors with random keys
        //@return      - none
        //
        void fillKeyList();
        //
        //@generateKeys - random key of random size is generated
        //@key          - input, memory address where key needs to be generated
        //@key_len      - key length
        //@return       - none
        //
        void generateKey(nvm_kv_key_t *key, int key_len);
        //
        //@getKey       - gets key from ith vector located at jth position
        //@i            - index for one vector within vectors of vector
        //@j            - index within vector
        //@key_len      - output, length of the key that is retrieved
        //@inc_counter  - input, variable which stores key in case key is
        //                generated incrementally
        //@return       - memory address of the key
        //
        nvm_kv_key_t* getKey(int i, int j, uint32_t &key_len,
                             int inc_counter=0);
        //
        //@resetCounter - reset static counter
        //@return       - none
        //
        void resetCounter()
        {
            m_incrCounter = 0;
        }
        //
        //@initialize   - initialize member variables like, m_numbKeys
        //                m_numbThreads
        //@numb_keys    - total number of keys
        //@numb_threads - total number of threads
        //@return       - none
        //
        void initialize(int numb_keys, int numb_threads);
        //
        //@instance - creates instance of the class
        //
        static KeyList* instance();
    protected:
        //
        //@constructor
        //
        KeyList()
        {
            pthread_mutex_init(&m_mtx, NULL);
        }
    private:
        //@member variables
        //   m_numbKeys     - number of keys that are generated
        //   m_numbThreads   - number of threads
        //   m_keys          - vector of keys
        //   m_keySize       - vector of key size
        //   m_pInstance     - instance of the class
        //   m_incremental   - if set to true keys are generated
        //                     incrementally
        //   m_incrCounter   - static counter to generate keys
        //   m_mtx           - mutex variable used for thread safety
        int m_numbKeys;
        int m_numbThreads;
        std::vector< std::vector <nvm_kv_key_t *> > m_keys;
        std::vector<std::vector <int> > m_keySize;
        static KeyList *m_pInstance;
        bool m_incremental;
        static int m_incrCounter;
        pthread_mutex_t m_mtx;
        //
        //member constants
        // M_MAX_RAND_KEYS - if the io size is less than 10000000 then generate
        //                   keys randomly
        static const int M_MAX_RAND_KEYS = 10000000;
};
//
//@KvWorkerThreads - class that handles threads for kv_get and kv_put io
//
class KvWorkerThreads
{
    public:
        //
        //@constructor
        //
        KvWorkerThreads()
        {
        }
        //
        //@createThreads - creates threads and allow them to handle the work
        //@job           - job type can be set to KV_PUT, KV_GET or KV_EXISTS
        //
        static void createThreads(int job);
        //
        //@waitAndTerminateThreads - wait till all threads finish and do
        //                           clean up
        //@return                  - none
        //
        static void waitAndTerminateThreads();
        //
        //@setOptions  - set m_pOptions
        //@options     - commandline options
        //return       - none
        //
        static void setOptions(KvOptions *options);
    private:
        //
        //@workerThread - threads starts at this function
        //
        static void* workerThread(void *param);
        //
        //@constants
        //M_MAX_THREADS        - maximum threads that can be created for multi
        //                       threading KV store APIs
        //
        static const uint32_t M_MAX_THREADS = 256;
        //@member_variables
        //  m_threads     - vector to hold thread ids
        //  m_job         - flag to type of job
        //  m_pOptions    - pointer to options from command line
        static KvOptions *m_pOptions;
        static pthread_t m_threads[M_MAX_THREADS];
        static int m_job;
};
//
//@KvTest     - encapsulates all functionality required to test KV store
//
class KvTest
{
    public:
    //
    //@openKvContainer - Open the KV store container
    //@options         - command line param
    //@return          - true on success and false on failure
    //
    static bool openKvContainer(KvOptions &options);
    //
    //@closeKvContainer - close the KV store container
    //@options          - command line param
    //@return           - none
    //
    static void closeKvContainer(KvOptions &options);
    //
    //@testPerf - perf testing for kv_get and kv_put,
    //            functionality testing of all APIs
    //@options -  command line param
    //@return  -  true on success and false on failure
    //
    static void testPerf(KvOptions &options);
    //
    //@testFunctionality - test all functionality of KV store
    //@return            - return true if all test passes else return false
    //
    static bool testFunctionality();
    //
    //@testSmoke      - perform smoke test for all functionality of KV store
    //@options        - command line param
    //@return         - return true if all test passes else return false
    //
    static bool testSmoke(KvOptions &options);
    //
    //@constants
    //@M_MAX_DEV_NAME_SIZE  - maximum device name size
    //
    static const int M_MAX_DEV_NAME_SIZE = 256;
};

//
//@KvApi     - encapsulates all KV store APIs like nvm_kv_put, nvm_kv_get
//
class KvApi
{
    public:
    //
    //@setOptions - sets the options member variable
    //
    static void setOptions(KvOptions &options)
    {
        m_options = options;
    }
    //
    //@testKvOpen  - opens a KV store
    //@return      - 0 on success and -1 on failure
    //
    static int testKvOpen(int fd);
    //
    //@testKvClose    - closes KV store
    //@return         - 0 on success and -1 on failure
    //
    static int testKvClose();
    //
    //@testPoolCreate  - creates KV store pools
    //@return          - 0 on success and -1 on failure
    //
    static int testPoolCreate();
    //
    //@testPoolTags    - fetches pool tags and make sure if the tags are
    //                   correct
    //@pool_count      - number of pool_tags that needs to be retrieved
    //@kv_id           - default value is -1, if kv_id not set use member
    //                   variable
    //@return          - 0 on success and -1 on failure
    //
    static int testPoolTags(int pool_count, int kv_id = -1);
    //
    //@testGetKvStoreInfo  - gets KV store info
    //@return              - 0 on success and -1 on failure
    //
    static int testGetKvStoreInfo();
    //
    //@testGetAllPoolInfo  - prints pool info of all pools
    //@return              - 0 on success and -1 on failure
    //
    static int testGetAllPoolInfo();
    //
    //@testGetPoolInfo  - gets pool info of a pool
    //@pool_id          - pool_id of the pool
    //@return           - pool_info on success and -1 on failure
    //
    static int testGetPoolInfo(int pool_id);
    //
    //@testDeleteAllPools - deletes all pools in the KV store
    //@oneTimeDeletion    - if set to true deletes all pool at one time
    //@return             - returns 0 on success and -1 on failure
    //
    static int testDeleteAllPools(int oneTimeDeletion);
    //
    //@testDeletePool  - deletes a pool in the KV store
    //@pool_id         - pool_id of the pool
    //@return          - returns 0 on success and -1 on failure
    //
    static int testDeletePool(int pool_id);
    //
    //@testDeleteAllKeys  - deletes all keys in a pools
    //@return             - returns 0 on success and -1 on failure
    //
    static int testDeleteAllKeys();
    //
    //@testSetExpiry - tests the setting of global expiry
    //@return        - returns 0 on success and -1 on failure
    //
    static int testSetExpiry();
    //
    //@testBasicApi - test kv_put, kv_get, kv_exists, kv_delete
    //@job          - type of API that needs to be tested
    //@count        - number of times same API call needs to be executed
    //@index        - index into key list based on the thread executing
    //@return       - returns 0 on success and -1 on failure
    //
    static int testBasicApi(int job, int count, int index);
    //
    //@testBatchApi - test kv_batch_put
    //@job          - type of API that needs to be tested
    //@count        - number of times same API call needs to be executed
    //@index        - index into key list based on the thread executing
    //@return       - returns 0 on success and -1 on failure
    //
    static int testBatchApi(int job, int count, int index);
    //
    //@testIterator - test kv_begin, kv_get_current, kv_next
    //@index        - index into key list based on the thread executing
    //@return       - returns 0 on success and -1 on failure
    //
    static int testIterator(int index);
    //
    //@printStats   - prints statistics for perf tests
    //@return       - none
    //
    static void printStats(int job);
    private:
    //
    //@bytesToMib - convert bytes to Mib units
    //@return     - bytes converted to Mib
    //
    static uint64_t bytesToMib(uint64_t bytes)
    {
        return(bytes >> 20);
    }
    //
    //@initializePoolVector  - initialize m_poolIdsVector
    //@return                - none
    //
    static void initializePoolVector();
    //
    //@getMicroTime   - gets current time in micro seconds
    //@return         - time in ms
    //
    static int64_t getMicroTime(void);
    //
    //@verifyBuffer - verifies if the data of length value_size stored
    //                @ptr contains char of value test_char
    //
    static int verifyBuffer(char *ptr, int value_size, char test_char);
    //
    //@constants
    //@M_USEC_PER_SEC   - used to convert ms data to seconds data
    //@M_MAX_THREADS    - maximum threads that can be created for multi
    //                    threading KV store APIs
    //@M_MAX_KEY_SIZE   - maximum key length
    //@M_NSEC_PER_PSEC  - used to convert ns data to seconds data
    //
    static const uint32_t M_USEC_PER_SEC = 1000000;
    static const uint32_t M_MAX_THREADS = 256;
    static const uint32_t M_MAX_KEY_SIZE = 128;
    static const int M_NSEC_PER_PSEC = 1000;
    //
    //@member_variables
    //  m_timeDelta        - array that stores the time delta for each
    //                       operation for each thread
    //  m_kvId              - KV store id, which is created at initial
    //                        call to kv_create
    //  m_sectorSize        - sector size of the IO memory
    //  m_poolIdsVector     - vector of pool ids in a map, each thread will
    //                        have a vector of pool ids that are created by
    //                        them
    //  m_options           - options that are obtained by parsing config file
    //
    static int m_kvId;
    static uint32_t m_sectorSize;
    static std::vector<int> m_poolIdsVector;
    static KvOptions m_options;
    static uint64_t m_timeDelta[M_MAX_THREADS];
};
