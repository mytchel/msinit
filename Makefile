DESTDIR?=
SBINDIR?=/sbin
SYSCONFDIR?=/etc
MANDIR?=/usr/man

msinit: msinit.c msinit.h
	@gcc -o msinit msinit.c -lpthread

msinit.8.gz: msinit.8
	gzip -k msinit.8

.PHONY:
clean:
	rm msinit msinit.8.gz

.PHONY:
install: install-msinit install-services install-man

.PHONY:
install-msinit: msinit
	install -Dm 755 msinit ${DESTDIR}${SBINDIR}/msinit

.PHONY:
install-services: msinit.d/*
	@if [ ! -d ${DESTDIR}${SYSCONFDIR}/msinit.d ]; then \
		mkdir -p ${DESTDIR}${SYSCONFDIR}/msinit.d; fi
	cp -r msinit.d/* ${DESTDIR}${SYSCONFDIR}/msinit.d/

.PHONY:
install-man: msinit.8.gz
	install -D msinit.8.gz ${DESTDIR}${MANDIR}/man8/msinit.8

.PHONY:
uninstall:
	rm ${DESTDIR}${SBINDIR}/msinit
	rm -r ${DESTDIR}${SYSCONFDIR}/msinit.d
	rm ${DESTDIR}${MANDIR}/man8/msinit.8.gz


