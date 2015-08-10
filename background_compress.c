
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "structs.h"
#include "globals.h"
#include "file.h"
#include "compress.h"
//#include "dedup.h"
#include "log.h"
#include "direct_compress.h"
#include "background_compress.h"

/**
 * Add entry to the list of the files that will be compressed or
 * deduplicated later.
 *
 * @param file File that should be compressed/deduped later
 * @param dedup Set to make a deduplication entry.
 */
void background_compress_dedup(file_t *file, int dedup)
{
	compress_t *entry;

	// This function must be called with read lock
	//
	NEED_LOCK(&file->lock);

	DEBUG_("add '%s' to compress queue", file->filename);

	/* For compression entries, compressor must not be set. */
	assert(dedup || !file->compressor);

	// Increase accesses by 1, so entry is not flushed from the database.
	//
	file->accesses++;

	entry = (compress_t *) malloc(sizeof(compress_t));
	if (!entry)
	{
		ERR_("malloc failed!");
		return;
	}
	entry->file = file;
	entry->is_dedup = dedup;

	// Add us to the database
	//
	LOCK(&comp_database.lock);
	list_add_tail(&entry->list, &comp_database.head);
	pthread_cond_signal(&comp_database.cond);
	comp_database.entries++;
	UNLOCK(&comp_database.lock);
}

void background_compress(file_t *file)
{
	background_compress_dedup(file, 0);
}

void background_dedup(file_t *file)
{
	background_compress_dedup(file, 1);
}

void *thread_compress(void *arg)
{
	file_t     *file;
	compress_t *entry;
	
	while (TRUE)
	{
		// Lock compressor database and check for new entries
		//
		LOCK(&comp_database.lock);

		// Wait for new entry in database
		//
		while (list_empty(&comp_database.head))
		{
			pthread_cond_wait(&comp_database.cond, &comp_database.lock);
		}
		STAT_(STAT_BACKGROUND_COMPRESS);

		// Grab first comp_entry
		//
		entry = list_entry((&comp_database.head)->next, typeof(*entry), list);
		assert(entry);
		
		// Get file from entry
		//
		file = entry->file;
		assert(file);

		// We have file, we can delete and free entry
		//
		list_del(&entry->list);
		comp_database.entries--;

		UNLOCK(&comp_database.lock);

		// This is safe, entry cannot be freed because entry->accesses++ in
		// background_compress_dedup()
		//
		LOCK(&file->lock);

		// entry->accesses must be > 0 because it was added
		// by background_compress_dedup()
		//
		assert(file->accesses > 0);
		        /* compression entry */
		        
			// If entry->accesses = 1 we're the only one accessing it, also only
			// try to compress files that hasn't been deleted
			// and hasn't been compressed
			//
			if ((file->accesses == 1) && (!file->deleted) && (!file->compressor) &&
			    (dont_compress_beyond == -1 || file->size < dont_compress_beyond) )
			{
				DEBUG_("compressing '%s'", file->filename);
				do_compress(file);
			}
		free(entry);

		// Restore entry->accesses to original value
		// (@see background_compress_dedup())
		//
		file->accesses--;

		UNLOCK(&file->lock);
	}

}
