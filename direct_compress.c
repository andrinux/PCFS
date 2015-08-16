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
 
#include "structs.h"
#include "globals.h"
#include "log.h"
#include "list.h"
#include "file.h"
#include "direct_compress.h"
#include "compress.h"
#include "background_compress.h"
#include "utils.h"

#ifndef PAGE_SIZE
#define PAGE_SIZE (4096)
#endif

#ifndef DOUBLE_PAGE
#define DOUBLE_PAGE (PAGE_SIZE*2)
#endif

#ifndef COMPRESSED 
#define COMPRESSED 1
#endif

#ifndef UNCOMPRESSED
#define UNCOMPRESSED 0
#endif

#ifndef IMPOS_VAL
#define IMPOS_VAL (-99)
#endif


/*
* Implementation of 4KB compression, store the result into 
* @ buf: the input data.
* @ tBuf: is the tmp Buffer hold all the compBlks
* @ fBUF: is the data will be written into file.
* @ Flags: compression flags of each 4K.
* @ Offsets:For debugging purpose, the offset of the first 4K. PAGE_SIZE is 4096
* To begin, use a memory-cost way to compress: hold all the cmpBlks instead of on-the-fly compress
//int direct_compress(file_t *file, descriptor_t *descriptor, const void *buffer, size_t size, off_t offset)
*/
size_t PageLevelCompression(file_t *file, descriptor_t *descriptor, const void *buf, size_t size, off_t offset)
{
	struct compBlk* tBUF = NULL;
	struct compBlk* fBUF = NULL;
	unsigned char* Flags = NULL; //@ 1: compressed. 0: no compressed.
	unsigned short * Offsets = NULL;//@ 0->4096: compressed. 4096: no compression
	/*
	* The size after compression is tricky for the final page.(c'z that the final page is < 4096)
	* pageUsed can be calculated by cSize:
	* pU = cSize/PAGE_SIZE + (cSize%PAGE_SIZE != 0)?1:0;
	*/
	size_t rData =0; //bytes of real data
	size_t cSize = 0; // the size after compression.
	
	int pageUsed =0;
	
	//initialization of buffer memory
	int count; 
	count = size/PAGE_SIZE + (size % PAGE_SIZE == 0) ? 0 : 1;
	
	tBUF = (struct compBlk*) malloc( count* sizeof(struct compBlk));
	if(!tBUF){
		DEBUG_("Memory Error on tBUFFER allocation.\n");
		return FAIL;
	}
	
	fBUF = (struct compBlk*) malloc(count * sizeof(struct compBlk));
	if(!fBUF){
		DEBUG_("Memory Error on fBUFFER allocation.\n");
		return FAIL;
	}
	
	Flags	= (unsigned char*) malloc(count * sizeof(unsigned char));
	if(!Flags){
		DEBUG_("Memory Error on Flags allocation.\n");
		return FAIL;
	}
	
	Offsets = (unsigned short*)malloc(count * sizeof(unsigned short));
	if(!Offsets){
		DEBUG_("Memory Error on Offsets allocation.\n");
		return FAIL;
	}
	
	//Do compression
	cSize = doPageLevelCompression(tBUF, fBUF, Flags, Offsets, buf, size);
	
	pageUsed = cSize/PAGE_SIZE + (cSize % PAGE_SIZE == 0) ? 0 : 1;
	
	//Update metadata , and write file header
	descriptor->cPage = pageUsed;
	descriptor->cFlags = Flags;
	descriptor->cOffsets=Offsets;
	descriptor->cSize = cSize;
	
	//Write file data into disk- header.
	file_write_ExtHeader(file, descriptor);	
	//write Flags and Offsets
	int fd = descriptor->fd;
	int ret =0;
	ret = write(fd, Flags, pageUsed*sizeof(uchar));
	DEBUG_("write %d Flags into fd %d\n", ret, fd);
	ret = write(fd, Offsets, pageUsed*sizeof(ushort));
	DEBUG_("write %d Offsets into fd %d\n", ret, fd);
	//After write to file, need to free the mrmory allocated to buffer.
	int index = 0;
	
	for(index =0; index < pageUsed; index++){
		ret = write(fd, fBUF[index].dst, fBUF[index].dst_len);
		DEBUG_("write %d bytes into fd %d\n", ret, fd);
		rData += ret;
	}
	
	free(tBUF);
	free(fBUF);
	free(Flags);
	free(Offsets);
	
	//return values
	return rData;
}

