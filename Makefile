DESTDIR?=
SBINDIR?=/sbin
SYSCONFDIR?=/etc
MANDIR?=/usr/man

msinit: msinit.c msinit.h
	gcc -o msinit msinit.c -lpthread

.PHONY:
install: install-msinit install-services install-man

.PHONY:
install-msinit: msinit
	install -Dm 755 msinit ${DESTDIR}${SBINDIR}/msinit

.PHONY:
install-services: msinit.d/*
	cp -r msinit.d ${DESTDIR}${SYSCONFDIR}

.PHONY:
install-man: msinit.8
	install -D msinit.8 ${DESTDIR}${MANDIR}/man8/msinit.8
	gzip ${DESTDIR}${MANDIR}/man8/msinit.8

.PHONY:
uninstall:
	rm ${DESTDIR}${SBINDIR}/msinit
	rm -r ${DESTDIR}${SYSCONFDIR}/msinit.d
	rm ${DESTDIR}${MANDIR}/man8/msinit.8.gz

