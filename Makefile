all: msinit.c
	gcc -o msinit msinit.c

install: all
	install -Dm 755 msinit /sbin/msinit
	install -Dm 755 msinit.start /etc/msinit.start
	install -Dm 755 msinit.stop /etc/msinit.stop