// Do Page level compression and store the results in these four buffers.
// Support one-time sequential write.  
// retval is count of effective blocks.
size_t doPageLevelCompression(Blk_t* tBUF, Blk_t* fBUF, uchar* Flags, ushort* Offsets, 
						   const void *buf, size_t size)
{
	int index = 0;
	int fCnt = 0;
	size_t cSize = 0;
	struct compBlk* curBlk;
	
	int count = size/PAGE_SIZE + (size % PAGE_SIZE == 0) ? 0 : 1; // in total count pages: 0->count-1
	cSize = 0;
	
	for(index = 0; index < count; index++){
		//Initialize cBlk
		memset(tBUF[index].dst, 0, PAGE_SIZE);
		tBUF[index].dst_len =  2*PAGE_SIZE;
		
		//From (buf + index*PAGE_SIZE)
		curBlk = tBUF + index;
		
		//determine the effective bytes of last page
		uLong blkSize =  PAGE_SIZE;
		if(index == count -1){
			if(size % PAGE_SIZE == 0){
				blkSize = PAGE_SIZE;
			}else{
				blkSize = size % PAGE_SIZE;
			}
		}
		//do compression.
		int ret = compress(curBlk->dst, &curBlk->dst_len, 
						   (Bytef*) (buf + index*PAGE_SIZE), (uLong) blkSize);
		//update compress flag.
		if(curBlk->dst_len <= PAGE_SIZE) 
			curBlk->flag = COMPRESSED;
		else
			curBlk->flag = UNCOMPRESSED;
		
		DEBUG_("@doPageLevelCompression: Ret is %d. dst_len is %ld.", ret, curBlk->dst_len);
		
		if(Z_OK != ret ){
			DEBUG_("@doPageLevelCompression: %d block Error.\n",index);
			return FAIL;
		}else{
			DEBUG_("%2d Block: %ld becomes %ld bytes.\n",
				   index, blkSize, curBlk->dst_len);
		}
	}
	
	//So far, finished all the 4kB blocks compression and begin PACKING work.
	//tBUF->fBUF, update Offsets, update cFlags
	int cur = 0;
	while(cur + 1 < count){
		curBlk = fBUF + fCnt;
		memset(curBlk->dst, 0, PAGE_SIZE);
		
		//assert(cur < count);
		if(tBUF[cur].dst_len + tBUF[cur+1].dst_len <= PAGE_SIZE){
			memcpy(curBlk->dst, tBUF[cur].dst, tBUF[cur].dst_len);
			memcpy(curBlk->dst + tBUF[cur].dst_len, tBUF[cur+1].dst, tBUF[cur+1].dst_len);
			
			//padding.
			memset(curBlk->dst + tBUF[cur].dst_len + tBUF[cur+1].dst_len, 0, 
				   (PAGE_SIZE - tBUF[cur].dst_len - tBUF[cur+1].dst_len));
			
			//iteration.
			cur = cur + 2;
			Flags[fCnt] = COMPRESSED;
			Offsets[fCnt] = tBUF[cur].dst_len;
			cSize += PAGE_SIZE;
		}else{
			//uncompressed -or- the final page: 
			memcpy(curBlk->dst, buf + cur*PAGE_SIZE, PAGE_SIZE);
			cur = cur + 1;
			Flags[fCnt] = UNCOMPRESSED;
			Offsets[fCnt] = PAGE_SIZE;
			cSize += PAGE_SIZE;
		}
		fBUF[fCnt].dst_len = PAGE_SIZE; //Effective data(including padding 0).
		fCnt++;		
	}
	//the final page. could be compressed or not.
	//copy directly.
	if(cur == count-1){
		curBlk = fBUF + fCnt;
		memcpy(curBlk->dst, tBUF[cur].dst, tBUF[cur].dst_len);
		Flags[fCnt] = tBUF[cur].flag;
		Offsets[fCnt] = tBUF[cur].dst_len;
		fCnt++;
		cSize+= tBUF[cur].dst_len;
	}
	
	//Reorganising finished.
	return cSize;
}

int writeCompInfo(file_t *file, descriptor_t *descriptor, uchar* cFlags, ushort* cOffsets)
{
	return FAIL;
}


