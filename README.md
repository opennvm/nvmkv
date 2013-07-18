1. OVERVIEW
2. SYSTEM REQUIREMENTS
3. GETTING STARTED WITH NVMKV
4. NVMKV API USAGE
5. NVMKV MAXIMUM SUPPORTED LIMITS
6. NVMKV SAMPLE CODE


1. OVERVIEW
===========

The ioMemory Software Development Kit (SDK) provides Primitives and APIs so applications can take advantage of the ioMemory and the Virtual Storage Layer (VSL). Because ioMemory natively operates as a primitive key-value (KV) store, applications can eliminate significant amounts of source code by eliminating duplicate logic in the ioMemory VSL. At the most basic level, the VSL inserts blocks (values) at sparse addresses (keys). The ioMemory SDK NVMKV API library builds upon this concept by providing higher-level features that utilize internal VSL primitives for the NVMKV API.



2. SYSTME REQUIREMENTS
======================


The iomemory SDK Primitives and APIs are only available for Linux OS Distributions.  Details? TBD ?



3. GETTING STARTED WITH NVMKV
=============================

Applications can begin using the NVMKV API with the following steps, which are illustrated in the sample code included in this repository:

1.  Link with the ioMemory VSL SDK libraries.
2.	Perform one of the following operations:
	a.	If you are using the NVMKV API for the first time, then create a new NVMKV store on the raw ioMemory device or on a NVMFS (TBD? is this NVMFS needed now? TBD) file system using nvm_kv_create(). Refer to the NVMKV API specifications for more detailed information. 
	b.	If you are using an existing NVMKV store, then the nvm_kv_create() API validates the NVMKV store and returns a handle, which can be used in subsequent NVMKV store API operations as described in NVMKV Store API specifications.
	c.	A NVMKV store may be further subdivided into pools. Pools provide a mechanism for aggregating groups of related key/value pairs. When utilizing a NVMKV store with pools, both the NVMKV store handle and pool id are passed as arguments to NVMKV store API operations.



4. NVMKV API USAGE
==================

For specific details on each API within the NVMKV Store library, refer to the following link: http://opennvm.github.io/nvmkv-documents/Default.htm





5. NVMKV MAXIMUM SUPPORTED LIMITS
=================================
<ul>
<li> Maximum number of pools within a store, 1048576. </li>
<li> Maximum key size, 128 bytes. </li>
<li> Maximum value size, 1 MiB – 1KiB (1 MiB less 1 KiB). See Note below. </li>
<li> Maximum number of iterators, 128. </li>
</ul>

NOTE: NVMKV has been tested with VSL block sizes of 512 Bytes, 1kb, 2kb, and 4kb.  If you are using 2kb or 4kb blocks, then the maximum value size will be 1Mb - block size.



6. NVMKV SAMPLE CODE
====================

There is sample NVMKV application code at the following link: https://github.com/opennvm/nvmkv/tree/master/docs/examples.



