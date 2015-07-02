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