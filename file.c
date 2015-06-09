/*
* Implementation of FUSE file handles
* 
*/

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/types.h>
#include <unistd.h>


#include "log.h"
#include "structs.h"
#include "file.h"



int file_write_header(int fd, compressor_t *compressor, off_t size)
{
	header_t fh;

	assert(fd >= 0);
	assert(compressor);
	assert(size != (off_t) -1);

	fh.id[0] = '\037';
	fh.id[1] = '\135';
	fh.id[2] = '\211';
	fh.type = compressor->type;
	fh.size = to_le64(size);

	DEBUG_("writing header to %d at %zd\n", fd, lseek(fd, 0, SEEK_CUR));
	return write(fd, &fh, sizeof(fh));
}


/**
 * Returns type of compression and real size of the file if file is compressed.
 * If the header is invalid, the size will not be touched, and the compressor
 * will be set to NULL.
 *
 * @param fd
 * @param **compressor
 * @param *size
 *
 * @return  0 - compressor and size assigned or invalid header
 *         -1 - file i/o error
 */
int file_read_header_fd(int fd, compressor_t **compressor, off_t *size)
{
	int           r;
	header_t      fh;

	assert(fd >= 0);
	assert(compressor);
	assert(size);

	DEBUG_("reading header from %d at %zd\n", fd, lseek(fd, 0, SEEK_CUR));
	r = read(fd, &fh, sizeof(fh));
	if (r == -1)
		return r;

	if (r == sizeof(fh))
	{
		fh.size = from_le64(fh.size);
		*compressor = file_compressor(&fh);
		if (*compressor)
			*size       = fh.size;
	}
	return 0;
}


int file_open(const char *filename, int mode)
{
	int         fd;
	int         newmode;
	struct stat buf;

	// Try to open file, if we dont have access, force it.
	//
	fd = open(filename, mode);
	if (fd == FAIL && (errno == EACCES || errno == EPERM))
	{
		DEBUG_("fail with EACCES or EPERM");
		// TODO: chmod failing in the start here shouldn't be critical, but size will be invalid if it fails.
		// Grab old file info.
		//
		if (stat(filename,&buf) == FAIL)
		{
			return FAIL;
		}
		newmode = buf.st_mode | ALLMODES;
		if (chmod(filename,newmode) == FAIL)
		{
			return FAIL;
		}
		fd = open(filename, mode);
		if (fd == FAIL)
		{
			return FAIL;
		}
		if (chmod(filename,buf.st_mode) == FAIL)
		{
			return FAIL;
		}
	} else { DEBUG_("fail"); }
	return fd;
}

inline void file_close(int *fd)
{
	assert(fd);
	assert(*fd > FAIL);

	if (close(*fd) == -1)
	{
		CRIT_("Failed to close fd!");
		//exit(EXIT_FAILURE);
	};

	*fd = -1;
}

