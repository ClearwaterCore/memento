# Development

This document describes how to build and test Memento.

Memento development is ongoing on Ubuntu 14.04, so the processes described
below are targetted for (and tested on) this platform.  The code has been
written to be portable, though, and should compile on other platforms once the
required dependencies are installed.

Memento consists of two main components, Memento SIP and Memento HTTP. Memento SIP is responsible for processing SIP call traffic, and Memento HTTP presents a Ut-like interface to UEs to allow them to download the call list for their subscriber.

## Memento (SIP)

Memento (SIP) is integrated into the Application Server framework provided by Sprout,
and can be run on the Sprout nodes or as a standalone node. Memento (SIP) is built as part of the [Sprout build](https://github.com/Metaswitch/sprout/blob/dev/docs/Development.md) and the code is mastered [here](https://github.com/Metaswitch/memento-as)

## Memento (HTTP)

Dependencies
------------

Memento depends on a number of tools and libraries.  Some of these are
included as git submodules, but the rest must be installed separately.

On Ubuntu 14.04,

1.  update the package list

        sudo apt-get update

2.  install the required packages

        sudo apt-get install libboost-all-dev make cmake flex bison libtool libcloog-ppl1 git gcc g++ bison flex libxml2-utils autoconf libevent-dev libzmq3-dev pkg-config libcurl4-openssl-dev valgrind devscripts debhelper
        
Getting the Code
----------------

The Memento code is all in the `memento` repository, and its submodules, which
are in the `modules` subdirectory.

To get all the code, clone the memento repository with the `--recursive` flag to
indicate that submodules should be cloned too.

    git clone --recursive git@github.com:Metaswitch/memento.git

This accesses the repository over SSH on Github, and will not work unless you have a Github account and registered SSH key. If you do not have both of these, you will need to configure Git to read over HTTPS instead:

    git config --global url."https://github.com/".insteadOf git@github.com:
    git clone --recursive git@github.com:Metaswitch/memento.git

Building Binaries
-----------------

Note that the first build can take a long time. It takes 10-15 minutes on
an EC2 m1.small instance.

To build the memento code only and all its dependencies, change to the top-level `memento`
directory and issue `make`.

On completion,

* the memento binary is in `build/bin`
* libraries on which it depends are in `usr/lib`.

Subsequent builds should be quicker, but still check all of the
dependencies. For fast builds when you've only changed memento code, change to
the `src` subdirectory below the top-level `memento` directory and then run
`make`.

Building Debian Packages
------------------------

To build Debian packages, run `make deb`.  On completion, Debian packages
are in the parent of the top-level `memento` directory.

`make deb` does a full build before building the Debian packages and, even if
the code is already built, it can take a minute or two to check all the
dependencies.  If you are sure the code has already been built, you can use
`make deb-only` to just build the Debian packages without checking the
binaries.

`make deb` and `make deb-only` can push the resulting binaries to a Debian
repository server.  To push to a repository server on the build machine, set
the `REPO_DIR` environment variable to the appropriate path.  To push (via
scp) to a repository server on a remote machine, also set the `REPO_SERVER`
environment variable to the user and server name.

Running Unit Tests
------------------

Memento uses our common infrastructure to run the unit tests. How to run the UTs, and the different options available when running the UTs are described [here](http://clearwater.readthedocs.io/en/latest/Running_unit_tests.html#c-unit-tests).
