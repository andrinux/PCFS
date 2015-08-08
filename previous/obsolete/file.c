/*
* File key data structures and functions
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/types.h>
#include <unistd.h>

#include "file.h"


/*
* Write the header of file. The header information locates at the begining of a file.
* Of course this part will not be compressed.
*/

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

