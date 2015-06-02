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