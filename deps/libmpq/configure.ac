# the autoconf initilization.
YC_INIT(libmpq, 0.4.2, [mbroemme@plusserver.de], [libmpq])

# detect the canonical host and target build environment.
YC_CANONICAL_SYSTEM

# initialize autoconf and automake system.
AM_INIT_AUTOMAKE([no-dependencies])
YC_CONFIG_HEADERS([config.h:config.h.in])

# notices.
YC_PREREQ(2.53)
YC_REVISION($Revision: 1.6 $)

# checking for programs.
YC_PROG_LIBTOOL
YC_PROG_MAKE_SET
YC_PROG_CC
YC_SYS_LARGEFILE
YC_FUNC_FSEEKO

# check if we need to export some largefile flags.
if test "$enable_largefile" != no; then
	if test "$ac_cv_sys_file_offset_bits" != 'no'; then
		if test -z "$LFS_CFLAGS" ; then
			LFS_CFLAGS="-D_FILE_OFFSET_BITS=$ac_cv_sys_file_offset_bits"
		else
			LFS_CFLAGS="$LFS_CFLAGS -D_FILE_OFFSET_BITS=$ac_cv_sys_file_offset_bits"
		fi
	fi
	if test "$ac_cv_sys_large_files" != 'no'; then
		if test -z "$LFS_CFLAGS" ; then
			LFS_CFLAGS="-D_LARGE_FILES=1"
		else
			LFS_CFLAGS="$LFS_CFLAGS -D_LARGE_FILES=1"
		fi
	fi
	if  test "$ac_cv_sys_largefile_source" != 'no'; then
		if test -z "$LFS_CFLAGS" ; then
			LFS_CFLAGS="-D_LARGEFILE_SOURCE=1"
		else
			LFS_CFLAGS="$LFS_CFLAGS -D_LARGEFILE_SOURCE=1"
		fi
	fi
fi

# export largefile flags.
YC_SUBST(LFS_CFLAGS)

# check for zlib library.
YC_CHECK_HEADER([zlib.h], [], [YC_MSG_ERROR([*** zlib.h is required, install zlib header files])])
YC_CHECK_LIB([z], [inflateEnd], [], [YC_MSG_ERROR([*** inflateEnd is required, install zlib library files])])

# check for bzlib2 library.
YC_CHECK_HEADER([bzlib.h], [], [YC_MSG_ERROR([*** bzlib.h is required, install bzip2 header files])])
YC_CHECK_LIB([bz2], [BZ2_bzDecompressInit], [], [YC_MSG_ERROR([*** BZ2_bzDecompressInit is required, install bzip2 library files])])

# When we're running gcc 4 or greater, compile with -fvisibility=hidden.
YC_TRY_COMPILE([
#if !defined(__GNUC__) || (__GNUC__ < 4)
#error not gcc4
#endif
], [], [CFLAGS="$CFLAGS -fvisibility=hidden"])

# find python for binding
AM_PATH_PYTHON([2.4],,[:])
AM_CONDITIONAL([HAVE_PYTHON], [test "$PYTHON" != :])

# configuration files.
YC_CONFIG_FILES([libmpq.pc])
YC_CONFIG_FILES([libmpq-config],[chmod +x libmpq-config])

# creating files.
YC_OUTPUT([
Makefile
libmpq/Makefile
bindings/Makefile
bindings/d/Makefile
bindings/python/Makefile
doc/Makefile
doc/man1/Makefile
doc/man3/Makefile
tools/Makefile
])
