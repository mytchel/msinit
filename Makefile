DESTDIR?=
SBINDIR?=/sbin
SYSCONFDIR?=/etc
MANDIR?=/usr/man

all: bin

install: install-bin install-services install-man

bin: msinit.c msinit.h
	gcc -o msinit msinit.c -lpthread

install-bin: bin
	install -Dm 755 msinit ${DESTDIR}${SBINDIR}/msinit

install-services: msinit.d/*
	cp -r msinit.d ${DESTDIR}${SYSCONFDIR}

install-man: msinit.8
	install -D msinit.8 ${DESTDIR}${MANDIR}/man8/msinit.8
	gzip ${DESTDIR}${MANDIR}/man8/msinit.8

uninstall:
	rm ${DESTDIR}${SBINDIR}/msinit
	rm -r ${DESTDIR}${SYSCONFDIR}/msinit.d
	rm ${DESTDIR}${MANDIR}/man8/msinit.8.gz

