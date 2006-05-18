#!/bin/sh
#
# Copyright 2005-2006 Timo Hirvonen
#
# This file is licensed under the GPLv2.

msg_checking()
{
	printf "checking $@... "
}

msg_result()
{
	echo "$@"
}

msg_error()
{
	echo "*** $@"
}

# @program: program to check
# @name:    name of variable where to store the full program name (optional)
#
# returns 0 on success and 1 on failure
check_program()
{
	local program varname filename

	argc check_program $# 1 2
	program="$1"
	varname="$2"

	msg_checking "for program ${program}"
	filename=$(path_find "${program}")
	if test $? -eq 0
	then
		msg_result $filename
		test $# -eq 2 && set_var $varname $filename
		return 0
	else
		msg_result "no"
		return 1
	fi
}

cc_supports()
{
	$CC $CFLAGS "$@" -S -o /dev/null -x c /dev/null 2> /dev/null
	return $?
}

cxx_supports()
{
	$CXX $CXXFLAGS "$@" -S -o /dev/null -x c /dev/null 2> /dev/null
	return $?
}

# @flag: option flag(s) to check
#
# add @flag to CFLAGS if CC accepts it
check_cc_flag()
{
	argc check_cc_flag $# 1

	test -z "$CC" && die "check_cc_flag: CC not set"
	msg_checking "for CC flag $*"
	if cc_supports $*
	then
		CFLAGS="$CFLAGS $*"
		msg_result "yes"
		return 0
	else
		msg_result "no"
		return 1
	fi
}

# @flag: option flag(s) to check
#
# add @flag to CXXFLAGS if CXX accepts it
check_cxx_flag()
{
	argc check_cxx_flag $# 1

	test -z "$CXX" && die "check_cxx_flag: CXX not set"
	msg_checking "for CXX flag $*"
	if cxx_supports $*
	then
		CXXFLAGS="$CXXFLAGS $*"
		msg_result "yes"
		return 0
	else
		msg_result "no"
		return 1
	fi
}

# adds CC, LD, CFLAGS, LDFLAGS and SOFLAGS to config.mk
check_cc()
{
	var_default CC ${CROSS}gcc
	var_default LD $CC
	var_default CFLAGS "-O2"
	var_default LDFLAGS ""
	var_default SOFLAGS "-fPIC"
	if check_program $CC
	then
		case $(uname -s) in
			*BSD)
				CFLAGS="$CFLAGS -I/usr/local/include"
				LDFLAGS="$LDFLAGS -L/usr/local/lib"
				;;
		esac
		makefile_vars CC LD CFLAGS LDFLAGS SOFLAGS
		return 0
	fi
	return 1
}

# adds CXX, CXXLD, CXXFLAGS and CXXLDFLAGS to config.mk
check_cxx()
{
	var_default CXX ${CROSS}g++
	var_default CXXLD $CXX
	var_default CXXFLAGS "-O2"
	var_default CXXLDFLAGS ""
	if check_program $CXX
	then
		case $(uname -s) in
			*BSD)
				CXXFLAGS="$CXXFLAGS -I/usr/local/include"
				CXXLDFLAGS="$CXXLDFLAGS -L/usr/local/lib"
				;;
		esac
		makefile_vars CXX CXXLD CXXFLAGS CXXLDFLAGS
		return 0
	fi
	return 1
}

# check if CC can generate dependencies (.dep-*.o files)
# always succeeds
check_cc_depgen()
{
	msg_checking "if CC can generate dependency information"
	if cc_supports -MMD -MP -MF /dev/null
	then
		CFLAGS="$CFLAGS -MMD -MP -MF .dep-\$@"
		msg_result yes
	else
		msg_result no
	fi
	return 0
}

# check if CXX can generate dependencies (.dep-*.o files)
# always succeeds
check_cxx_depgen()
{
	msg_checking "if CXX can generate dependency information"
	if cxx_supports -MMD -MP -MF /dev/null
	then
		CXXFLAGS="$CXXFLAGS -MMD -MP -MF .dep-\$@"
		msg_result yes
	else
		msg_result no
	fi
	return 0
}

