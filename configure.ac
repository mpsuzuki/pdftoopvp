# Process this file with autoconf to create configure.

AC_PREREQ([2.65])
AC_INIT([pdftoopvp],[1.24.99])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([1.11 gnu foreign dist-xz dist-bzip2 subdir-objects])
AC_LANG([C++])
AC_CONFIG_HEADERS([config.h])

# ===========================
# Find required base packages
# ===========================
AC_PROG_CC
AC_PROG_CXX
AM_PROG_CC_C_O
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL

AC_SEARCH_LIBS([dlopen],
        [dl],
        [AS_IF([test "$ac_cv_search_dlopen" != "none required"], [
                DLOPEN_LIBS="$ac_cv_search_dlopen"
        ])],
        AC_MSG_ERROR([unable to find the dlopen() function])
)
AC_SUBST(DLOPEN_LIBS)

PKG_PROG_PKG_CONFIG([0.20])

PKG_CHECK_MODULES([FREETYPE], [freetype2], [AC_DEFINE([HAVE_FREETYPE_H], [1], [Have FreeType2 include files])])
PKG_CHECK_MODULES([FONTCONFIG], [fontconfig >= 2.0.0])

# ========================================
# Specify the fontdir patch if not default
# ========================================
AC_ARG_WITH([fontdir],
	[AS_HELP_STRING([--with-fontdir=path], [Specify path to font config directory (default: fonts/conf.d/).])],
	[FONTDIR="$withval"],
	[FONTDIR="fonts/conf.d"]
)
AC_SUBST(FONTDIR)

# ================================
# Find CUPS internals (no pc file)
# ================================
AC_ARG_WITH([cups-config],
	[AS_HELP_STRING([--with-cups-config=path], [Specify path to cups-config executable.])],
	[with_cups_config="$withval"],
	[with_cups_config=system]
)

AS_IF([test "x$with_cups_config" != "xsystem"], [
	CUPSCONFIG=$with_cups_config
], [
	AC_PATH_TOOL(CUPSCONFIG, [cups-config])
	AS_IF([test -z "$CUPSCONFIG"], [
		AC_MSG_ERROR([Required cups-config is missing. Please install CUPS developer packages.])
	])
])
CUPS_CFLAGS=`$CUPSCONFIG --cflags`' -D_PPD_DEPRECATED=""'
CUPS_LIBS=`$CUPSCONFIG --image --libs`
CUPS_VERSION=`$CUPSCONFIG --version`
AC_SUBST(CUPS_CFLAGS)
AC_SUBST(CUPS_LIBS)

CUPS_DATADIR="`$CUPSCONFIG --datadir`"
AC_DEFINE_UNQUOTED(CUPS_DATADIR, "$CUPS_DATADIR", [CUPS datadir])
AC_SUBST(CUPS_DATADIR)

CUPS_SERVERROOT="`$CUPSCONFIG --serverroot`"
AC_DEFINE_UNQUOTED(CUPS_SERVERROOT, "$CUPS_SERVERROOT", [CUPS serverroot])
AC_SUBST(CUPS_SERVERROOT)

CUPS_FONTPATH="$CUPS_DATADIR/fonts"
AC_DEFINE_UNQUOTED(CUPS_FONTPATH, "$CUPS_FONTPATH", [Path to CUPS fonts dir])
AC_SUBST(CUPS_FONTPATH)

CUPS_SERVERBIN="`$CUPSCONFIG --serverbin`"
AC_DEFINE_UNQUOTED(CUPS_SERVERBIN, "$CUPS_SERVERBIN", [Path to CUPS binaries dir])
AC_SUBST(CUPS_SERVERBIN)

# =================
# Check for Poppler
# =================
AC_ARG_ENABLE(poppler, AS_HELP_STRING([--enable-poppler],[enable Poppler-based filters]),
	      enable_poppler=$enableval,enable_poppler=yes)
AM_CONDITIONAL(ENABLE_POPPLER, test x$enable_poppler = xyes)
if test x$enable_poppler = xyes; then
	PKG_CHECK_MODULES([POPPLER], [poppler])
	PKG_CHECK_MODULES([POPPLER_CPP], [poppler-cpp >= 0.19])
        orig_CFLAGS="${CFLAGS}"
        orig_CPPFLAGS="${CPPFLAGS}"
        CFLAGS="${POPPLER_CPP_CFLAGS} ${CFLAGS}"
        CPPFLAGS="${POPPLER_CPP_CFLAGS} ${CPPFLAGS}"
	AC_CHECK_HEADER([poppler-version.h], [AC_DEFINE([HAVE_CPP_POPPLER_VERSION_H],,[Define if you have poppler-cpp "poppler-version.h" header file.])], [])
        CFLAGS="${orig_CFLAGS}"
        CPPFLAGS="${orig_CPPFLAGS}"
fi

AC_ARG_ENABLE([opvp],
	[AS_HELP_STRING([--enable-opvp], [Enable filters using OpenPrinting Vector (OPVP).])],
	[enable_opvp="$enableval"],
	[enable_opvp=yes]
)
AM_CONDITIONAL(ENABLE_OPVP, test "x$enable_opvp" = "xyes")

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
