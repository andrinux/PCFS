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

static int PCFS_getattr(const char *path, struct stat *stbuf)
{
    int res;
    
    return res;
}

static int PCFS_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	const char    *full;
	DIR           *dp;
	struct dirent *de;

	full = fusecompress_getpath(path);

	dp = opendir(full);
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL)
	{
		struct stat st;

		/* ignore FUSE temporary files */
		if (strstr(de->d_name, FUSE))
			continue;
		
		/* ignore internal use files */
		if (!strncmp(de->d_name, FUSECOMPRESS_PREFIX, sizeof(FUSECOMPRESS_PREFIX) - 1))
			continue;

		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0))
			break;
	}

	closedir(dp);
	return 0;
}

static int PCFS_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int ret = 0;
	const char *full;
	uid_t uid;
	gid_t gid;
	struct fuse_context *fc;
	file_t     *file;

	full = fusecompress_getpath(path);

	DEBUG_("('%s') mode 0%o rdev 0x%x", full, mode, (unsigned int)rdev);

	file = direct_open(full, TRUE);

	fc = fuse_get_context();

	if (mknod(full, mode, rdev) == -1) {
		ret = -errno;
	}

	UNLOCK(&file->lock);


	return ret;
}


static int PCFS_read(const char *path, char *buf, size_t size, 
                        off_t offset, struct fuse_file_info *fi)
{
    int           res;
    file_t       *file;
    descriptor_t *descriptor;

    return res;
}

//Structure of write/read should be similar to each other
static int PCFS_write(const char *path, const char *buf, size_t size,
                          off_t offset, struct fuse_file_info *fi)
{
    int res; //the return value
    file_t       *file;
    descriptor_t *descriptor;

    return res;
}



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