int readCompInfo(file_t *file, descriptor_t *descriptor, uchar* cFlags, ushort* cOffsets)
{
	//According to offset of header+offset+flags.
	
	return FAIL;
}
/*
* Decompression should be easier: just load them one by one into buf.
// Read out size Bytes from offSetInFile, and decompressed to buf.
* The compression flags are stored inside the file metadata.
* Step1: read out the compressed data.(calculate the physical offset)
* Step2: decompress to buf.
*/
//To begin, let's assume the offsetInFile is sizeof(header_t)
int PageLevelDecompression(file_t *file, descriptor_t *descriptor, void *outbuf, size_t size, off_t offsetInFile)
{
	ushort * offset = NULL;
	uchar * FLAG = NULL;
	//OffsetInfile is offset in uncompressed domain: calculate the offset in compressed domain.
	//the memory must be allocated inside the main function.
	int fd = descriptor->fd;
	
	//Store the compression information: offset and flag.
	
	//ushort * offset = descriptor->cOffsets ;
	//uchar * FLAG = descriptor->cFlags;
	int cSize = descriptor->cSize;
	int count = cSize /PAGE_SIZE;
	Bytef* inbuf = (Bytef*) malloc (cSize * sizeof(Bytef));
	//Read original compressed data into inbuf
	if(cSize != read(descriptor->fd, inbuf, cSize)){
		DEBUG_("compressed data read error.\n");
		return FAIL;
	}
	//do decompression one by one and compose output.
	int index = 0;
	
	Bytef decBuf[DOUBLE_PAGE] = { 0 };
	uLong decLen1 = PAGE_SIZE;
	uLong decLen2 = PAGE_SIZE;
		
	int ret1 = IMPOS_VAL, ret2 = IMPOS_VAL;
	int nchar = 0;
	int total = 0;
	
	while(index < count){
		memset(decBuf, 0, DOUBLE_PAGE);
		//Need to find a way to continue decompressing for two sectors.
		uLong inLen = PAGE_SIZE;
		if(FLAG[index] == COMPRESSED){
			ret1 = uncompress(decBuf, &decLen1, inbuf + PAGE_SIZE*index, inLen);
			ret2 = uncompress(decBuf+PAGE_SIZE, &decLen2, 
							  inbuf+PAGE_SIZE*index+offset[index], inLen-offset[index]);
			DEBUG_("Index = %d: dec1Len=%ld. dec2Len=%ld.\n", index, decLen1, decLen2);
		}else{
			//no compression
			ret1 = Z_OK; ret2 = Z_OK;
			decLen1 = PAGE_SIZE; 
			decLen2 = 0;
			memcpy(decBuf, inbuf + PAGE_SIZE*index, PAGE_SIZE);
		}

		if(ret1 == Z_OK && ret2 == Z_OK){
			//nchar = write(fd, decBuf, decLen1 + decLen2);
			
			total += nchar;
			index++;
			DEBUG_("Ret is %d, Write %d Bytes into file.\n", ret1, nchar);
		}else{
			DEBUG_("Ret is %d, Something happened.\n", ret1 + ret2);
			return FAIL;
		}
	}
	return total;
}

//Step2.
int doPageLevelDecompression(void *outbuf, void* inbuf,  uchar* Flags, ushort* Offsets, size_t size)
{
	return FAIL;
}



/*  MAX_DATABASE_LEN = When the database has this many entries, we clean the database

    Note that these are soft targets and can grow over these sizes if you have a ton of open files.
    Making these big will only slow us down, as we have to scan through the entire database on new files.
*/
#define MAX_DATABASE_LEN 30

void flush_file_cache(file_t* file)
{
	NEED_LOCK(&file->lock);
	if (file->cache)
	{
		int i;
		for (i = 0; i < file->cache_size; i++)
		{
			if (file->cache[i])
			{
				decomp_cache_size -= DC_PAGE_SIZE;
				free(file->cache[i]);
			}
		}
		DEBUG_("decomp_cache_size %d", decomp_cache_size);
		free(file->cache);
		file->cache = NULL;
		file->cache_size = 0;
	}
}

void direct_open_delete(file_t *file)
{
	NEED_LOCK(&file->lock);

	flush_file_cache(file);
	
	// It's out of the database, so we can unlock and destroy it
	UNLOCK(&file->lock);
	pthread_mutex_destroy(&file->lock);

	free(file);
}

/**
 * Truncate database
 *
 * @param force if TRUE, files with accesses != 0 are removed from database
 *              if FALSE, only files with accesses == 0 are removed from database
 */
