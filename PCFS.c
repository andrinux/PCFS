/*
* Core file of PCFS: Pagelevel Compression File System
* This is the core file containing the implementations of callback functions.
* Each Functions must be fully implemented in cases of various calling in Apps.
*/


#define FUSE_USE_VERSION 26

#include "config.h"

#include <fuse.h>
#include <string.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#ifdef HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif
#include <sys/mman.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/resource.h>
#ifdef HAVE_SYS_FSUID_H
#include <sys/fsuid.h>
#endif
#include <ctype.h>
#include <limits.h>

#include <syslog.h>

#include <pthread.h>

#include "structs.h"
#include "globals.h"
#include "log.h"
#include "compress.h"
#include "file.h"
#include "direct_compress.h"
#include "background_compress.h"

static char DOT = '.';
static int cmpdirFd;	// Open fd to cmpdir for fchdir.

//Get the file size given a const char path.
//off_t is defined as long int.
//For compressed file, this way may not be a good method...
//So just visit descriptor->size.
off_t PCFS_getFileSize(const char* path)
{
	struct stat *stbuf = (struct stat*) malloc (sizeof(struct stat));
	int res = lstat(path, stbuf);
	int size = stbuf->st_size;
	free(stbuf); // in case of memory leak.
	if (res == -1)
		return -1;
	else
		return size;
}

static inline const char* PCFS_getpath(const char *path)
{
	if (path[1] == 0)
		return &DOT;
	return ++path;
}

static int PCFS_getattr(const char *path, struct stat *stbuf)
{
	int           res;
	const char   *full;
	file_t       *file;

	full = PCFS_getpath(path);

	DEBUG_("('%s')", full);

	/* check for magic file */
	if (full[0] == '_' && full[1] == 'f' && full[2] == 'c') {
			return -EINVAL;
	}

		res = lstat(full, stbuf);

	if (res == FAIL)
	{
		return -errno;
	}

	// For non-regular files return now.
	//
	if (!S_ISREG(stbuf->st_mode))
	{
		return 0;
	}

	// TODO: Move direct_open before lstat
	//
	file = direct_open(full, FALSE);

	// Invalid file->size: correct value may be in stbuf->st_size
	// if file is uncompressed or in header if it is compressed.
	//
	if ((file->size == (off_t) -1))
	{
		// No need to read header if file is smaller then header.
		//
		if (stbuf->st_size >= sizeof(header_t))
		{
			res = file_read_header_name(full, &file->compressor, &stbuf->st_size);
			if (res == FAIL)
			{
				UNLOCK(&file->lock);
				return -errno;
			}
		}
		file->size = stbuf->st_size;
	}
	else
	{
		// Only if a file is compressed, real uncompressed file size
		// is in file->size
		//
		if (file->compressor)
		{
			stbuf->st_size = file->size;
		}
	}

	
	// Set right time of the last status change this way because
	// there is no call that allows to change it directly on file.
	//
	// (tar checks this item and it is loudly when the result
	// is different than what it exepects)
	//
	/* seems to be incorrect, see issue #36 */
	/* stbuf->st_ctime = stbuf->st_mtime; */

	UNLOCK(&file->lock);
	return 0;
}

static int PCFS_readlink(const char *path, char *buf, size_t size)
{
	int         res;
	const char *full;

	full = PCFS_getpath(path);

	res = readlink(full, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';

	return 0;
}

static int PCFS_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	const char    *full;
	DIR           *dp;
	struct dirent *de;

	full = PCFS_getpath(path);

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
		if (!strncmp(de->d_name, PCFS_PREFIX, sizeof(PCFS_PREFIX) - 1))
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
	
	full = PCFS_getpath(path);

	DEBUG_("('%s') mode 0%o rdev 0x%x", full, mode, (unsigned int)rdev);

	file = direct_open(full, TRUE);

	fc = fuse_get_context();
#ifdef HAVE_SETFSUID
	uid = setfsuid(fc->uid);
#endif
#ifdef HAVE_SETFSGID
	gid = setfsgid(fc->gid);
#endif
	
	if (mknod(full, mode, rdev) == -1) {
		ret = -errno;
	}

	UNLOCK(&file->lock);

#ifdef HAVE_SETFSUID
	setfsuid(uid);
#endif
#ifdef HAVE_SETFSGID
	setfsgid(gid);
#endif
	return ret;
}

