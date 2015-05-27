# UserFS: A Userspace File System Implementation 

~~~~~~~
   ____   ___  ___  ___               
 |  _,\ / __|| __|/ __|              The PageLevel Compression File System
 |  __/  (__ | _| \__ \              A no-overhead transparent compression   
 |_|    \___||_|  |___/              https://github.com/andrinux/UserFS    
~~~~~~         

A filesystem in which data and metadata are provided by an ordinary
userspace process.  The filesystem can be accessed normally through
the kernel interface.

TO Do some really interesting things...
:)

##News
* May-24-2015: Create a new file(touch) without compression
* May-14-2015: finish the framework, seems compressor module doesn't work well
* May-03-2015: try to switch to FUSE, kernel module is not an easy thing

## Project Goals
* Faster speed.
* Simpler & smaller footprint.
* Lower memory usage.
* Concurrent, in memory access.
* More on the way... :)

##Technology
It is pretty hard to debug a kernel space file system. So I decide to try a userspace one first and migrate it to kernel version.


##How did I do

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