void _direct_open_purge(int force)
{
	file_t *file;
	file_t *safe = NULL;

	NEED_LOCK(&database.lock);

	DEBUG_("cleaning db, entries %d", database.entries);

	list_for_each_entry_safe(file, safe, &database.head, list)
	{

		DEBUG_("('%s'), accesses: %d, deleted: %d, compressor: %p, size: %zi",
				file->filename, file->accesses, file->deleted,
				file->compressor, file->size);

		if (file->accesses == 0)
		{
		        LOCK(&file->lock);
		        /* check again after locking */
		        if (file->accesses == 0) {
                          // Check if file should be compressed
                          //
                          // file must not be deleted, must not have assigned a compressor,
                          // must be bigger than minimal size or have unknown size ( == 0) and
                          // compressor can be assigned with this file.
                          // Also, the backing FS must have at least enough space to store the
                          // file uncompressed (worst case).
                          struct statvfs stat;
                          if ((!file->deleted) &&
                              (!file->compressor) &&
                              ((file->size == (off_t) -1) || (file->size > min_filesize_background)) &&
                              statvfs(file->filename, &stat) == 0 &&
                              (stat.f_bsize * stat.f_bavail >= file->size || (geteuid() == 0 && stat.f_bsize * stat.f_bfree >= file->size)) &&
                              !read_only &&
                              choose_compressor(file))
                          {
                                  DEBUG_("compress file in background");
                                  background_compress(file);
                          }
                          else
                          {
                                  DEBUG_("trim from database");
                                  list_del(&file->list);
                                  database.entries--;

                                  // It's out of the database, so we can destroy it
                                  //
                                  direct_open_delete(file);
                                  continue;
                          }
                        }
			UNLOCK(&file->lock);
		}
		else
		{
			if (force)
			{
			        LOCK(&file->lock);
				DEBUG_("Forcing trim from database");
				DEBUG_("This could happend only in debug mode, "
				       "when PCFS was terminated with "
				       "some files still opened. Don't report "
				       "this as bug please.");
				list_del(&file->list);
				database.entries--;
	
				// It's out of the database, so we can destroy it
				//
				/* The background compression thread seems to still use this lock
				   sometimes, leading to massive data corruption. This is triggered
				   by using LZMA. The bright side is that this code here is only run
				   on unmount, so losing a few bytes by not freeing the file
				   descriptor won't hurt us. */
				//direct_open_delete(file);
				UNLOCK(&file->lock);
			}
		}			
	}
}

inline void direct_open_purge(void)
{
	_direct_open_purge(FALSE);
}

inline void direct_open_purge_force(void)
{
	_direct_open_purge(TRUE);
}

file_t* direct_new_file(unsigned int filename_hash, const char *filename, int len)
{
	file_t *file;

	DEBUG_("('%s'), hash: %d", filename, filename_hash);

	// Allocate memory for file_t and filename together.
	// This is a optimization - it's much better to allocate one bigger buffer
	// than two small buffers...
	//
	file = malloc(sizeof(file_t) + len);
	if (!file)
	{
		CRIT_("No memory!");
		exit(EXIT_FAILURE);
		// direct_new_file() is used by direct_open(), and there this
		// absolutely no error handling anywhere direct_open() is used,
		// so we have to leave this one.
	}

	file->accesses = 0;
	file->size = (off_t) -1;	// -1 means unknown file size
	file->deleted = FALSE;
	file->deduped = TRUE;
	file->compressor = NULL;
	file->type = 0;
	file->dontcompress = FALSE;
	file->skipped = 0;
	file->status = 0;

	file->cache = NULL;
	file->cache_size = 0;
	
	file->filename_hash = filename_hash;
	file->filename = (char *) file + sizeof(file_t);
	memcpy(file->filename, filename, len);

	pthread_mutex_init(&file->lock, &locktype);
	pthread_cond_init(&file->cond, NULL);
	INIT_LIST_HEAD(&file->head);

	return file;
}

