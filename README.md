msinit
=====
A very simple init system.
Pretty much everything is hardcoded. Yes, that is a bad idea.
Don't even attempt to boot without looking through code and changing things. (ie: the autoboot to my login, you probably don't want that.)
I will try make this easy.
Good luck.
Shit's fast. Hopefully. Yeah, it's pretty fast.

Notes
-----
Runs whatever file is at /etc/msinit.start on boot and /etc/msinit.stop on shutdown.

Send SIGINT to pid 1 to shutdown.

Needs folders /etc/msinit.d and /var/run/msinit.

Works on crux 3.1. Though I had to edit "/sbin/start_udev" and have it make /dev/pts and /dev/shm, not sure where they are made with sysvinit/rc.
