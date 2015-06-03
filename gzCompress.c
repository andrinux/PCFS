#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

#define BUF_SIZE 4096



/**
 * Close compressed stream.
 *
 * @return  0 - Success
 * @return -1 - File system error (not compression error), errno contains error code
 */
static int gzClose(gzFile fd_gz)
{
	int res;

	res = gzclose(fd_gz);
	if (res != Z_OK)
	{
		if (res == Z_ERRNO)
		{
			ERR_("Failed to close fd_gz!");
			return -1;
		}
		CRIT_("Failed to close fd_gz (gz error): %s", gzerror(fd_gz, &res));
		//exit(EXIT_FAILURE);
		return -1;
	}
	return 0;
}