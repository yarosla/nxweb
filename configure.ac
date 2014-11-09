# Copyright (c) 2011-2013 Yaroslav Stavnichiy <yarosla@gmail.com>
#
# This file is part of NXWEB.
#
# NXWEB is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License
# as published by the Free Software Foundation, either version 3
# of the License, or (at your option) any later version.
#
# NXWEB is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with NXWEB. If not, see <http://www.gnu.org/licenses/>.

AC_INIT([nxweb], [3.3.0-dev])
AC_CONFIG_HEADER(src/include/nxweb/config.h)
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE
AC_PROG_CC([gcc])
AM_PROG_CC_C_O
AC_PROG_LIBTOOL

m4_include([m4/m4_ax_python_devel.m4])

AC_CANONICAL_HOST
case $host_os in
  linux*)
        ;;
  *)
        AC_MSG_ERROR([$host_os is not currently supported])
        ;;
esac

AC_CHECK_HEADER([sys/eventfd.h], AC_DEFINE([HAVE_EVENTFD], [1], [eventfd() present]), AC_MSG_ERROR(*** Required header sys/eventfd.h was not found. Can't continue.))

AC_CHECK_LIB(rt, clock_gettime, AC_DEFINE([HAVE_RT_CLOCK], [1], [clock_gettime() present]), AC_MSG_ERROR(*** Required library librt was not found. Can't continue.))

AC_ARG_WITH(gnutls, AS_HELP_STRING([--with-gnutls[=PATH]], [compile with SSL support]), , with_gnutls="no")
if test $with_gnutls != "no"
then
  if test $with_gnutls != "yes"
  then
    export PKG_CONFIG_PATH="$with_gnutls:$with_gnutls/pkgconfig:$with_gnutls/lib/pkgconfig:$PKG_CONFIG_PATH"
    GNUTLS_BIN_DIR="$with_gnutls/bin/"
    AC_SUBST(GNUTLS_BIN_DIR)
  fi
  echo "PKG_CONFIG_PATH=$PKG_CONFIG_PATH"
  PKG_CHECK_MODULES([GNUTLS], [gnutls >= 3.0.11], [AC_DEFINE([WITH_SSL], [1], [Use GNUTLS])])
  echo "GNUTLS_CFLAGS=$GNUTLS_CFLAGS"
  echo "GNUTLS_LIBS=$GNUTLS_LIBS"
  echo "GNUTLS_BIN_DIR=$GNUTLS_BIN_DIR"
fi
AM_CONDITIONAL([WITH_SSL], [test $with_gnutls != "no"])

AC_ARG_ENABLE(certificates, AS_HELP_STRING([--disable-certificates], [disable SSL certificate generation]), , enable_certificates="yes")
AM_CONDITIONAL([GENERATE_CERTIFICATES], [test $enable_certificates = "yes" -a $with_gnutls != "no"])

AC_ARG_ENABLE(postinstall, AS_HELP_STRING([--disable-postinstall], [disable post-install ldconfig]), , enable_postinstall="yes")
AM_CONDITIONAL([ENABLE_POSTINSTALL], [test $enable_postinstall = "yes"])

AC_ARG_WITH(imagemagick, AS_HELP_STRING([--with-imagemagick[=PATH]], [compile with ImageMagick support]), , with_imagemagick="no")
if test $with_imagemagick != "no"
then
  if test $with_imagemagick != "yes"
  then
    export PKG_CONFIG_PATH="$with_imagemagick:$with_imagemagick/pkgconfig:$with_imagemagick/lib/pkgconfig:$PKG_CONFIG_PATH"
    IMAGEMAGICK_BIN_DIR="$with_imagemagick/bin/"
    AC_SUBST(IMAGEMAGICK_BIN_DIR)
  fi
  echo "PKG_CONFIG_PATH=$PKG_CONFIG_PATH"
  PKG_CHECK_MODULES([IMAGEMAGICK], [MagickWand >= 6.5.0], [AC_DEFINE([WITH_IMAGEMAGICK], [1], [Use IMAGEMAGICK])])
  #IMAGEMAGICK_LIBS+=" -lMagickWand -lMagickCore"
  #AC_SUBST(IMAGEMAGICK_LIBS)
  echo "IMAGEMAGICK_CFLAGS=$IMAGEMAGICK_CFLAGS"
  echo "IMAGEMAGICK_LIBS=$IMAGEMAGICK_LIBS"
fi
AM_CONDITIONAL([WITH_IMAGEMAGICK], [test $with_imagemagick != "no"])

AC_ARG_WITH(zlib, AS_HELP_STRING([--without-zlib], [disable gzip compression support]), with_zlib=$withval, with_zlib=yes)
if test $with_zlib != "no"
then
  AC_CHECK_LIB(z, deflate, [with_zlib=yes; AC_SUBST(ZLIB_LIBS, "-lz") AC_DEFINE([WITH_ZLIB], [1], [Use zlib])], [with_zlib=no; AC_MSG_WARN(*** ZLIB was not found. You will not be able to use gzip compression)])
fi
AM_CONDITIONAL([WITH_ZLIB], [test $with_zlib = "yes"])

AC_ARG_WITH(python, AS_HELP_STRING([--with-python], [add python support]), , with_python="no")
if test $with_python != "no"
then
  AX_PYTHON_DEVEL()
  if test $pythonexists = "yes"
  then
    AC_DEFINE([WITH_PYTHON], [1], [Use Python])
    echo "PYTHON_CPPFLAGS=${PYTHON_CPPFLAGS}"
    echo "PYTHON_LDFLAGS=${PYTHON_LDFLAGS}"
    echo "PYTHON_EXTRA_LIBS=${PYTHON_EXTRA_LIBS}"
    echo "PYTHON_EXTRA_LDFLAGS=${PYTHON_EXTRA_LDFLAGS}"
  fi
fi
AM_CONDITIONAL([WITH_PYTHON], [test $pythonexists = "yes"])

AC_ARG_ENABLE(logdebug, AS_HELP_STRING([--enable-logdebug], [enable debug logging]), , enable_logdebug="no")
AM_CONDITIONAL([ENABLE_LOG_DEBUG], [test $enable_logdebug = "yes"])
AM_COND_IF([ENABLE_LOG_DEBUG], AC_DEFINE([ENABLE_LOG_DEBUG], [1], [Enable debug logging]))

AC_CHECK_FUNC(register_printf_specifier, AC_DEFINE([USE_REGISTER_PRINTF_SPECIFIER], [1], [Use register_printf_specifier() instead of register_printf_function()]))

AC_SUBST(NXWEB_EXT_LIBS, "$GNUTLS_LIBS $IMAGEMAGICK_LIBS $ZLIB_LIBS -ldl -lrt -lpthread $PYTHON_LDFLAGS")
AC_SUBST(NXWEB_EXT_CFLAGS, "$GNUTLS_CFLAGS $IMAGEMAGICK_CFLAGS $PYTHON_CPPFLAGS")

AC_SUBST(NXWEB_LIB_VERSION_INFO, "0:0:0")

AC_CONFIG_FILES([Makefile src/lib/Makefile src/include/Makefile src/bin/Makefile src/bin/nxwebc src/lib/nxweb.pc sample_config/ssl/Makefile  sample_config/modules/Makefile], [chmod +x src/bin/nxwebc])
AC_OUTPUT

AC_MSG_NOTICE([Summary of build options:

  Version:            ${VERSION}
  Host OS:            ${host_os}
  Install prefix:     ${prefix}
  Compiler:           ${CC}
  CFlags:             ${CFLAGS}
  SSL support:        $with_gnutls
  ImageMagick:        $with_imagemagick
  GZIP compression:   $with_zlib
  Python integration: $pythonexists
  Shared lib version: $NXWEB_LIB_VERSION_INFO
])
