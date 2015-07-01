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
	header_t fh; // file handler
	
	fd.id[0] = '\037';
	fd.id[1] = '\001';
	fd.id[2] = '\002';

	int retval = 0;
	retval = write(fd, &fh, sizeof(fh));

	return retval;  
}
