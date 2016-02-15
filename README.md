# PCFS: A Userspace File System Implementation 

~~~~~~~              
  ____    ___   ___   ___               
 |  _ \  / __| | __| / __|              The Page-level Compression File System
 |  __/ ( (__  | _|  \__ \              A no-overhead transparent compression   
 |_|     \___| |_|   |___/              https://github.com/andrinux/PCFS   
~~~~~~                

A filesystem in which data and metadata are provided by an ordinary
userspace process.  The filesystem can be accessed normally through
the kernel interface.

TO Do some really interesting things which has some impact...
:)

## Feature
* support whole file compression
* support null compression
* support page-level compression

## Usage
~~~~~
$ ./autogen.sh
$ ./configure
$ make
$ ./PCFS ~/testDir
example:
$ mount
$ cp ~/Trace.log ~/testDir
$ du -h ~/Trace.log
$ du -h ~/testDir/Trace.log
~~~~~
* if you don't want to use `Makefile`, the you can try this command to compile:
~~~~~
$ gcc -D_FILE_OFFSET_BITS=64 -I/usr/include/  -lfuse -pthread  -L/usr/lib  *.c -o PCFS -D_FILE_OFFET_BITS=64  -lfuse -lrt -ldl -lz
~~~~~

## Migrate to Android
* provide Kernel sopport
* Cross compile the FUSE library and LZ library
* statically cross compile the main program
* adb push and mode change

## Project Goals
* Faster speed.
* Simpler & smaller footprint.
* Lower memory usage.
* Concurrent, in memory access.
* More on the way... :)

##Technology
I decide to try a userspace one first and migrate it to kernel version.
Actually, migrating to Kernel is always necessary.
Another project TC-Ext2 is for kernel version.

##How did I do
* Add extra information on the metadata of data blocks
* Use CPU to do the compression/decompression
 



##Related Work
This work is inspired by the following outstanding projects:
- [BtrFS](https://btrfs.wiki.kernel.org/index.php/Main_Page)
- [ZFS](https://github.com/zfsonlinux/zfs)
- [gzip](http://www.gzip.org/)
- [FUSE](http://fuse.sourceforge.net/)
- [texFuse](https://github.com/tex/fusecompress)
- [Ext4Fuse](https://github.com/gerard/ext4fuse)
- [Ext4](https://github.com/torvalds/linux/tree/master/fs/ext4)

##Can I ask questions?
Welcome! 

andrinux.loid@gmail.com
