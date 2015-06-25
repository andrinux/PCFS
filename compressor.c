/*
* Wrapping of the compressing module
*/

#include <pthread.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <syslog.h>
#include <errno.h>
#include <utime.h>


/**
 * Decompress file.
 *
 * @param file Specifies file which will be decompressed.
 */
int do_decompress(file_t *file)
{
	descriptor_t *descriptor = NULL;
	compressor_t *header_compressor = NULL;
	int res;
	int fd_temp;
	int fd_source;
	char *temp;
	off_t size;
	off_t header_size;
	struct stat stbuf;
	struct utimbuf buf;

	assert(file);

	NEED_LOCK(&file->lock);

	DEBUG_("('%s')", file->filename);
	STAT_(STAT_DECOMPRESS);

	DEBUG_("file size %zd",file->size);
	struct statvfs stat;
	if(statvfs(file->filename, &stat) < 0)
		return FALSE;
	if(stat.f_bsize * stat.f_bavail < file->size) {
		if(!(geteuid() == 0 && stat.f_bsize * stat.f_bfree >= file->size)) {
			errno = ENOSPC;
			return FALSE;
		}
	}

	flush_file_cache(file);
	
	// Open file
	//
	fd_source = file_open(file->filename, O_RDWR);
	if (fd_source == FAIL)
	{
		CRIT_("open failed on '%s'", file->filename);
		//exit(EXIT_FAILURE);
		return FALSE;
	}

	// Try to read header.
	//
	res = file_read_header_fd(fd_source, &header_compressor, &header_size);
	if (res < 0) {
		CRIT_("I/O error reading header on '%s': %s", file->filename, strerror(errno));
		return FALSE;
	}
	if (!header_compressor) {
		/* This is not a compressed file (most likely it's empty)
		   we only have to reset the compressor */
		file->compressor = NULL;
		file->size = -1; /* is this safe? */
		file_close(&fd_source);
		return TRUE;
	}

	// Set compressor (it'll be unset if we're called from
	// truncate for example)
	//
	if (!file->compressor)
	{
		file->compressor = header_compressor;
	}
	assert(file->compressor == header_compressor);

	// close compressor data
	//
	list_for_each_entry(descriptor, &file->head, list)
	{
		direct_close(file, descriptor);
		DEBUG_("file_closing %s (fd %d)",file->filename,descriptor->fd);
		file_close(&descriptor->fd);
	}
	
	// pull fstat info after compressor data is flushed
	//
	res = fstat(fd_source, &stbuf);
	if (res == -1)
	{
		file_close(&fd_source);
		WARN_("fstat failed on '%s'", file->filename);
		return FALSE;
	}

	// Create temp file
	//
	temp = file_create_temp(&fd_temp);
	if (fd_temp == -1)
	{
		CRIT_("can't create tempfile for '%s'", file->filename);
		//exit(EXIT_FAILURE);
		return FALSE;
	}
	
	// do actual decompression of the file
	//

	file->status |= DECOMPRESSING;

	/* We cannot release file->lock here: we have closed
	   all file descriptors on a file that is in use! Any
	   accesses would bomb horribly. */
	size = file->compressor->decompress(fd_source, fd_temp);

	file->status &= ~DECOMPRESSING;

	if (file->status & CANCEL)
	{
		file->status &= ~CANCEL;

		pthread_cond_broadcast(&file->cond);
	}

	if ((size == (off_t) FAIL) || (size != header_size))
	{
		// Clean up temporary file
		//
		file_close(&fd_temp);
		unlink(temp);
		CRIT_("decompression of '%s' has failed!", file->filename);
		//exit(EXIT_FAILURE);
		return FALSE;
	}

	// reopen all fd's (BEFORE fchmod, so we dont get any
	// permission denied problems)
	//
	list_for_each_entry(descriptor, &file->head, list) {
		descriptor->fd = open(temp, O_RDWR);
	}

	// Chmod file, rename file and close fd to tempfile and source
	//
	res = fchown(fd_temp, stbuf.st_uid, stbuf.st_gid);
	res = fchmod(fd_temp, stbuf.st_mode);
	if (res == -1)
	{
#if 0	// some backing filesystems (vfat, for instance) do not support permissions
		CRIT_("fchmod failed on '%s'!", file->filename);
		//exit(EXIT_FAILURE);
		return FALSE;
#endif
	}

	// Rename tmpfile to real file
	//
	res = rename(temp, file->filename);
	if (res == -1)
	{
		CRIT_("Rename failed on '%s' -> '%s'!", temp, file->filename);
		//exit(EXIT_FAILURE);
		return FALSE;
	}
	free(temp);

	// Close source file
	//
	file_close(&fd_source);
	
	// Close temp file (now main file)
	//
	file_close(&fd_temp);

	// access and modification time can be only changed
	// after the descriptor is closed 
	//
	buf.actime = stbuf.st_atime;
	buf.modtime = stbuf.st_mtime;
	res = utime(file->filename, &buf);
	if (res == -1)
	{
		CRIT_("utime failed on '%s'!", file->filename);
		//exit(EXIT_FAILURE);
		return FALSE;
	}

	file->compressor = NULL;
	file->size = (off_t) size;

	return TRUE;
}
