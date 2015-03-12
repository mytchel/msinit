msinit
=====
A very simple init system.

Uses files in /etc/msinit.d to determine what to start and in what order.

Things can start in parallel.

Don't even attempt to boot without looking through code and msinit.d files and
changing things. (ie: the autoboot to my login, you probably don't want that.)

I will try make this easy.

Good luck.

Shit's fast. Hopefully. Yeah, it's pretty fast.

Notes
-----
Send SIGINT to pid 1 to shutdown. SIGQUIT to restart.

Reads files and files in directories in /etc/msinit.d to determine what to do. 
Have a look at msinit.d/getty to get an idea of how to use it. Dot files are 
ignored.
