AC_PREREQ([2.64])
AC_INIT([ecore-wayland-immodule],
        [0.95.0],
        [],
        [ecore-wayland-immodule],
        [http://wayland.freedesktop.org/])

AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.11 foreign no-dist-gzip dist-xz])

AM_SILENT_RULES([yes])

# Check for programs
AC_PROG_CC
AC_PROG_CXX
AC_PROG_SED

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT([disable-static])

PKG_PROG_PKG_CONFIG()

PKG_CHECK_MODULES(WAYLAND, [wayland-client])
PKG_CHECK_MODULES(ECORE, [ecore-imf >= 1.6.99 ecore-wayland >= 1.6.99 ecore-input >= 1.6.99])
PKG_CHECK_MODULES(EINA, [eina >= 1.6.99])
PKG_CHECK_MODULES(EVAS, [evas >= 1.6.99])

WAYLAND_SCANNER_RULES(['$(top_srcdir)/protocol'])

AC_CONFIG_FILES([Makefile
		 src/Makefile
		 protocol/Makefile])
AC_OUTPUT
