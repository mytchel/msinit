all: bin

install: install-bin install-services

bin: msinit.c msinit.h
	gcc -o msinit msinit.c -lpthread

install-bin: bin
	install -Dm 755 msinit /sbin/msinit

install-services:
	cp -r msinit.d/* /etc/msinit.d/

dirs:
	mkdir /etc/msinit.d