// Returns locked database entry
file_t *direct_open(const char *filename, int stabile)
{
	int          len;
	int          hysteresis;
	unsigned int hash;
	file_t      *file;
	file_t      *safe = NULL;

	assert(filename);

	DEBUG_("('%s')", filename);

	hash = gethash(filename, &len);

	LOCK(&database.lock);

	list_for_each_entry_safe(file, safe, &database.head, list)
	{
	        /* file can only be free()d after it has been removed from the database,
	           which we have locked, so it's safe to access it for reading without
	           locking */
	        if (unlikely(file->filename_hash == hash)) {
                  LOCK(&file->lock);

//		  DEBUG_("%d: ('%s'), size: %lli, deleted: %d",
//			 i++, file->filename, file->size, file->deleted);

                  if (likely((memcmp(file->filename, filename, len) == 0)))
                  {
                          // Return file with lock. This file is requested and that
                          // means that file cannot be in deleted state.
                          //
                          file->deleted = FALSE;
                          UNLOCK(&database.lock);

                          if (stabile == TRUE)
                          {
                                  // TODO: Caller require stable enviroment. The question is:
                                  // does he need decompressed file or would he work with compressed fine?
                                  // And has compression been running for a long time? Is it before end or does it
                                  // just started now? What is better?
                                  //
                                  // Caller require stable enviroment - cancel compressing
                                  // if it is running now or block until decompression ends...
                                  //
                                  while (file->status & (COMPRESSING | DECOMPRESSING | DEDUPING))
                                  {
                                          file->status |= CANCEL;
                                          pthread_cond_wait(&file->cond, &file->lock);
                                  }
                          }
                          return file;
                  }
                  UNLOCK(&file->lock);
                }
	}

	hysteresis = comp_database.entries;

	if (database.entries++ > MAX_DATABASE_LEN + hysteresis)
	{
		direct_open_purge();
	}

	file = direct_new_file(hash, filename, len);

	// Return file with read-lock
	//
	LOCK(&file->lock);
	list_add_tail(&file->list, &database.head);

	UNLOCK(&database.lock);
	return file;
}

int direct_close(file_t *file, descriptor_t *descriptor)
{
	int ret = 0;
	struct stat stbuf;
	struct utimbuf utim;
	int stret;

	NEED_LOCK(&file->lock);

	DEBUG_("('%s')", file->filename);

	stret = fstat(descriptor->fd, &stbuf);
	
	if (descriptor->handle)
	{
		assert(file->compressor);

		ret = file->compressor->close(descriptor->handle);

		descriptor->offset = 0;
		descriptor->handle = NULL;
	}

	/* file->compressor->close() functions sometimes write data in the
	   compression buffer to disk when, from the user's point of view,
	   it should have already been written, thereby overwriting the
	   times the user may have already set manually. We need to restore
	   them. */
	if(file->type == WRITE && stret == 0) {
		utim.actime = stbuf.st_atime;
		utim.modtime = stbuf.st_mtime;
		utime(file->filename, &utim);
	}
	
	if (file->accesses == 1)
	{
		// There is only one user of this file and he is going to
		// release it => set some properties to default state.
		//
		file->type = 0;
		file->dontcompress = FALSE;
	}

	return ret;
}

static inline off_t bytes_to_skip(off_t now, off_t req)
{
	if (req >= now) return req-now;
	else return req;
}

