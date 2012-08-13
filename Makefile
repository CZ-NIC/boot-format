# Makefile for boot_format

INSTALL=install
PREFIX=/usr

all: boot_format

boot_format: boot_format.o
	$(CC) $< -o $@

install: boot_format
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) boot_format $(DESTDIR)$(PREFIX)/bin/
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/share/boot_format
	$(INSTALL) config*.dat $(DESTDIR)$(PREFIX)/share/boot_format

clean:
	rm -rf *.o
	rm -f boot_format