static int PCFS_mkdir(const char *path, mode_t mode)
{
	int ret = 0;
	const char *full;
	uid_t uid;
	gid_t gid;
	struct fuse_context *fc;

	full = PCFS_getpath(path);

	DEBUG_("('%s')", full);

	fc = fuse_get_context();
#ifdef HAVE_SETFSUID
	uid = setfsuid(fc->uid);
#endif
#ifdef HAVE_SETFSGID
	gid = setfsgid(fc->gid);
#endif
	
	if (mkdir(full, mode) == -1)
		ret = -errno;

#ifdef HAVE_SETFSUID
	setfsuid(uid);
#endif
#ifdef HAVE_SETFSGID
	setfsgid(gid);
#endif
	
	return ret;
}

static int PCFS_rmdir(const char *path)
{
	const char *full;
	
	full = PCFS_getpath(path);

	if (rmdir(full) == -1)
		return -errno;

	return 0;
}

static int PCFS_unlink(const char *path)
{
	int	    ret = 0;
	const char *full;
	file_t     *file;
	
	full = PCFS_getpath(path);

	DEBUG_("('%s')", full);

	file = direct_open(full, TRUE);

	int res;

		res = unlink(full);
	
	if (res == 0)
	{
		// Mark file as deleted
		//
		direct_delete(file);
	} 
	else
	{
		ret = -errno;
	}

	UNLOCK(&file->lock);

	return ret;
}

static int PCFS_symlink(const char *from, const char *to)
{
	const char *full_to;
	
	full_to = PCFS_getpath(to);
	DEBUG_("('%s' -> '%s')", from, full_to);

	if (symlink(from, full_to) == -1)
		return -errno;

	struct fuse_context *fc = fuse_get_context();
	lchown(full_to, fc->uid, fc->gid);

	return 0;
}

static int PCFS_rename(const char *from, const char *to)
{
	int         ret = 0;
	const char *full_from;
	const char *full_to;
	file_t     *file_from;
	file_t     *file_to;

	full_from = PCFS_getpath(from);
	full_to = PCFS_getpath(to);

	DEBUG_("('%s' -> '%s')", full_from, full_to);

	file_from = direct_open(full_from, TRUE);
	file_from->accesses++;
	UNLOCK(&file_from->lock);

	file_to = direct_open(full_to, TRUE);
	file_to->accesses++;
	UNLOCK(&file_to->lock);

	LOCK(&file_from->lock);
	LOCK(&file_to->lock);

	file_from->accesses--;
	file_to->accesses--;

	int res;

		res = rename(full_from, full_to);

	if (res == 0)
	{
		// Rename file_from to full_to
		//

		file_to = direct_rename(file_from, file_to);
	}
	else
	{
		ret = -errno;
	}

	UNLOCK(&file_to->lock);
	UNLOCK(&file_from->lock);

	return ret;
}

static int PCFS_link(const char *from, const char *to)
{
	const char *full_from;
	const char *full_to;
	file_t* file;
	int res;
	
	full_from = PCFS_getpath(from);
	full_to = PCFS_getpath(to);
	
	file = direct_open(full_from,TRUE);
	if(file->compressor && !do_decompress(file)) {
		res = -errno;
		UNLOCK(&file->lock);
		return res;
	}
	file->dontcompress = TRUE;
	UNLOCK(&file->lock);

	if (link(full_from, full_to) == FAIL)
		return -errno;
	
	return 0;
}

static int PCFS_chmod(const char *path, mode_t mode)
{
	const char *full;
	
	full = PCFS_getpath(path);

	int res;

		res = chmod(full, mode);

	if (res == FAIL)
		return -errno;
	
	return 0;
}

