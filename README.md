msinit
=====
A very simple init system.

Uses files in /etc/msinit.d to determine what to start and in what order.

Things can start in parallel.

Don't even attempt to boot without looking through code and msinit.d files and
changing things.

I will try make this easy.

Good luck.

Shit's fast. Hopefully. Yeah, it's pretty fast.

Notes
-----
Send SIGINT to pid 1 to shutdown. SIGQUIT to restart.

Have a look at msinit.d/getty to get an idea of how to use it. Dot files are 
ignored. Sub dirs are fine. When refering to services in subdirs (even from 
other services in the sub dir) give the path from /etc/msinit.d/ to the servie.
ie; /etc/msinit.d/mount/mount-home is refered to as mount/mount-home.

It is usefull to make services non-forking then set exits=n and restart=y.
Now when this service exits msinit will re eval the service file (so you can 
change is if you made a mistake) and restart the service.

Not much is done by pid 1. Pretty much just setting signal handlers mounting 
/proc, /sys, remount / rw, forks, then sleeps forever. The fork then manages 
the services, each in their own thread. 
