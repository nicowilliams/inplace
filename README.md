inplace
=======

A command-line tool for in-place stream editing of files with stream
editors that don't know how to do that themselves.

    $ inplace somefile sed -e 's/foo/bar/g'

inplace will open its file argument and will use it as the stdin of
the given command.  The stdout of the command will go to a temporary
file in the same directory as the argument file.  If the command exits
successfully then the temporary file will be `rename(2)`ed into place.

    $ inplace
    Usage: inplace file command [arguments]
           inplace -h
           inplace --help

Kinda like the GNU sponge(1) command, which is used like this:

    $ sed -e 's/foo/bar/g' somefile | sponge somefile

TODO:
 - Add a build system
 - Add manpage
 - Implement sponge(1) as well?