static int PCFS_chown(const char *path, uid_t uid, gid_t gid)
{
	const char *full;
	
	full = PCFS_getpath(path);

	int res;

		res = lchown(full, uid, gid);

	if (res == FAIL)
		return -errno;

	return 0;
}

static int PCFS_truncate(const char *path, off_t size)
{
	int         ret = 0;
	const char *full;
	file_t     *file;
	int fd;

	full = PCFS_getpath(path);

	DEBUG_("('%s'), new size: %zd", full, size);
	STAT_(STAT_TRUNCATE);

	file = direct_open(full, TRUE);


	// And finally do the actual decompress (note, that we only decompress
	// if size > 0, no need to run through that time consuming process
	// when we're 0'ing a file out!)
	//
	if ((size > 0 ) && file->compressor && (!do_decompress(file)))
	{
		ret = -errno;
		goto out;
	}
	if (size == 0 && file->compressor)
	{
		/* we don't have to decompress, but we still have to reset the
		   file descriptors like do_decompress() does */
		descriptor_t* descriptor = NULL;
		list_for_each_entry(descriptor, &file->head, list)
		{
			direct_close(file, descriptor);
			lseek(descriptor->fd, 0, SEEK_SET);
		}
		file->compressor = NULL;
	}

	// truncate file and reset size if all ok.
	//
	/* This is called on both truncate() and ftruncate(). Unlike truncate(),
	   ftruncate() must work even if there are no write permissions,
	   so we use the crowbar (file_open()). */
	if ((fd = file_open(full, O_WRONLY)) == FAIL)
	{
		ret = -errno;
		goto out;
	}
	if (ftruncate(fd, size) == FAIL)
	{
		ret = -errno;
		close(fd);
		goto out;
	}
	close(fd);

	file->size = size;
out:
	UNLOCK(&file->lock);

	return ret;	
}

static int PCFS_utime(const char *path, struct utimbuf *buf)
{
	const char *full;
	file_t *file;
	
	struct timeval timesval[2];
	struct timeval *timesbuf=NULL;

	if (buf != NULL)
	{
		timesval[0].tv_usec = 0;
		timesval[1].tv_usec = 0;
		timesval[0].tv_sec = buf->actime;
		timesval[1].tv_sec = buf->modtime;
		timesbuf=timesval;
	}

	full = PCFS_getpath(path);
	DEBUG_("('%s')", full);

	struct stat st;
	if (!lstat(full, &st) && !S_ISREG(st.st_mode)) {
	  if (lutimes(full, timesbuf) < 0)
	    return -errno;
          else
            return 0;
	}

	file = direct_open(full, FALSE);

	int res;

		res = lutimes(full, timesbuf);

	UNLOCK(&file->lock);

	if (res == -1)
 		return -errno;

	return 0;
}

