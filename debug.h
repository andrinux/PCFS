#ifndef _DEBUG_F
#define _DEBUG_F


#ifndef DEBUG
#defin DEBUG_ON
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