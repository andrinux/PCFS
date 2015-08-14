 
#ifndef DIRECT_COMPRESS_H
#define DIRECT_COMPRESS_H

#include <errno.h>

#include "structs.h"
#include "compress.h"

int readCompInfo(file_t *file, descriptor_t *descriptor, uchar* cFlags, ushort* cOffsets);

size_t PageLevelCompression(file_t *file, descriptor_t *descriptor, const void *buf, 
					   size_t size, off_t offset);

int doPageLevelCompression(Blk_t* tBUF, Blk_t* fBUF, uchar* Flags, ushort* Offsets, 
						const void *buf, size_t size);

int PageLevelDecompression(file_t *file, descriptor_t *descriptor, void *buf, size_t size, 
					off_t offsetInFile);


int direct_close(file_t *file, descriptor_t *descriptor);
file_t *direct_open(const char *filename, int stabile);
void direct_open_purge(void);
void direct_open_purge_force(void);
int direct_decompress(file_t *file, descriptor_t *descriptor,void *buffer, size_t size, off_t offset);
int direct_compress(file_t *file, descriptor_t *descriptor, const void *buffer, size_t size, off_t offset);
void direct_delete(file_t *file);
file_t *direct_rename(file_t *file_from, file_t *file_to);

void flush_file_cache(file_t* file);

#endif