static int PCFS_open(const char *path, struct fuse_file_info *fi)
{
	int            res;
	const char    *full;
	struct stat    statbuf;
	file_t        *file;
	descriptor_t  *descriptor;
	
	full = PCFS_getpath(path);

	DEBUG_("XZ: the full path is: ('%s')", full);
	STAT_(STAT_OPEN);

	descriptor = (descriptor_t *) malloc(sizeof(descriptor_t));
	if (!descriptor)
	{
		CRIT_("\tno memory");
		//exit(EXIT_FAILURE);
		return -ENOMEM;
	}
	//XZ: what is direct_open used for?
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
		// Note: PCFS_write is called with offset to the end of
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

static int PCFS_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
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
		//res = PageLevelDecompression(file, descriptor, buf, size, offset);
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
/**
struct fuse_file_info {
	/** Open flags.	 Available in open() and release() */
//	int flags;

	/** Old file handle, don't use */
//	unsigned long fh_old;

	/** In case of a write operation indicates if this was caused by a
	    writepage */
//	int writepage;

	/** Can be filled in by open, to use direct I/O on this file.
	    Introduced in version 2.4 */
//	unsigned int direct_io : 1;

	/** Can be filled in by open, to indicate, that cached file data
	    need not be invalidated.  Introduced in version 2.4 */
//	unsigned int keep_cache : 1;

	/** Indicates a flush operation.  Set in flush operation, also
	    maybe set in highlevel lock operation and lowlevel release
	    operation.	Introduced in version 2.6 */
//	unsigned int flush : 1;

	/** Can be filled in by open, to indicate that the file is not
	    seekable.  Introduced in version 2.8 */
//	unsigned int nonseekable : 1;

	/* Indicates that flock locks for this file should be
	   released.  If set, lock_owner shall contain a valid value.
	   May only be set in ->release().  Introduced in version
	   2.9 */
//	unsigned int flock_release : 1;

	/** Padding.  Do not use*/
//	unsigned int padding : 27;

	/** File handle.  May be filled in by filesystem in open().
	    Available in all other file operations */
//	uint64_t fh;

	/** Lock owner id.  Available in locking operations and flush */
//	uint64_t lock_owner;
//};
//*/


static int PCFS_write(const char *path, const char *buf, size_t size,
                          off_t offset, struct fuse_file_info *fi)
{
	int           res;
	file_t       *file;
	descriptor_t *descriptor;
	DEBUG_("XZ: Write Function is triggered: Target is %s.\n", path);
	DEBUG_("('%s') size: %zd, offset: %zd", path, size, offset);
	STAT_(STAT_WRITE);

	descriptor = (descriptor_t *) fi->fh;
	assert(descriptor);

	file = descriptor->file;
	assert(file);

	LOCK(&file->lock);
	//do_compress4K();

	DEBUG_("\tfile->filename: %s", file->filename);

	DEBUG_("offset: %zi, file->size: %zi", offset, file->size);

	// Decide about type of compression applied to this file.
	//
	if ((!file->dontcompress) &&
	    (file->size == 0) &&
	    (file->accesses == 1) &&
	    (offset == 0))
	{
		assert(file->compressor == NULL);
		file->compressor = choose_compressor(file);
		DEBUG_("\tcompressor set to %s",
			file->compressor ? file->compressor->name : "null");
	}
	file->dontcompress = TRUE;
	if (file->compressor)
	{
		//XZ: do the compression and write to file inside this function.
		//XZ: The header(including flags and offsets) is generated after
		//		compression, so we need to do compression first. 
		res = direct_compress(file, descriptor, buf, size, offset);
		//res= PageLevelCompression(file, descriptor, buf, size, offset);
	}
	else
	{
		int fd = descriptor->fd;
		UNLOCK(&file->lock);
		res = pwrite(fd, buf, size, offset);
		LOCK(&file->lock);
	}

	if (res == FAIL)
	{
		res = -errno;

		// Read failed, invalidate file size in database. Right value will
		// be acquired later if needed.
		//
		file->size = -1;
	}

	UNLOCK(&file->lock);

//	sched_yield();
	return res;
}
//Release the 
static int PCFS_release(const char *path, struct fuse_file_info *fi)
{
	const char   *full;
	file_t       *file;
	descriptor_t *descriptor;

	full = PCFS_getpath(path);

	descriptor = (descriptor_t *) fi->fh;
	assert(descriptor);

	file = descriptor->file;
	assert(file);

	DEBUG_("('%s')", full);
	STAT_(STAT_RELEASE);

	LOCK(&file->lock);

	if (file->compressor)
	{
		direct_close(file, descriptor);
	}

	// Close descriptor and remove it from list.
	//
	list_del(&descriptor->list);
	file->accesses--;

	DEBUG_("file_closing %s (fd %d)",path,descriptor->fd);
	/* file may have already been closed by a failing do_decompress() */
	if (descriptor->fd != -1)
	 	file_close(&descriptor->fd);
	free(descriptor);

	UNLOCK(&file->lock);

	return 0;
}

static int PCFS_fsync(const char *path, int isdatasync,
                          struct fuse_file_info *fi)
{
	int res;
	descriptor_t *descriptor;

	descriptor = (descriptor_t *) fi->fh;

#ifdef HAVE_FDATASYNC
	if (isdatasync)
		res = fdatasync(descriptor->fd);
	else
#endif
		res = fsync(descriptor->fd);

	if (res == -1)
		return -errno;

	return 0;
}

static int PCFS_statfs(const char *path, struct statvfs *stbuf)
{
	int         res;
	const char *full;

	full = PCFS_getpath(path);

	res = statvfs(full, stbuf);
	if(res == -1)
		return -errno;

	return 0;
}

#define REISERFS_SUPER_MAGIC 0x52654973

static void *PCFS_init(struct fuse_conn_info* conn)
{
	struct statfs fs;

	pthread_mutexattr_init(&locktype);
#ifdef DEBUG
#ifndef PTHREAD_MUTEX_ERRORCHECK_NP
#define PTHREAD_MUTEX_ERRORCHECK_NP PTHREAD_MUTEX_ERRORCHECK
#endif
	pthread_mutexattr_settype(&locktype, PTHREAD_MUTEX_ERRORCHECK_NP);
#endif

	pthread_mutex_init(&database.lock, &locktype);
	pthread_mutex_init(&comp_database.lock, &locktype);

	if (fchdir(cmpdirFd) == -1)
	{
		CRIT_("fchdir failed!");
		exit(EXIT_FAILURE);
	}

	// Get parameters of the underlaying filesystem and set minimal
	// filesize for background and direct compression.
	//
	if (fstatfs(cmpdirFd, &fs) == -1)
	{
		CRIT_("fstatfs failed!");
		exit(EXIT_FAILURE);
	}
	switch (fs.f_type)
	{
		case REISERFS_SUPER_MAGIC:
			// Compress all files when reiserfs detected. Reiserfs
			// should work really very good with small files...
			//
			min_filesize_background = 0;
			break;
		default:
			// Limit minimal filesize for direct to 4096 - this
			// is buffer size used by fuse. If data length is smaller
			// than this we know that this is size of the whole
			// file.
			//
			min_filesize_background =  fs.f_bsize;
			break;
	}
	DEBUG_("min_filesize_background: %d",
		min_filesize_background);

	pthread_create(&pt_comp, NULL, thread_compress, NULL);

	return NULL;
}

static void PCFS_destroy(void *arg)
{
	int r;
	struct timespec delay = {
		.tv_sec = 1,
		.tv_nsec = 0,
	};
	
	// Free database and add stuff to the background compressor if neccesary
	//
	INFO_("Compressing remaining files in cache");

	do {
		LOCK(&database.lock);
		if (database.entries == 0)
		{
			LOCK(&comp_database.lock);
			if (comp_database.entries == 0)
			{
				UNLOCK(&comp_database.lock);
				UNLOCK(&database.lock);
				break;
			}
			UNLOCK(&comp_database.lock);
		}
		else
		{
			INFO_("There are still #%d files in the cache...", database.entries);
 
			// Database contains some entries, try to purge them from it
			//
			direct_open_purge_force();
		}
		UNLOCK(&database.lock);

		r = nanosleep(&delay, NULL);
		if ((r == -1) && (errno == EINTR))
		{
			break;
		}
	} while (1);
	
	INFO_("Finished compressing background files");

	DEBUG_("Canceling pt_comp");
	pthread_cancel(pt_comp);
	pthread_cond_signal(&comp_database.cond);
	pthread_join(pt_comp, NULL);
	DEBUG_("All threads stopped!");

	statistics_print();

	file_close(&cmpdirFd);

}

static struct fuse_operations PCFS_oper = {
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

static void print_help(void)
{
	printf("Usage: %s [OPTIONS] /storage/directory "
#ifndef CONFIG_OSX
	       "["
#endif
	       "/mount/point"
#ifndef CONFIG_OSX
	       "]"
#endif
	       "\n\n", "PCFS");

	printf("\t-h                   print this help\n");
	printf("\t-v                   print version\n");
	printf("\t-c "
#ifdef HAVE_LZO2
		"lzo/"
#endif
#ifdef HAVE_BZIP2
	        "bz2/"
#endif
#ifdef HAVE_ZLIB
		"gz/"
#endif
#ifdef HAVE_LZMA
		"lzma/"
#endif
		"null   choose default compression method\n");
	printf("\t-l LEVEL             set compression level (1 to 9)\n");
	printf("\t-o ...               pass arguments to fuse library\n\n");
}

static void print_version(void)
{
	printf("%s version %d.%d.%d\n", "PCFS", 0, 9, 1);
}

#include <signal.h>
static void sigterm_handler(int sig)
{
	assert(sig == SIGTERM);
	/* use SIGTERM as a cue that we will be unmounted soon and
	   purge remaining files */
	LOCK(&database.lock);
	direct_open_purge_force();
	UNLOCK(&database.lock);
}

static int set_sigterm_handler(void)
{
	struct sigaction sa;
	struct sigaction old_sa;

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = sigterm_handler;
	sigemptyset(&(sa.sa_mask));
	sa.sa_flags = 0;

	if (sigaction(SIGTERM, NULL, &old_sa) == -1) {
		perror("PCFS: cannot get old signal handler");
		return -1;
	}

	if (sigaction(SIGTERM, &sa, NULL) == -1) {
		perror("PCFS: cannot set signal handler");
		return -1;
	}
	return 0;
}

char compresslevel[3] = "wbx";

#define MAX_OPTS 50
int main(int argc, char *argv[])
{
	int               fusec = 0;
	int               next_option;
	char             *fusev[argc + MAX_OPTS];
	char             *root = NULL;		/* uncompressed FS root */
	char		 *mountpoint = NULL;	/* compressed mountpoint */
	char             *fs_opts = NULL;
	char             *o;
	int               ret;
	int		  nccount = 0;
	int excount = 0;
	
#ifdef DEBUG
	FILE* debugout = stderr;
    printf("XZ: DEBUG is defined already.\n");
#endif
	const char* const short_options = "dfhvo:c:l:s:";
	
	int detach = 1;
	int noterm = 0; /* do not exit on SIGTERM */

	fusev[fusec++] = argv[0];
#ifdef DEBUG
	detach = 0;
#endif
	fusev[fusec++] = "-o";
	fusev[fusec++] = 
			 "kernel_cache,default_permissions";

	if (geteuid() == 0)
	{
		/* default to the behavior people expect when mounting a filesystem as root */
		fusev[fusec++] = "-o";
		fusev[fusec++] = "allow_other,suid,dev";
		noterm = 1;
	}
	
	root_fs = 0;
	read_only = 0;
	cache_decompressed_data = 0;
	decomp_cache_size = 0;
	max_decomp_cache_size = 100 * 1024 * 1024;
	dont_compress_beyond = -1;
	dedup_enabled = FALSE;
	dedup_redup = FALSE;
	int no_opt_args = 0;	/* counter for non-option args */
	
	do {
		if (fusec >= argc + MAX_OPTS - 2)
		{
			ERR_("too many options");
			exit(EXIT_FAILURE);
		}
		next_option = getopt(argc, argv, short_options);
		switch (next_option)
		{
			case 'h':
				print_help();
				exit(EXIT_SUCCESS);

			case 'v':
				print_version();
				exit(EXIT_SUCCESS);

			case 'c':
				compressor_default = find_compressor_name(optarg);
				if (!compressor_default)
				{
					print_help();
					exit(EXIT_FAILURE);
				}
				break;

			case '?':
				print_help();
				exit(EXIT_FAILURE);

			case 'o':
				/* strsep() modifies the string, so we need to copy it */
				fs_opts = strdup(optarg);
				while (fs_opts)
				{
					o = strsep(&fs_opts, ",");
					if (!strcmp(o, "ro") || !strcmp(o, "rw"))
					{
						fusev[fusec++] = "-o";
						fusev[fusec++] = o;
						if (o[1] == 'o') read_only = 1;
						else read_only = 0;
					}
					else if (!strcmp(o, "uncompressed_binaries"))
					{
						root_fs = 1;
					}
					else if (!strcmp(o, "cache_skipped") || !strcmp(o, "cache"))
					{
						cache_decompressed_data = 1;
					}
					else if (!strncmp(o, "cache_size=", 11) && strlen(o) > 11) {
						max_decomp_cache_size = strtol(o + 11, NULL, 10) * 1024 * 1024;
						DEBUG_("max_decomp_cache_size set to %d", max_decomp_cache_size);
					}
					else if (!strncmp(o, "level=", 6) && strlen(o) == 7) {
						if(isdigit(o[6]) && o[6] >= '1' && o[6] <= '9')
							compresslevel[2] = o[6];
					}
					else if (!strcmp(o, "detach"))
					{
						detach = 1;
					}
					else if (!strcmp(o, "nodetach"))
					{
						detach = 0;
					}
					else if (!strcmp(o, "lzo") || !strcmp(o, "lzma") || !strcmp(o, "bz2") || !strcmp(o, "gz") || !strcmp(o, "null"))
					{
						compressor_default = find_compressor_name(o);
					}
					else if (!strcmp(o, "noterm"))
					{
						noterm = 1;
					}
					else if (!strcmp(o, "term"))
					{
						noterm = 0;
					}
					else if (!strncmp(o, "maxcompress=", 12) && strlen(o) > 12) {
						dont_compress_beyond = strtol(o + 12, NULL, 10) * 1024 * 1024;
						DEBUG_("dont_compress_beyond set to %zd", dont_compress_beyond);
					}
					else if (!strncmp(o, "nocompext=", 10) && strlen(o) > 10) {
						if (!user_incompressible) user_incompressible = malloc(sizeof(char*) * (nccount + 2));
						else user_incompressible = realloc(user_incompressible, sizeof(char*) * (nccount + 2));
						user_incompressible[nccount] = (char *)malloc(strlen(o + 10) + 2);
						sprintf(user_incompressible[nccount], ".%s", o + 10);
						DEBUG_("don't compress %s", user_incompressible[nccount]);
						user_incompressible[nccount + 1] = NULL;
						nccount++;
					}
					else if (!strncmp(o, "exclude=", 8) && strlen(o) > 8) {
						if (!user_exclude_paths)
						  user_exclude_paths = malloc(sizeof(char*) * (excount + 2));
						else
						  user_exclude_paths = realloc(user_exclude_paths, sizeof(char*) * (excount + 2));
						user_exclude_paths[excount] = (char *)malloc(strlen(o + 8) + 1);
						strcpy(user_exclude_paths[excount], o + 8);
						DEBUG_("don't compress/dedup prefix %s", user_exclude_paths[excount]);
						user_exclude_paths[excount + 1] = NULL;
						excount++;
					}
					else
					{
						fusev[fusec++] = "-o";
						fusev[fusec++] = o;
					}
				}
				/* fs_opts gets lost here, but there is no proper way of freeing it anyway
				   because it is used by fuse_main() */
				break;

			case 'l':
				if(strlen(optarg) == 1 && isdigit(optarg[0]) && optarg[0] >= '1' && optarg[0] <= '9')
					compresslevel[2] = optarg[0];
				else {
					print_help();
					exit(EXIT_FAILURE);
				}
				break;
			
			case 'f':
				detach = 0;
				break;
			
			case 'd':
				detach = 1;
				break;

			case 's':
#ifdef DEBUG
				debugout = fopen(optarg, "w");
				if(!debugout) {
					perror("could not open debug output");
					exit(EXIT_FAILURE);
				}
#endif
				break;
				
			case -1:
				if (no_opt_args == 0) {
					root = argv[optind + no_opt_args];
					next_option = 0;
				}
				else if (no_opt_args == 1 && argv[optind + no_opt_args]) {
					mountpoint = argv[optind + no_opt_args];
					next_option = 0;
				}
				else if (root != NULL) {
					/* no more options, root has been specified -> we're good */
					break;
				}
				else {
					print_help();
					exit(EXIT_FAILURE);
				}
				no_opt_args++;
				break;
			default:
				print_help();
				exit(EXIT_FAILURE);
		}
	} while (next_option != -1);

	if(!detach) fusev[fusec++] = "-f";

	if (!dedup_enabled) {
		fusev[fusec++] = "-o";
		fusev[fusec++] = "use_ino";
	}
	
	if (!mountpoint) {
		mountpoint = root;
	}
	fusev[fusec++] = mountpoint;
	
	// Sets the default compressor if user didn't choose one.
	//
	if (!compressor_default)
	{
#ifdef HAVE_LZO2
		compressor_default = &module_lzo;
#elif HAVE_ZLIB
		compressor_default = &module_gzip;
#elif HAVE_LZMA
		compressor_default = &module_lzma;
#elif HAVE_BZIP2
		compressor_default = &module_bz2;
#else
		compressor_default = &module_null;
#endif
	}
	
	if(compresslevel[2] == 'x') {
		switch(compressor_default->type) {
			case 1:	/* bz2 */
			case 2: /* gzip */
				compresslevel[2] = '6';
				break;
			case 4: /* LZMA */
				compresslevel[2] = '4';
				break;
			case 3: /* LZO */
			case 0: /* null */
				compresslevel[2] = 0; /* minilzo and null ignore compress level */
				break;
			default:
				ERR_("unknown compressor type %d",compressor_default->type);
				exit(EXIT_FAILURE);
				break;
		}
	}

#ifdef __linux__
	/* try to raise RLIMIT_NOFILE to fs.file-max */
	struct rlimit rl;
	char buf[80];
	int fd = open("/proc/sys/fs/file-max",O_RDONLY);
	if (fd < 0)
	{
		WARN_("failed to read fs.file-max attribute");
		goto trysomethingelse;
	}
	if (read(fd, buf, 80) < 0)
	{
		close(fd);
		goto trysomethingelse;
	}
	close(fd);
	rl.rlim_cur = rl.rlim_max = atol(buf);
	DEBUG_("setting fd limit to %zd", rl.rlim_cur);
	if (setrlimit(RLIMIT_NOFILE, &rl) < 0)
	{
		if (geteuid() == 0)
			WARN_("failed to set file descriptor limit to fs.file-max: %s", strerror(errno));
trysomethingelse:
		if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
		{
			WARN_("failed to get file descriptor limit: %s", strerror(errno));
		}
		else
		{
			/* try to raise limit as far as possible */
			DEBUG_("setting fd limit to %zd (now %zd)", rl.rlim_max, rl.rlim_cur);
			rl.rlim_cur = rl.rlim_max;
			if (setrlimit(RLIMIT_NOFILE, &rl) < 0)
				WARN_("failed to set file descriptor limit to maximum: %s", strerror(errno));
		}
	}
#endif /* __linux__ */
	
	openlog("PCFS", LOG_PERROR | LOG_CONS, LOG_USER);
#ifndef DEBUG
	setlogmask(LOG_UPTO(LOG_INFO));
#endif

#ifdef DEBUG
	int i;

	printf("Fuse started with:\n\t");
	for (i = 0; i < fusec; i++)
	{
		printf("%s ",fusev[i]);
	}
	printf("\nRoot directory: %s\n", root);
	printf("Type of compression: %s\n", compressor_default->name);
#endif

	if ((cmpdirFd = open(root, 0)) == FAIL) {
		CRIT_("Failed to open directory %s", root);
		exit(EXIT_FAILURE);
	}

	umask(0);
	
#ifdef DEBUG
	stderr = debugout;
#endif
	
	if (noterm)
	{
		if (set_sigterm_handler())
			WARN_("unable to set SIGTERM handler, will terminate on SIGTERM");
	}
	
	
	ret = fuse_main(fusec, fusev, &PCFS_oper, NULL);
	
	
	if (fs_opts) free(fs_opts);
	if (user_incompressible) {
		char **s;
		for (s = user_incompressible; *s; s++) free(*s);
		free(user_incompressible);
	}
	if (user_exclude_paths) {
	  char **s;
	  for (s = user_exclude_paths; *s; s++)
	    free(*s);
	  free(user_exclude_paths);
	}
	
	return ret;
}
