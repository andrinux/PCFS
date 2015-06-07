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

static int PCFS_open(const char *path, struct fuse_file_info *fi)
{
	int            res;
	const char    *full;
	struct stat    statbuf;
	file_t        *file;
	descriptor_t  *descriptor;
	
	full = fusecompress_getpath(path);

	DEBUG_("('%s')", full);
	STAT_(STAT_OPEN);

	descriptor = (descriptor_t *) malloc(sizeof(descriptor_t));
	if (!descriptor)
	{
		CRIT_("\tno memory");
		//exit(EXIT_FAILURE);
		return -ENOMEM;
	}

	file = direct_open(full, TRUE);

	// if user wants to open file in O_WRONLY, we must open file for reading too
	// (we need to read header...)
	//
	if (fi->flags & O_WRONLY)
	{
		fi->flags &= ~O_WRONLY;		// remove O_WRONLY flag
		fi->flags |=  O_RDWR;		// add O_RDWR flag
	}
	if (fi->flags & O_APPEND)
	{
		// TODO: Some inteligent append handling...
		//
		// Note: fusecompress_write is called with offset to the end of
		//       file - this is fuse/kernel part of work...
		//
		fi->flags &= ~O_APPEND;
	}
	

	descriptor->fd = file_open(full, fi->flags);
	if (descriptor->fd == FAIL)
	{
		UNLOCK(&file->lock);
		free(descriptor);	// This is safe, because this descriptor has
					// not yet been added to the database entry
		return -errno;
	}
	DEBUG_("\tfd: %d", descriptor->fd);

	res = fstat(descriptor->fd, &statbuf);
	if (res == -1)
	{
		CRIT_("\tfstat failed after open was ok");
		//exit(EXIT_FAILURE);
		return -errno;
	}
	
	if(S_ISREG(statbuf.st_mode) && statbuf.st_nlink > 1) {
		file->dontcompress = TRUE;
	}

	DEBUG_("\tsize on disk: %zi", statbuf.st_size);

	if (statbuf.st_size >= sizeof(header_t))
	{
		res = file_read_header_fd(descriptor->fd, &file->compressor, &statbuf.st_size);
		if (res == FAIL)
		{
			CRIT_("\tfile_read_header_fd failed");
			//exit(EXIT_FAILURE);
			return -EIO;
		}
	}
	else
	{
		// File has size smaller than size of header, set compressor to NULL as
		// default. Compressor method will be selected when write happens.
		//
		file->compressor = NULL;
	}

	DEBUG_("\topened with compressor: %s, uncompressed size: %zd, fd: %d",
		file->compressor ? file->compressor->name : "null",
		statbuf.st_size, descriptor->fd);

	descriptor->file = file;
	descriptor->offset = 0;
	descriptor->handle = NULL;
	file->accesses++;

	// The file size has to be -1 (invalid) or the same as we think it is
	//
	DEBUG_("\tfile->size: %zi", file->size);

	// Cannot be true (it is not implemented) if writing to
	// noncompressible file or into rollbacked file...
	//
	// assert((file->size == (off_t) -1) ||
	//        (file->size == statbuf.st_size));
	//
	file->size = statbuf.st_size;

	// Add ourself to the database's list of open filedata
	//
	list_add(&descriptor->list, &file->head);

	// Set descriptor in fi->fh
	//
	fi->fh = (long) descriptor;

	UNLOCK(&file->lock);

	return 0;
}


static int PCFS_read(const char *path, char *buf, size_t size, 
                        off_t offset, struct fuse_file_info *fi)
{
    int           res;
	file_t       *file;
	descriptor_t *descriptor;

	DEBUG_("('%s') size: %zd, offset: %zd", path, size, offset);
	STAT_(STAT_READ);

	descriptor = (descriptor_t *) fi->fh;
	assert(descriptor);

	file = descriptor->file;
	assert(file);

	LOCK(&file->lock);

	if (file->compressor)
	{
		res = direct_decompress(file, descriptor, buf, size, offset);
		UNLOCK(&file->lock);
	}
	else
	{
		int fd = descriptor->fd;
		UNLOCK(&file->lock);
		res = pread(fd, buf, size, offset);
	}

	if (res == FAIL)
	{
		res = -errno;

		// Read failed, invalidate file size in database. Right value will
		// be acquired later if needed.
		//
		/* XXX: locking? */
		file->size = -1;
	}

//	sched_yield();
	DEBUG_("returning %d",res);
	return res;
}

//Structure of write/read should be similar to each other
static int PCFS_write(const char *path, const char *buf, size_t size,
                          off_t offset, struct fuse_file_info *fi)
{	
	int           res;
	file_t       *file;
	descriptor_t *descriptor;

	DEBUG_("('%s') size: %zd, offset: %zd", path, size, offset);
	STAT_(STAT_WRITE);

	descriptor = (descriptor_t *) fi->fh;
	assert(descriptor);

	file = descriptor->file;
	assert(file);

	LOCK(&file->lock);
	//TODO...
	
	
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