# adds AR to config.mk
check_ar()
{
	var_default AR ${CROSS}ar
	var_default ARFLAGS "-cr"
	if check_program $AR
	then
		makefile_vars AR ARFLAGS
		return 0
	fi
	return 1
}

# adds AS to config.mk
check_as()
{
	var_default AS ${CROSS}gcc
	if check_program $AS
	then
		makefile_vars AS
		return 0
	fi
	return 1
}

check_pkgconfig()
{
	if test -z "$PKG_CONFIG"
	then
		if check_program pkg-config PKG_CONFIG
		then
			makefile_vars PKG_CONFIG
		else
			# don't check again
			PKG_CONFIG="no"
			return 1
		fi
	fi
	return 0
}

# check for library FOO and add FOO_CFLAGS and FOO_LIBS to config.mk
#
# @name:    variable prefix (e.g. CURSES -> CURSES_CFLAGS, CURSES_LIBS)
# @cflags:  CFLAGS for the lib
# @libs:    LIBS to check
#
# adds @name_CFLAGS and @name_LIBS to config.mk
# CFLAGS are not checked, they are assumed to be correct
check_library()
{
	local name cflags libs

	argc check_library $# 3 3
	# make uppercase for backwards compatibility
	name=$(echo "$1" | to_upper)
	cflags="$2"
	libs="$3"

	msg_checking "for ${name}_LIBS ($libs)"
	if try_link "$libs" >/dev/null 2>&1
	then
		msg_result yes
		makefile_var ${name}_CFLAGS "$cflags"
		makefile_var ${name}_LIBS "$libs"
		return 0
	else
		msg_result no
		return 1
	fi
}

# run pkg-config
#
# @name:    variable prefix (e.g. GLIB -> GLIB_CFLAGS, GLIB_LIBS)
# @modules: the argument for pkg-config
# @cflags:  CFLAGS to use if pkg-config failed (optional)
# @libs:    LIBS to use if pkg-config failed (optional)
#
# if pkg-config fails and @libs are given check_library is called
#
# example:
#   ---
#   check_glib()
#   {
#     pkg_config GLIB "glib-2.0 >= 2.2"
#     return $?
#   }
#
#   add_check check_glib
#   ---
#   GLIB_CFLAGS and GLIB_LIBS are automatically added to Makefile
pkg_config()
{
	local name modules cflags libs

	argc pkg_config $# 2 4
	# make uppercase for backwards compatibility
	name=$(echo $1 | to_upper)
	modules="$2"

	# optional
	cflags="$3"
	libs="$4"

	check_pkgconfig
	msg_checking "for ${name}_LIBS (pkg-config)"
	if test "$PKG_CONFIG" != "no" && $PKG_CONFIG --exists "$modules" >/dev/null 2>&1
	then
		# pkg-config is installed and the .pc file exists
		libs="$($PKG_CONFIG --libs ""$modules"")"
		msg_result "$libs"

		msg_checking "for ${name}_CFLAGS (pkg-config)"
		cflags="$($PKG_CONFIG --cflags ""$modules"")"
		msg_result "$cflags"

		makefile_var ${name}_CFLAGS "$cflags"
		makefile_var ${name}_LIBS "$libs"
		return 0
	fi

	# no pkg-config or .pc file
	msg_result "no"

	if test -z "$libs"
	then
		if test "$PKG_CONFIG" = "no"
		then
			# pkg-config not installed and no libs to check were given
			msg_error "pkg-config required for $name"
		else
			# pkg-config is installed but the required .pc file wasn't found
			$PKG_CONFIG --errors-to-stdout --print-errors "$modules" | sed 's:^:*** :'
		fi
		return 1
	fi

	check_library "$name" "$cflags" "$libs"
	return $?
}

# old name
pkg_check_modules()
{
	pkg_config "$@"
}

# run <name>-config
#
# @name:    name
# @program: the -config program, default is ${name}-config
#
# example:
#   ---
#   check_cppunit()
#   {
#     app_config cppunit
#     return $?
#   }
#
#   add_check check_cppunit
#   ---
#   CPPUNIT_CFLAGS and CPPUNIT_LIBS are automatically added to config.mk
app_config()
{
	local name program cflags libs

	argc app_config $# 1 2
	name=$(echo $1 | to_upper)
	if test $# -eq 1
	then
		program="${1}-config"
	else
		program="$2"
	fi

	check_program $program || return 1

	msg_checking "for ${name}_CFLAGS"
	cflags="$($program --cflags)"
	msg_result "$cflags"

	msg_checking "for ${name}_LIBS"
	libs="$($program --libs)"
	msg_result "$libs"

	makefile_var ${name}_CFLAGS "$cflags"
	makefile_var ${name}_LIBS "$libs"
	return 0
}

