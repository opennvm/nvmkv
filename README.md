#WELCOME TO NVMKV

<ol>
	<li> OVERVIEW </li>
	<li> GETTING STARTED WITH NVMKV </li>
	<li> NVMKV API REFERENCE </li>
	<li> NVMKV SAMPLE CODE </li>
	<li> NVMKV MAXIMUM SUPPORTED LIMITS </li>
	<li> NVMKV BENCHMARKING UTILITY </li>
</ol>

## 1. OVERVIEW

A Key-Value (KV) store is a type of NoSQL database used in high-performance, data-intensive, and scale-out
environments. Persistent KV stores today use flash as a block device and are unable to fully leverage powerful
capabilities that a Flash Translation Layer (FTL) offers, such as dynamic mapping, transaction persistence, and autoexpiry.
Additionally, non-FTL-aware KV stores maintain some of the same metadata that are maintained by the
underlying FTL, resulting in wasted memory.
 
The NVMKV library, as described in the API specification, is a lightweight user space library that provides basic key-value operations
such as get, put, delete, and advanced operations such as batch put/get/delete, pools, iterator, and lookup. The library
leverages highly-optimized primitives such as sparse addressing, atomic-writes, Persistent TRIM, etc., provided by the
underlying FTL. The strong consistency guarantees of the underlying NVM Primitives allow KV to achieve high performance
combined with ACID compliance.

## 2. GETTING STARTED WITH NVMKV

See the 'DIY On-Premises -> NVMKV' section under http://opennvm.github.io/get-started.html

## 3. NVMKV API REFERENCE

http://opennvm.github.io/nvmkv-documents/

## 4. NVMKV SAMPLE CODE

https://github.com/opennvm/nvmkv/tree/master/docs/examples/

## 5. NVMKV MAXIMUM SUPPORTED LIMITS

Maximum number of pools within a store, 1048576
Maximum key size, 128 bytes.
Maximum value size, 1 MiB - 1 KiB (1 MiB less 1 KiB). See NOTE below.
Maximum number of iterators, 128.

NOTE: NVMKV has been tested with VSL block sizes of 512 Bytes, 1 KiB, 2 KiB, and 4 KiB. If you are using 2 KiB or 4 KiB blocks, then the maximum value size will be 1 MiB - block size.

## 6. NVMKV BENCHMARKING UTILITY

https://github.com/opennvm/nvmkv/tree/master/test
