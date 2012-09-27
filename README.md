What is this?
=============

The intention with this project is to show you how you may utilize
libcouchbase to implement a filesystem on top of CBFS. CBFS is a
project from Couchbase Labs implemented in the go language, and
provides its own client to access the data stored in the cluster. A
filesystem interface would make it a lot easier for users to
communicate with the cluster, so I figured I should write up a small
example on how you could do that.

Luckily for us we don't have to implement a new kernel driver for
this; we can utilize the FUSE project for this (see
http://fuse.sourceforge.net ) which allows us to implement the
filesystem in userspace. Given that I'm the original author of
libcouchbase we'll be using libcouchbase to communicate with CBFS ;)

Installing FUSE
---------------

So let's go ahead and install FUSE on your system.

###Mac###

FUSE is available in homebrew:

    $ brew install fuse4x

Homebrew won't install the kernel drivers for you, so you have to
execute the following commands:

    $ sudo cp -rfX /usr/local/Cellar/fuse4x-kext/0.9.1/Library/Extensions/fuse4x.kext /Library/Extensions
    $ sudo chmod +s /Library/Extensions/fuse4x.kext/Support/load_fuse4x

Installing libcouchbase
-----------------------

I had to extend libcouchbase with a small patch that isn't available
in any released binary package yet (as of September 27th 2012), but do
not fear; installing libcouchbase is a piece of cake:

    $ git clone git://github.com/couchbase/libcouchbase
    $ cd libcouchbase
    $ ./config/autorun.sh
    $ ./configure --prefix=/opt/couchbase
    $ gmake all check && sudo gmake install

Installing the FUSE driver
--------------------------

Execute the following command to build the driver:

    $ gmake

The makefile will look in /opt/couchbase for the libcouchbase libs,
but you may override this by specifying the LCB_ROOT variable to make:

    $ gmake LCB_ROOT=/opt/local

You should get a binary named mount_cbfs in the current directory.

Mounting the CBFS filesystem
============================

You mount the CBFS filesystem by executing the mount_cbfs program:

    $ mkdir /tmp/cbfs
    $ ./mount_cbfs -f -s /tmp/cbfs

The *-f* option tells mount_cbfs to stay in the foreground, and the *-s*
option tells mount_cbfs that it should be operating in a single threaded
mode (see limitations below). /tmp/cbfs is the mount point of our
filesystem.

The content of your cbfs cluster should be available in /tmp/cbfs :)

If you want to connect to a CBFS/Couchbase server that isn't running
on your own machine you need to create a configuration file named
config.json in the current working directory with the following
content:

    {
        "cbfs_host" : "localhost:8484",
        "cbfs_username" : "",
        "cbfs_password" : "",
        "couchbase_host" : "localhost:8091",
        "couchbase_username" : "cbfs",
        "couchbase_password" : "",
        "couchbase_bucket" : "cbfs"
    }

Limitations
===========

Given that this is a really rough prototype I've not spent a lot of
time making it flexible. It shouldn't be hard to extend it, but for
now I've got the following limitations:

* Single threaded - I don't use any form for locking on the
  libcouchbase instance (and I don't use a pool of them), so if you
  try to use multiple threads it'll just crash on you.
* Read-only filesystem. I haven't added any write-related operations.

But note.. With these limitations I got us down to a working example
in less than 350 lines of code ;)
