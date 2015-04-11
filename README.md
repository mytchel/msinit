msinit
=====
A very simple init system.

Uses files in /etc/msinit.d to determine what to start and in what order.

Things can start in parallel.

Once installed look through the files in /etc/msinit.d/ before booting. And 
read the man page.

Good luck.

Notes
-----
Send SIGINT to pid 1 to shutdown. SIGQUIT to restart.

It is usefull to make services non-forking then set exits=n and restart=y.
Now when this service exits msinit will re eval the service file (so you can 
change is if you made a mistake) and restart the service.

You will almost definately want to edit msinit.d/getty/1 and 2 as they use
mingetty rather than agetty or something more popular. Or install mingetty.