int direct_decompress(file_t *file, descriptor_t *descriptor, void *buffer, size_t size, off_t offset)
{
	int len,ret;
	int cache_this_read = cache_decompressed_data;

	assert(file);
	assert(file->compressor);
	assert(descriptor);
	assert(descriptor->fd != -1);


	if(offset % DC_PAGE_SIZE != 0 || size % DC_PAGE_SIZE != 0)
	{
		//DEBUG_ON
		DEBUG_("turned off caching for odd read at %ld, size %zd", offset, size);
		cache_this_read = 0;
	}
	
	NEED_LOCK(&file->lock);

	DEBUG_("('%s'), offset: %zi, descriptor->offset: %zi",
				file->filename, offset, descriptor->offset);
	STAT_(STAT_DIRECT_READ);

	while (file->status & DECOMPRESSING)
	{
		file->status |= CANCEL;
		pthread_cond_wait(&file->cond, &file->lock);
	}

	if (file->type == 0)
	{
		file->type = READ;
	}

	size_t s = 0;
	if (cache_this_read && (file->type & READ) && file->cache && file->cache_size > offset / DC_PAGE_SIZE && file->cache[offset / DC_PAGE_SIZE])
	{
		//DEBUG_ON
		DEBUG_("serving data from cache at offset %zd", offset);
		while(size && file->cache[offset / DC_PAGE_SIZE])
		{
			memcpy(buffer, file->cache[offset / DC_PAGE_SIZE], DC_PAGE_SIZE);
			free(file->cache[offset / DC_PAGE_SIZE]);
			file->cache[offset / DC_PAGE_SIZE] = NULL;
			decomp_cache_size -= DC_PAGE_SIZE;
			size -= DC_PAGE_SIZE;
			buffer += DC_PAGE_SIZE;
			offset += DC_PAGE_SIZE;
			s += DC_PAGE_SIZE;
		}
		DEBUG_("decomp_cache_size %d", decomp_cache_size);
		if (!size) return s;
	}
	// Decompress file if:
	//  - user wants to read from somewhere else
	//  - user wants to read when he was writing to file before
	//
	// If offset is wrong, we need to close and open file in raw mode.
	//
	if ( 
	     !(file->type & READ) ||	/* no need to check for the r/o case here: if the FS is r/o, the file type cannot be WRITE */
	     (
		     (	/* caching disabled or no space free in cache */
			!cache_this_read || 
			decomp_cache_size >= max_decomp_cache_size ||
			bytes_to_skip(descriptor->offset, offset) > max_decomp_cache_size - decomp_cache_size
		     ) &&
		     !read_only && /* cannot decompress on r/o filesystem */
		     (	/* already did a lot of skipping, have a non-small file and are requested to read from a non-current position */
			file->skipped + bytes_to_skip(descriptor->offset, offset) > file->size &&
			file->size > 131072 && 
			offset != descriptor->offset
		     )
	     )
	   )
	{
		//DEBUG_ON
		DEBUG_("\tfallback, offset: %zi, descriptor->offset: %zi, size %zd, !(file->type & READ): %d, file->size %zd, file->skipped %zd",
			offset, descriptor->offset, size, (!(file->type & READ)), file->size, file->skipped);
		STAT_(STAT_FALLBACK);

		DEBUG_("calling do_decompress, descriptor->fd %d",descriptor->fd);
		if (!do_decompress(file))
		{
			DEBUG_("do_decompress failed, descriptor->fd %d",descriptor->fd);
			return FAIL;
		}

		file->size = -1;
		file->skipped = 0;

		int fd = descriptor->fd;
		UNLOCK(&file->lock);
		ret = pread(fd, buffer, size, offset);
		LOCK(&file->lock);
		if (ret < 0) return ret;
		else return s + ret;
	}

	if (offset < descriptor->offset)
	{
		//DEBUG_ON
		DEBUG_("resetting offset (current %zd, requested %zd)", descriptor->offset, offset);
		assert(descriptor->handle);
		ret = file->compressor->close(descriptor->handle);
		if(ret < 0)
		{
			FILEERR_(file, "unable to close compressor on file %s", file->filename);
			return FAIL;
		}
		descriptor->offset = 0;
		descriptor->handle = NULL;
		ret = lseek(descriptor->fd, sizeof(header_t), SEEK_SET);
		if(ret != sizeof(header_t))
		{
			FILEERR_(file, "unable to seek back to beginning of compressed data on file %s", file->filename);
			return FAIL;
		}
	}
	
	// Open file if neccesary
	//
	if (!descriptor->handle)
	{
		assert(descriptor->offset == 0);

		descriptor->handle = file->compressor->open(dup(descriptor->fd), "rb");
		if (!descriptor->handle)
		{
			ERR_("failed to open compressor on fd %d file ('%s')",
				descriptor->fd, descriptor->file->filename);

			return FAIL;
		}
	}

	if(offset > descriptor->offset)
	{
		size_t toread = offset - descriptor->offset;
		void* skipbuf = buffer;
		
		if (offset - descriptor->offset > max_decomp_cache_size - decomp_cache_size) {
			DEBUG_("not caching skip of size %zd, bigger than free cache (%d)", offset - descriptor->offset, max_decomp_cache_size - decomp_cache_size);
			cache_this_read = 0; /* no chance to cache this anyway */
		}
			
		if (cache_this_read && !file->cache)
		{
			file->cache_size = file->size / DC_PAGE_SIZE + 1;
			file->cache = calloc(file->cache_size, sizeof(void*));
			if (!file->cache)
			{
				ERR_("out of memory");
				return -1;
			}
		}
		//DEBUG_ON
		DEBUG_("skipping %zd to %zd before reading %zd bytes",toread,offset,size);
		while(toread) {
			size_t readsize = toread > DC_PAGE_SIZE ? DC_PAGE_SIZE : toread;
			
			if (cache_this_read && !file->cache[descriptor->offset / DC_PAGE_SIZE] && decomp_cache_size < max_decomp_cache_size)
			{
				decomp_cache_size += DC_PAGE_SIZE;
				skipbuf = malloc(DC_PAGE_SIZE);
				if (!skipbuf)
				{
					ERR_("out of memory");
					return -1;
				}
			}
			else
				skipbuf = buffer;
			
			len = file->compressor->read(descriptor->handle, skipbuf, readsize);
			DEBUG_("tried %zd bytes, got %d (file size %zd)",readsize,len,file->size);
			if(len < 0) {
				FILEERR_(file, "failed to read from compressor");
				errno = EIO;
				return -1;
			}
			if(len < readsize && (file->size == (off_t)-1 || descriptor->offset + len < file->size)) {
				/* Waitaminit, this should have worked! */
				/* (Note on the file->size -1 thing: I am
				   assuming that if file->size == -1,
				   something bad (probably another failed
				   read) happened before.) */
				FILEERR_(file, "short read while skipping in compressed file %s (probably corrupt)",file->filename);
				errno = EIO;
				return -1;
			}
			if(len == 0) return len; /* sought beyond the end of the file */
			toread -= len;
			if (cache_this_read && skipbuf != buffer)
				file->cache[descriptor->offset / DC_PAGE_SIZE] = skipbuf;
			else
				file->skipped += len;
			descriptor->offset += len;
			DEBUG_("toread %zd offset %zd",toread,descriptor->offset);
		}
		DEBUG_("decomp_cache_size %d, total skipped %zd", decomp_cache_size, file->skipped);
	}

	// Do actual decompressing
	//
	if (file->size != -1 && descriptor->offset + size > file->size)
		size = file->size - descriptor->offset;
	
	len = file->compressor->read(descriptor->handle, buffer, size);
	if (len < 0)
	{
		FILEERR_(file, "failed to read from compressor (file %s)", file->filename);
		errno = EIO;
		return -1;
	}
	if (len < size) {
		/* Again, this should have worked. */
		FILEERR_(file, "short read in compressed file %s (probably corrupt)",file->filename);
		errno = EIO;
		return -1;
	}
	descriptor->offset += len;

	DEBUG_("read requested len: %zd, got: %d", size, len);
	//DEBUG_OFF
	return len + s;
}