try_compile()
{
	local file src obj exe

	argc try_compile $# 1 1
	file="$1"
	src=$(tmp_file prog.c)
	obj=$(tmp_file prog.o)
	exe=$(tmp_file prog)
	echo "$file" > $src || exit 1
	$CC -c $src -o $obj 2>/dev/null || exit 1
	$LD -o $exe $obj 2>/dev/null
	return $?
}

# tries to link against a lib
# 
# @ldadd:  something like "-L/usr/X11R6/lib -lX11"
try_link()
{
	local ldadd
	local file src obj exe

	argc try_link $# 1 1
	ldadd="$1"
	file="
int main(int argc, char *argv[])
{
	return 0;
}
"
	src=$(tmp_file prog.c)
	obj=$(tmp_file prog.o)
	exe=$(tmp_file prog)

	echo "$file" > $src || exit 1
	$CC -c $src -o $obj || return 1
	$LD $LDFLAGS $ldadd -o $exe $obj
	return $?
}

# check if the architecture is big-endian
#
# defines WORDS_BIGENDIAN=y/n
check_endianness()
{
	local file src obj exe

	file="
int main(int argc, char *argv[])
{
	unsigned int i = 1;

	return *(char *)&i;
}
"
	msg_checking "byte order"
	src=$(tmp_file prog.c)
	obj=$(tmp_file prog.o)
	exe=$(tmp_file prog)
	echo "$file" > $src || exit 1
	$CC -c $src -o $obj 2>/dev/null || exit 1
	$LD -o $exe $obj 2>/dev/null || return 1
	if ./$exe
	then
		msg_result "big-endian"
		WORDS_BIGENDIAN=y
	else
		msg_result "little-endian"
		WORDS_BIGENDIAN=n
	fi
	return 0
}

# check if linking against @ldadd is possible
# use check_library instead if possible
#
# @name:   user visible name
# @ldadd:  arg passed to try_link
check_lib()
{
	local name ldadd
	local output

	argc check_lib $# 2 2
	name="$1"
	ldadd="$2"
	msg_checking "for $name"
	output=$(try_link "$ldadd" 2>&1)
	if test $? -eq 0
	then
		msg_result "yes"
		return 0
	else
		msg_result "no"
		return 1
	fi
}

# check X11 libs
#
# adds X11_LIBS (and empty X11_CFLAGS) to config.mk
check_x11()
{
	local libs

	for libs in "-lX11" "-L/usr/X11R6/lib -lX11"
	do
		check_library X11 "" "$libs" && return 0
	done
	return 1
}

# check posix threads
#
# adds PTHREAD_CFLAGS and PTHREAD_LIBS to config.mk
check_pthread()
{
	local libs

	for libs in "$PTHREAD_LIBS" -lpthread -lc_r -lkse
	do
		test -z "$libs" && continue
		check_library PTHREAD "-D_REENTRANT" "$libs" && return 0
	done
	echo "using -pthread gcc option"
	makefile_var PTHREAD_CFLAGS "-pthread -D_THREAD_SAFE"
	makefile_var PTHREAD_LIBS "-pthread"
	return 0
}

# check dynamic linking loader
#
# adds DL_LIBS to config.mk
check_dl()
{
	local libs

	for libs in "-ldl -Wl,--export-dynamic" "-Wl,--export-dynamic"
	do
		check_library DL "" "$libs" && return 0
	done
	return 1
}

# check for iconv
#
# adds ICONV_CFLAGS and ICONV_LIBS to config.mk
check_iconv()
{
	if ! check_library ICONV "" "-liconv"
	then
		echo "assuming libc contains iconv"
		makefile_var ICONV_CFLAGS ""
		makefile_var ICONV_LIBS ""
	fi
	return 0
}
