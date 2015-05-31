/*
* Core file of PCFS: Pagelevel Compression File System
* Author: XZ
*/

#define FUSE_USE_VERSION 26
#define MAX_OPT 10

#include <fuse.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

static char DOT = '.';

static struct fuse_operations PCFS_Oper = {
    .getattr	= PCFS_getattr,
    .readlink	= PCFS_readlink,
    .readdir	= PCFS_readdir,
    .mknod	= PCFS_mknod,
    .mkdir	= PCFS_mkdir,
    .symlink	= PCFS_symlink,
    .unlink	= PCFS_unlink,
    .rmdir	= PCFS_rmdir,
    .rename	= PCFS_rename,
    .link	= PCFS_link,
    .chmod	= PCFS_chmod,
    .chown	= PCFS_chown,
    .truncate	= PCFS_truncate,
    .utime	= PCFS_utime,
    .open	= PCFS_open,
    .read	= PCFS_read,
    .write	= PCFS_write,
    .statfs	= PCFS_statfs,
    .release	= PCFS_release,
    .fsync	= PCFS_fsync,
    .init       = PCFS_init,
    .destroy    = PCFS_destroy,
};



int main(int argc, char* argv[])
{
	int fusec = 0;
	char* fusev[argc, MAX_OPT];
	char *root = NULL;
	int ret;

	ret=fuse_main(fusec, fusev, &PCFS_Oper, NULL);
	return ret;
}

