MAINTAINERCLEANFILES = Makefile.in

AM_CFLAGS = \
-I$(top_srcdir) \
-DPACKAGE_LIB_DIR=\"$(libdir)\" \
-DPACKAGE_DATA_DIR=\"$(datadir)/$(PACKAGE)\" \
@WAYLAND_CFLAGS@ \
@ECORE_CFLAGS@ \
@EVAS_CFLAGS@ \
@EINA_CFLAGS@

pkgdir = $(libdir)/ecore/immodules

pkg_LTLIBRARIES = wayland.la
wayland_la_SOURCES = \
wayland_module.c \
wayland_imcontext.c \
wayland_imcontext.h \
text-protocol.c	\
text-client-protocol.h

wayland_la_LIBADD = \
	@WAYLAND_LIBS@ \
	@ECORE_LIBS@ \
	@EVAS_LIBS@ \
	@EINA_LIBS@

wayland_la_LDFLAGS = -no-undefined -module -avoid-version
wayland_la_LIBTOOLFLAGS = --tag=disable-static

BUILT_SOURCES = \
text-protocol.c	\
text-client-protocol.h

CLEANFILES = $(BUILT_SOURCES)

@wayland_scanner_rules@
