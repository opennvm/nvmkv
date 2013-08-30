usrlib_execdir = .libs
libdir = /usr/lib/nvm
includedir = /usr/include/nvm
bindir = /usr/bin
docdir = /usr/share/doc/nvm/nvmkv

ACLOCAL_AMFLAGS = -I m4
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir) @CXXFLAGS@
AM_LDFLAGS = @LDFLAGS@

lib_LTLIBRARIES = lib/libnvmkv.la
lib_libnvmkv_la_SOURCES = src/kv_expiry_manager.cpp \
					  src/kv_wrappers.cpp \
					  src/kv_iterator.cpp \
					  src/kv_layout.cpp \
					  src/kv_pool_del_manager.cpp \
					  src/kv_pool_manager.cpp \
					  src/kv_scanner.cpp \
					  src/kv_store.cpp \
					  src/kv_store_mgr.cpp \
					  src/kv_async_expiry.cpp \
					  src/nvm_kv.cpp \
					  util/kv_bitmap.cpp \
					  util/kv_buffer_pool.cpp \
					  util/kv_hash_func.cpp \
					  util/kv_sync_list.cpp

include_HEADERS = include/nvm_error.h include/nvm_kv.h

bin_PROGRAMS = bin/nvm-kv
bin_nvm_kv_SOURCES = test/nvm-kv.cpp
bin_nvm_kv_CPPFLAGS = $(AM_CPPFLAGS) -I/usr/include/libxml2
bin_nvm_kv_LDFLAGS = $(AM_LDFLAGS) -lxml2 -static
bin_nvm_kv_LDADD = $(lib_LTLIBRARIES)

doc_DATA = docs/examples/nvm_kv_sample.c docs/examples/Makefile README.md test/nvm-kv.xml
default: all

all: $(lib_LTLIBRARIES)

test: $(lib_LTLIBRARIES) $(bin_PROGRAMS)
