inplace
=======

A command-line tool for in-place stream editing of files with stream
editors that don't know how to do that themselves.

    $ inplace somefile sed -e 's/foo/bar/g'

This is similar to the GNU sponge(1) command, which is used like this:

    $ sed -e 's/foo/bar/g' somefile | sponge somefile

The sponge usage is also supported:

    $ sed -e 's/foo/bar/g' | inplace somefile

The file edited in-place will be renamed into place unless the `-w` option is
used, then the file will be re-written and will keep its identity.

    $ inplace
    Usage:  ./inplace [options] FILE [COMMAND [ARGUMENTS]]
            ./inplace -h
            ./inplace --help

            This program runs the COMMAND, if given, with stdin from FILE,
            and saves the output of COMMAND to FILE when the COMMAND
            completes.  Any ARGUMENTS are passed to the COMMAND.
            If a COMMAND is not given then the stdin will be saved to FILE
            when end-of-file (EOF) is reached on stdin.

            Options:
             -bEXT, -b EXT, --backup EXT -> keep a backup named FILE.EXT
             -w, --write -> re-write FILE to keep file identity the same,
                            do not rename into place

            By default ./inplace renames the new FILE into place; use the
            -w option to have ./inplace rewrite the FILE..

BUILD INSTRUCTIONS
==================

    $ cc -o inplace inplace.c

There's no build system yet as this program is so simple.

TODO
====

 - Add a build system (cc -o inplace inplace.c)
 - Add manpage
 - Add examples with jq and other stream editors

