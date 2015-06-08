#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


#include "structs.h"
#include "globals.h"
#include "file.h"
#include "log.h"
#include "compress.h"

#define BUF_SIZE 4096

