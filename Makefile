all: msinit.c msinit.h
	gcc -o msinit msinit.c

install: all
	install -Dm 755 msinit /sbin/msinit
	install -Dm 755 msinit.d/* /etc/msinit.d/

dirs:
	mkdir /var/run/msinit
	mkdir /etc/msinit.d
