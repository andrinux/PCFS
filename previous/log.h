/*
* Debug information and messages when the FUSE is running.
* system logs; traces; debugs assertions
*/

#ifndef LOG_H
#define LOG_H

#include <syslog.h>


#ifndef DEBUG
#define DEBUG_ON
#define DEBUG_OFF
#define DEBUG_(fmt,...)
#else
#include <stdio.h>
extern int _debug_on;
#define DEBUG_ON (_debug_on = 1);
#define DEBUG_OFF (_debug_on = 0);
#define DEBUG_(fmt,...) \
        if(_debug_on) fprintf(stderr, "DEBUG %s %d: " fmt "\n", __PRETTY_FUNCTION__, __LINE__, ## __VA_ARGS__);
#endif

        
#endif