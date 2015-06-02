//A simple Fuse implementation
//Author: XZ.
//Only support 'ls' operations
/*
 Must put c file right after -Wall. Weird
 
gcc -Wall simpleFuse_v1.c -D_FILE_OFFSET_BITS=64 -I/usr/include/fuse  -pthread -L/usr/lib -lfuse -o hello 
*/
#ifndef FUSE_USE_VERSION
#define  FUSE_USE_VERSION 26
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

static int simple_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
							off_t offset, struct fuse_file_info* fi){
	return filler(buf, "hello-simpleFUSE!", NULL, 0);
}

static int simple_getattr(const char* path, struct stat* st){
	
	memset(st, 0, sizeof(struct stat));

	if(strcmp(path, "/") == 0){
		//the root directory
		st->st_mode = 0755 | S_IFDIR;
	}else{
		st->st_mode = 0777 | S_IFREG;
	}

	return 0;
}

static struct fuse_operations simple_ops={
	.readdir = simple_readdir,
	.getattr = simple_getattr,
};

int main(int argc, char* argv[])
{

	return fuse_main(argc, argv, &simple_ops, NULL);
}