
include $(top_srcdir)/ganglia.inc

AM_CFLAGS  = -D_LARGEFILE64_SOURCE -I$(top_builddir)/include -I$(top_builddir)/lib

NOINST_LIBRARY_NAMES = libmodpython.la
PKGLIB_LIBRARY_NAMES = modpython.la

if BUILD_PYTHON3
NOINST_LIBRARY_NAMES += libmodpython3.la
PKGLIB_LIBRARY_NAMES += modpython3.la
endif

if STATIC_BUILD
noinst_LTLIBRARIES       = $(NOINST_LIBRARY_NAMES)
libmodpython_la_SOURCES  = mod_python.c
libmodpython_la_CPPFLAGS = @PYTHON_INCLUDES@
libmodpython_la_LDFLAGS  = -export-all-symbols -Bsymbolic

if BUILD_PYTHON3
libmodpython3_la_SOURCES  = mod_python.c
libmodpython3_la_CPPFLAGS = @PYTHON3_INCLUDES@
libmodpython3_la_CFLAGS   = -DBUILD_PYTHON3 $(AM_CFLAGS)
libmodpython3_la_LDFLAGS  = -export-all-symbols -Bsymbolic
endif

else
pkglib_LTLIBRARIES    = $(PKGLIB_LIBRARY_NAMES)
modpython_la_SOURCES  = mod_python.c
modpython_la_CPPFLAGS = @PYTHON_INCLUDES@
#modpython_la_LDFLAGS  = -module -avoid-version -Bsymbolic
modpython_la_LDFLAGS  = -module -avoid-version

if BUILD_PYTHON3
modpython3_la_SOURCES  = mod_python.c
modpython3_la_CPPFLAGS = @PYTHON3_INCLUDES@ $(AM_CFLAGS)
modpython3_la_CFLAGS   = -DBUILD_PYTHON3
#modpython3_la_LDFLAGS  = -module -avoid-version -Bsymbolic
modpython3_la_LDFLAGS  = -module -avoid-version
endif

EXTRA_DIST = README.in ../conf.d/modpython.conf.in
endif

README:	README.in $(FIXCONFIG)
	$(FIXCONFIG) $< $@

$(builddir)/../conf.d/modpython.conf:	$(srcdir)/../conf.d/modpython.conf.in $(FIXCONFIG)
	mkdir -p $(builddir)/../conf.d && \
	  $(FIXCONFIG) $< $@

# Note that README is listed as a dependency to be generated, but it
# is not currently installed anywhere
install-exec-hook:	$(builddir)/../conf.d/modpython.conf README
	mkdir -p $(DESTDIR)$(sysconfdir)/conf.d && \
	  $(INSTALL_DATA) $(builddir)/../conf.d/modpython.conf $(DESTDIR)$(sysconfdir)/conf.d/modpython.conf
if BUILD_PYTHON3
	$(INSTALL_DATA) ../conf.d/modpython.conf $(DESTDIR)$(sysconfdir)/conf.d/modpython3.conf
	sed -i -e 's/python/python3/g' -e 's/pyconf/py3conf/' $(DESTDIR)$(sysconfdir)/conf.d/modpython3.conf
endif

CLEANFILES = $(builddir)/../conf.d/modpython.conf README

uninstall-hook:
	rm $(DESTDIR)$(sysconfdir)/conf.d/modpython.conf

INCLUDES = @APR_INCLUDES@
