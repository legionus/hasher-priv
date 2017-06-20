#
# Copyright (C) 2003-2013  Dmitry V. Levin <ldv@altlinux.org>
# 
# Makefile for the hasher-priv project.
#
# This file is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

PROJECT = hasher-priv
VERSION = $(shell sed '/^Version: */!d;s///;q' hasher-priv.spec)
HELPERS = getconf.sh getugid1.sh chrootuid1.sh getugid2.sh chrootuid2.sh makedev.sh maketty.sh
MAN5PAGES = $(PROJECT).conf.5
MAN8PAGES = $(PROJECT).8 hasher-useradd.8
TARGETS = $(PROJECT) hasher-privd hasher-useradd $(HELPERS) $(MAN5PAGES) $(MAN8PAGES)

sysconfdir = /etc
libexecdir = /usr/lib
sbindir = /usr/sbin
mandir = /usr/share/man
man5dir = $(mandir)/man5
man8dir = $(mandir)/man8
configdir = $(sysconfdir)/$(PROJECT)
helperdir = $(libexecdir)/$(PROJECT)
socketdir = /var/run
DESTDIR =

MKDIR_P = mkdir -p
INSTALL = install
HELP2MAN8 = help2man -N -s8
LFS_CFLAGS = $(shell getconf LFS_CFLAGS)
CHDIRUID_FLAGS = -DENABLE_SETFSUGID -DENABLE_SUPPLEMENTARY_GROUPS
WARNINGS = -Wall -W -Wshadow -Wpointer-arith -Wwrite-strings \
	-Wconversion -Waggregate-return -Wstrict-prototypes -Werror \
	-Wmissing-prototypes -Wmissing-declarations -Wmissing-noreturn \
	-Wmissing-format-attribute -Wredundant-decls -Wdisabled-optimization
CPPFLAGS = -std=gnu99 -D_GNU_SOURCE $(CHDIRUID_FLAGS) \
	$(LFS_CFLAGS) -DPROJECT_VERSION=\"$(VERSION)\" \
	-DSOCKETDIR=\"$(socketdir)\" -DPROJECT=\"$(PROJECT)\"
CFLAGS = -pipe -O2
override CFLAGS += $(WARNINGS)
LDLIBS =

SRC = hasher-priv.c cmdline.c fds.c sockets.c logging.c
OBJ = $(SRC:.c=.o)

server_SRC = hasher-privd.c \
	caller.c chdir.c chdiruid.c chid.c child.c chrootuid.c cmdline.c \
	config.c fds.c getconf.c getugid.c ipc.c killuid.c io_log.c io_x11.c \
	makedev.c mount.c net.c parent.c pass.c pty.c signal.c tty.c \
	umount.c unshare.c xmalloc.c x11.c sockets.c logging.c \
	connection.c tasks.c epoll.c logging.c pidfile.c
server_OBJ = $(server_SRC:.c=.o)

DEP = $(SRC:.c=.d) $(server_SRC:.c=.d)

.PHONY:	all install clean indent

all: $(TARGETS)

$(PROJECT): $(OBJ)
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) -o $@

hasher-privd: $(server_OBJ)
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) -o $@

install: all
	$(MKDIR_P) -m710 $(DESTDIR)$(configdir)/user.d
	$(INSTALL) -p -m640 fstab $(DESTDIR)$(configdir)/fstab
	$(INSTALL) -p -m640 system.conf $(DESTDIR)$(configdir)/system
	$(MKDIR_P) -m750 $(DESTDIR)$(helperdir)
	$(INSTALL) -p -m700 $(PROJECT) $(DESTDIR)$(helperdir)/
	$(INSTALL) -p -m755 $(HELPERS) $(DESTDIR)$(helperdir)/
	$(MKDIR_P) -m755 $(DESTDIR)$(sbindir)
	$(INSTALL) -p -m755 hasher-privd $(DESTDIR)$(sbindir)/
	$(INSTALL) -p -m755 hasher-useradd $(DESTDIR)$(sbindir)/
	$(MKDIR_P) -m755 $(DESTDIR)$(man5dir)
	$(INSTALL) -p -m644 $(MAN5PAGES) $(DESTDIR)$(man5dir)/
	$(MKDIR_P) -m755 $(DESTDIR)$(man8dir)
	$(INSTALL) -p -m644 $(MAN8PAGES) $(DESTDIR)$(man8dir)/

clean:
	$(RM) $(TARGETS) $(DEP) $(OBJ) $(server_OBJ) core *~

indent:
	indent *.h *.c

%1.sh: %.sh.in Makefile
	sed -e 's|@N@|1|g' -e 's|@helper@|$(helperdir)/$(PROJECT)|g' <$< >$@
	chmod --reference=$< $@

%2.sh: %.sh.in Makefile
	sed -e 's|@N@|2|g' -e 's|@helper@|$(helperdir)/$(PROJECT)|g' <$< >$@
	chmod --reference=$< $@

%.sh: %.sh.in Makefile
	sed -e 's|@helper@|$(helperdir)/$(PROJECT)|g' <$< >$@
	chmod --reference=$< $@

%.5: %.5.in
	sed -e 's/@VERSION@/$(VERSION)/g' <$< >$@
	chmod --reference=$< $@

hasher-useradd: hasher-useradd.in
	sed -e 's/@VERSION@/$(VERSION)/g' <$< >$@
	chmod 755 $@

%.8: % %.8.inc Makefile
	$(HELP2MAN8) -i $@.inc -o $@ ./$<

# We need dependencies only if goal isn't "indent" or "clean".
ifneq ($(MAKECMDGOALS),indent)
ifneq ($(MAKECMDGOALS),clean)

%.d:	%.c Makefile
	@echo Making dependences for $<
	@$(CC) -MM $(CPPFLAGS) $< |sed -e 's,\($*\)\.o[ :]*,\1.o $@: Makefile ,g' >$@

ifneq ($(DEP),)
-include $(DEP)
endif

endif # clean
endif # indent
