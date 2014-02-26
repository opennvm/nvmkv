#NVMKV UTILITY

## 1. BUILD

Please issue the following steps in the repository root directory
(repo-root-dir)

(i)   ./autogen.sh
(ii)  ./configure
(iii) make

If the build is successful, the nvm-kv binary should be generated under
<repo-root-dir>/bin/

## 2. INPUT

The input parameters to the utility can be provided using a XML file.
A sample XML file (nvm-kv.xml) is provided in the current directory.

<?xml version="1.0"?>
<nvmKV>

        <device name="/dev/fioa"/>
        <test smoke="true" perf="false" functional="false" dumpData="true"/>
        <perf>
            <io valueSize="1" kvCount="1000000" valueUnits="sector" batchSize="10" cacheSize="0"/>
            <!--threads count="1"/-->
            <pools maxPools="1024" poolCount="0" getPoolInfo="false"/>
            <expiry method="0" expiryInSecs="0"/>
            <jobs jobString="kv_open,kv_get_store_info,kv_put,kv_exists,kv_get,kv_iterate,kv_delete,kv_get_store_info,kv_batch_put,kv_close" verify="false" replacePuts="false" genCount="1" readExact="false"/>
        </perf>
        <dumpData filename="./kvstore_data"/>
        
</nvmKV>

## 3. EXPLANATION OF TAGS AND ATTRIBUTES

(i)  device tag: specifies properties of the device on which the benchmark is
                 being performed
     name: specifies the name of the device

(ii) test tag: specifies the type of test being performed
     smoke: runs a very basic suite of NVMKV APIs to check if basic
            functionality works
     perf: runs a performance test based on the parameters specified
           under the perf tag
     functional: runs a functional test (currently unused)
     dumpData: whether the data on media needs to be dumped to a file or not
               specified by the dumpData tag in (ix)

(iii) perf tag: specifies parameters for the performance test

(iv)  io tag: specifies the various IO parameters for the performance test
      valueSize: size of the value that needs to be used for the test. The unit for this
                 is specified by the valueUnits attribute
      kvCount: number of Key-Value pairs used for the performance test
      valueUnits: units for the value size. Currently this supports only "sector"
      batchSize: number of Key-Value pairs to be used in a batch for the batch APIs
      cacheSize: size of the collision cache (in bytes)

(v)   threads tag: specifies the properties of the threads used for the performance test
      count: number of threads used

(vi)  pools tag: specifies properties of the pools used for the test
      maxPools: maximum number of pools used for the test
      poolCount: number of pools used for the test
      getPoolInfo: whether to get information for the pool or not

(vii) expiry tag: specifies expiry information for the test
      method: specifies expiry method. "0" will disable expiry,
              "1" will use arbitrary expiry and "2" will use global expiry
      expiryInSecs: specifies the expiry value in seconds

(viii) jobs tag: specifies the APIs and other miscellaneous parameters
       jobString: specifies the APIs that need to be run in order
       verify: whether the test needs to verify the value that was returned
               after a nvm_kv_get call
       replacePuts: whether the test needs to replace the value for an existing
                    key for a nvm_kv_put
       genCount: generation count for the store
       readExact: whether the length of the buffer passed in to get the value in
                  nvm_kv_get specifies the expected length of the value or not

(ix) dumpData tag: specifies properties for dumping NVMKV data present in media
     filename: name of the file in which the data needs to be dumped
