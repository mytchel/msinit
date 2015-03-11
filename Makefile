all: msinit.c msinit.h
	gcc -o msinit msinit.c -lpthread

install: all
	install -Dm 755 msinit /sbin/msinit
	install -Dm 755 msinit.d/* /etc/msinit.d/

dirs:
	mkdir /etc/msinit.d
