#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(fio-kv, m4_esyscmd([buildtools/version-gen .version]), sdevendrappa@fusionio.com)
AC_CONFIG_SRCDIR([src/kv_store_mgr.cpp])
AC_LANG(C++)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
m4_pattern_allow([AM_PROG_AR])
AM_PROG_AR

# Checks for programs.
AC_PROG_CXXCPP
AC_PROG_LIBTOOL

AM_CONDITIONAL([LINUX], test "x$linux_os" = xyes)

#----------------------------------------------------
MODE="-O2 -DNDEBUG"     # Production mode
#MODE="-O2 -g2 -DNDEBUG" # Profiling mode without debug symbols
#MODE="-g2"              # Debug mode with symbols
#----------------------------------------------------

SHARED_CFLAGS="-fPIC -Wall -Werror -Wno-trigraphs -Wno-sign-compare -fno-strict-aliasing -fno-common -pipe -funit-at-a-time -std=c++0x -rdynamic"

INCLUDE_PATH="-I/usr/include/nvm"

LD_PATH="-L/usr/lib/nvm -L/usr/lib/fio"

CXXFLAGS="$LD_PATH $SHARED_CFLAGS $INCLUDE_PATH $MODE"

LIBS="-lrt"

AC_C_INLINE
AC_C_CONST

# Check for c++0x
AC_COMPILE_IFELSE([AC_LANG_SOURCE(
  [[template <typename T>
    struct check
    {
      static_assert(sizeof(int) <= sizeof(T), "Not enough");
    };

    typedef check<check<bool>> right_angle_brackets;

    int a;
    decltype(a) b;

    typedef check<int> check_type;
    check_type c;
    check_type&& cr = static_cast<check_type&&>(c);]])],,
  AC_MSG_FAILURE(['$CXX $CXXFLAGS' does not accept ISO c++0x]))

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([aio.h\
                  assert.h\
                  ctype.h\
                  dlfcn.h\
                  errno.h\
                  fcntl.h\
                  float.h\
                  getopt.h\
                  linux/fs.h\
                  malloc.h\
                  map\
                  pthread.h\
                  set\
                  stdarg.h\
                  stdbool.h\
                  stdint.h\
                  stdio.h\
                  stdlib.h\
                  string\
                  string.h\
                  sys/ioctl.h\
                  sys/param.h\
                  sys/stat.h\
                  sys/types.h\
                  sys/uio.h\
                  time.h\
                  unistd.h\
                  vector],,AC_MSG_ERROR(["Missing header."]))

# Checks for required libraries
LIBS+=" -ldl"
AC_CHECK_LIB([pthread], [pthread_create], [LIBS="-lpthread $LIBS"], AC_MSG_ERROR(["Missing pthread library"]))
AC_CHECK_LIB([vsl], [vsl_open], [LIBS="-lvsl $LIBS"], AC_MSG_ERROR(["Missing vsl library"]))
AC_CHECK_LIB([nvm-primitives], [nvm_get_version], [LIBS="-lnvm-primitives $LIBS"], AC_MSG_ERROR(["Missing nvm-primivites library"]))
LDFLAGS+=" $LIBS"

AC_CHECK_HEADERS([nvm_primitives.h\
                  nvm_types.h],,AC_MSG_ERROR(["Missing header."]))


# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_CHECK_FUNCS([preadv pwritev ftruncate memset strerror clock_gettime],,AC_MSG_ERROR(["Missing function."]))

AC_CONFIG_FILES([
Makefile
])

AC_OUTPUT
