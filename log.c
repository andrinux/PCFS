/*
 *
 */

#ifdef DEBUG

#include "log.h"

volatile statistic_t statistics[STAT_MAX + 2] = {
	{ 0, "getattr", },
	{ 0, "unlink", },
	{ 0, "rename", },
	{ 0, "truncate", },
	{ 0, "open", },
	{ 0, "release", },
	{ 0, "read", },
	{ 0, "write", },
	{ 0, "decompress", },
	{ 0, "background compress", },
	{ 0, "background compress queue", },
	{ 0, "direct open alloc", },
	{ 0, "direct read", },
	{ 0, "direct write", },
	{ 0, "fallback", },
	{ 0, "compress", },
	{ 0, "do dedup", },
	{ 0, "do undedup", },
	{ 0, "dedup discard", },
	{ 0, "", },
	{ 0, "", },
};

void statistics_print(void)
{
	int i;

	DEBUG_("Statistics:");

	for (i = 0; i < STAT_MAX; i += 3)
	{
		DEBUG_("\t%s: %d, %s: %d, %s: %d",
			statistics[i].name, statistics[i].count,
			statistics[i+1].name, statistics[i+1].count,
			statistics[i+2].name, statistics[i+2].count);
	}
}

#endif
