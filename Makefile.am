#
# Copyright (c) 2020 Jonathan Perkin <jperkin@joyent.com>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#

bin_PROGRAMS=		pkgain

#
# External source files imported from elsewhere.
#
openssh_SOURCES=	external/progressmeter.c
pkg_install_SOURCES=	external/automatic.c external/dewey.c
pkg_install_SOURCES+=	external/fexec.c external/iterate.c external/lpkg.c
pkg_install_SOURCES+=	external/opattern.c external/pkgdb.c external/plist.c
pkg_install_SOURCES+=	external/var.c external/xwrapper.c

pkgain_SOURCES=		actions.c autoremove.c depends.c download.c fsops.c impact.c
pkgain_SOURCES+=		main.c order.c pkg_check.c pkg_infos.c pkg_install.c
pkgain_SOURCES+=		pkg_str.c pkgaindb.c pkgaindb_queries.c pkglist.c preferred.c
pkgain_SOURCES+=		selection.c sqlite_callbacks.c summary.c tools.c
pkgain_SOURCES+=		$(openssh_SOURCES) $(pkg_install_SOURCES)

noinst_HEADERS=		cmd.h messages.h pkgain.h pkgaindb.h tools.h
noinst_HEADERS+=	external/dewey.h external/humanize_number.h external/lib.h
noinst_HEADERS+=	external/progressmeter.h external/queue.h

#
# Conditional sources.
#
if !HAVE_HUMANIZE_NUMBER
pkgain_SOURCES+=		external/humanize_number.c
endif

#
# Required defines.
#
pkgain_CPPFLAGS=		-DMACHINE_ARCH=\""$(MACHINE_ARCH)"\"
pkgain_CPPFLAGS+=	-DPKGAIN_DBDIR=\""$(PKGAIN_DBDIR)"\"
pkgain_CPPFLAGS+=	-DPKGAIN_VERSION=\""$(VERSION)"\"
pkgain_CPPFLAGS+=	-DPKG_INSTALL_DIR=\""$(PKG_INSTALL_DIR)"\"
pkgain_CPPFLAGS+=	-DPKG_SYSCONFDIR=\""$(sysconfdir)"\"
pkgain_CPPFLAGS+=	-DPREFIX=\""$(prefix)"\"

#
# Manual pages.
#
man1_MANS=		pkgain.1
dist_pkgain_SOURCES=	pkgain.1.in
nodist_pkgain_SOURCES=	pkgain.1
pkgain.1: pkgain.1.in
	@sed -e 's,/var/db/pkgain,$(PKGAIN_DBDIR),g' \
	     -e 's,/usr/pkg/etc,$(sysconfdir),g' $(srcdir)/pkgain.1.in >$@

#
# Generated sources.
#
dist_pkgain_SOURCES+=	pkgain.sql
nodist_pkgain_SOURCES+=	pkgaindb_create.h
pkgaindb_create.h: Makefile pkgain.sql
	@echo "/* Automatically generated, DO NOT EDIT */" >$@
	@echo "#define CREATE_DRYDB \" \\" >>$@
	@sed -e 's/$$/ \\/' -e 's/\"/\\\"/g' $(srcdir)/pkgain.sql >>$@
	@echo '"'  >>$@

BUILT_SOURCES=		$(nodist_pkgain_SOURCES)
CLEANFILES=		$(BUILT_SOURCES)