/*
  We need LOCK_ENTRY here.
  This function returns TRUE on data read ok (length in returned_len), FALSE on use pwrite instead and FAIL on error.
*/
int direct_compress(file_t *file, descriptor_t *descriptor, const void *buffer, size_t size, off_t offset)
{
	int len;
	int ret;

	assert(file);
	assert(file->compressor);
	assert(descriptor);
	assert(descriptor->fd != -1);
	assert(!read_only);
	
	NEED_LOCK(&file->lock);

	flush_file_cache(file); /* This may be superfluous. It did fix issue #32, but that may have been only
	                           due to the fact that it destroyed the cache falsely inherited by file_to
	                           in direct_rename(), which should have already been destroyed there. */
	
	DEBUG_("('%s'), offset: %zi, descriptor->offset: %zi",
				file->filename, offset, descriptor->offset);
	STAT_(STAT_DIRECT_WRITE);

	while (file->status & DECOMPRESSING)
	{
		file->status |= CANCEL;
		pthread_cond_wait(&file->cond, &file->lock);
	}

	if (file->type == 0)
	{
		file->type = WRITE;
	}

	// Decompress file if:
	//  - user wants to write somewhere else than to the end of file
	//  - user wants to write to the end of the file after he read from file
	//  - there is more than 1 user of the file, we cannot support more
	//    simulteanous writes at a time
	//
	if ((descriptor->offset != file->size) || (descriptor->offset != offset) || (!(file->type & WRITE)) || (file->accesses > 1))
	{
		DEBUG_("\tfallback for %s, offset: %zi, descriptor->offset: %zi, !(file->type & WRITE): %d, file->accesses: %d",
			file->filename, offset, descriptor->offset, (!(file->type & WRITE)), file->accesses);
		STAT_(STAT_FALLBACK);

		DEBUG_("calling do_decompress, descriptor->fd %d",descriptor->fd);
		if (!do_decompress(file))
		{
			DEBUG_("do_decompress failed, descriptor->fd %d",descriptor->fd);
			return FAIL;
		}

		file->size = -1;
		int fd = descriptor->fd;
		UNLOCK(&file->lock);
		int ret = pwrite(fd, buffer, size, offset);
		LOCK(&file->lock);
		return ret;
	}
	
	//***********Do 4KB compression here.**********************
	//*********Stored compressed data into Buffer**************
	//*********Write into file in the future*******************
	
	//*********************************************************

	// Open file if neccesary
	//XZ: the first time, this file is not opened, handle is NULL. so the following would be executed.
	if (!descriptor->handle)
	{
		assert(file->size == 0);
		assert(descriptor->offset == 0);
		
		//XZ: why here to write header?
		DEBUG_("XZ: writing header...");

                /* previous header reads might have advanced the fd to 12 */
		if (lseek(descriptor->fd, 0, SEEK_SET) < 0) {
			CRIT_("\t...seek failed!");
			return FAIL;
		}

		ret = file_write_header(descriptor->fd,
					file->compressor,
					file->size);
		if (ret == FAIL)
		{
			CRIT_("\t...failed!");
			//exit(EXIT_FAILURE);
			return FAIL;
		}
		//XZ: Format of file handle:
		/* the return type of gzdopen().
		struct gzFile_s {
		 unsigned have;
		 unsigned char *next;
		 z_off64_t pos;
		 };
		*/		
		descriptor->handle = file->compressor->open(dup(descriptor->fd), compresslevel);
		//DEBUG_("XZ: After open, handle offset is %ld\n", descriptor->handle->pos);
		if (!descriptor->handle)
		{
			ERR_("failed to open compressor on fd %d file ('%s')",
				descriptor->fd, descriptor->file->filename);

			return FAIL;
		}
	}

	assert(descriptor->handle);
	
	
	// Do actual compressing
	// XZ: Note the 'handle', what is used for? assigned in above, gzdopen return a handle.
	//gdb to see the content of handle
	len = file->compressor->write(descriptor->handle, (void *)buffer, size);
	
	DEBUG_("write requested: %zd, really written: %d", size, len);

	if (len == FAIL)
	{
		return FAIL;
	}
	descriptor->offset += len;

	// When writing we're always at EOF
	//
	file->size = descriptor->offset;
	
	// Write file length to file.  We can skip this here and do it on close instead?
	// (could mean sync problems because release isn't sync'ed with close)
	//
	DEBUG_("setting filesize to %zd", file->size);

	if (lseek(descriptor->fd, 0, SEEK_SET) == FAIL)
	{
		CRIT_("lseek failed!");
		//exit(EXIT_FAILURE);
		return FAIL;
	}
	// Todo, we can speed this up by only writing the size, and not flushing
	// it so often.
	//
	ret = file_write_header(descriptor->fd,
				file->compressor,
				file->size);
	if (ret == FAIL)
	{
		CRIT_("file_write_header failed!");
		//exit(EXIT_FAILURE);
		return FAIL;
	}
	if (lseek(descriptor->fd, 0, SEEK_END) == FAIL)
	{
		CRIT_("lseek failed");
		//exit(EXIT_FAILURE);
		return FAIL;
	}

	return len;
}

