/*
* Core file of PCFS: Pagelevel Compression File System
* This is the core file containing the implementations of callback functions.
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

#include "debug.h"
#include "file.h"



static char DOT = '.';



static int PCFS_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	return 0;
}


static int PCFS_open(const char *path, struct fuse_file_info *fi)
{
	return 0;
}


static int PCFS_read(const char *path, char *buf, size_t size, 
                        off_t offset, struct fuse_file_info *fi)
{

}

//Structure of write/read should be similar to each other
static int PCFS_write(const char *path, const char *buf, size_t size,
                          off_t offset, struct fuse_file_info *fi)
{	
	return 0;
}

static inline const char* PCFS_getpath(const char *path)
{
	if (path[1] == 0)
		return &DOT;
	return ++path;
}



static struct fuse_operations PCFS_Oper = {
    .readdir	= PCFS_readdir,
    .open		= PCFS_open,
    .read		= PCFS_read,
    .write		= PCFS_write,
    .statfs		= PCFS_statfs,

};



int main(int argc, char* argv[])
{
	int fusec = 0;
	char* fusev[argc, MAX_OPT];
	char *root = NULL;
	int ret = 0;

	ret=fuse_main(fusec, fusev, &PCFS_Oper, NULL);
	return ret;
}