// Mark when file is deleted
//
inline void direct_delete(file_t *file)
{
	NEED_LOCK(&file->lock);

	file->deleted = TRUE;
	file->size = (off_t) -1;
}

// Move information from file_from to file_to when file_from is
// renamed to file_to.
//
// file_to is always a new file_t. Fuse library creates a temporary
// file (.fusehidden...) when file_to is already opened
//
file_t* direct_rename(file_t *file_from, file_t *file_to)
{
	descriptor_t *safe;
	descriptor_t *descriptor;

	NEED_LOCK(&file_from->lock);
	NEED_LOCK(&file_to->lock);

	DEBUG_("('%s' -> '%s')", file_from->filename, file_to->filename);
	DEBUG_("\tfile_from->compressor: %p, file_from->size: %zi, file_from->accesses: %d",
		file_from->compressor, file_from->size, file_from->accesses);

	/* file_to may be an existing file that is overwritten, so we need to
	   destroy its cache */
	flush_file_cache(file_to);
	
	file_to->size = file_from->size;
	file_to->compressor = file_from->compressor;

	file_to->dontcompress = file_from->dontcompress;
	file_to->type = file_from->type;
	file_to->status = file_from->status;

	// Move descriptors from file_from to file_to.
	//
	list_for_each_entry_safe(descriptor, safe, &file_from->head, list)
	{

		list_del(&descriptor->list);
		file_from->accesses--;

		list_add(&descriptor->list, &file_to->head);
		file_to->accesses++;

		descriptor->file = file_to;
	}

	/* same for background compression queue */
	LOCK(&comp_database.lock);
	compress_t* cp;
	list_for_each_entry(cp, &comp_database.head, list)
	{
		if(cp->file == file_from) {
			DEBUG_("remove %s from background queue",file_from->filename);
			cp->file = file_to;
			file_from->accesses--;
			file_to->accesses++;
		}
	}
	UNLOCK(&comp_database.lock);

	/* Normally, file_from->accesses is 0 here. In some cases it is
	   still 1 because thread_compress() has already removed the file
	   from the database with the intention of compressing it. It will
	   not do so, however, because we mark the file as deleted before
	   releasing the lock on it, so this case is OK, too. */
	assert(file_from->accesses <= 1);

	// The file refered by file_from is now deleted
	direct_delete(file_from);

	return file_to;
}

