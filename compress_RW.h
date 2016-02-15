#include "zlib.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h> // open function
#include <string.h>

#define CHECK_ERR(err, msg) { \
	if(DEBUG) {\
		if (err != Z_OK) { \
		fprintf(stderr, "%s error: %d\n", msg, err); \
	} \
}\
}

#ifndef DEBUG
#define DEBUG 1
#endif

#ifndef PAGE_SIZE
#define PAGE_SIZE (4096)
#endif

#ifndef PACK_SIZE
#define PACK_SIZE (4096)
#endif

#ifndef OUT_BUF_SIZE
#define OUT_BUF_SIZE (2*PAGE_SIZE)
#endif

#ifndef OFFSET
#define OFFSET 5
#endif

/*
* WRITE PART
*/




int PCFS_compress(int fd, const char* buf, size_t size, uLong offset);
int testCompress( int pOut, unsigned char* buf, size_t size);
int check_and_flush(int *lastSize, int *curSize, unsigned char* lastBuf, unsigned char* curBuf);
int flush_page(unsigned char* Buf, int size,  int type);
int set_flag(unsigned char * wrBuf, int blk);
int write_file(int pOut, unsigned char *mark, unsigned char *wrBuf, int blk);

int try_decomp_W(unsigned char* inBuf);



// READ PART
int get_type(unsigned char * mark, int blk);
int testDecompress(FILE *pIn, FILE *pOut, unsigned char *rdBuf, uLong reqSize );
int decompress_page(z_stream* d_stream, int type, unsigned char *inBuf, 
	unsigned char * rdBuf, int* blk, int* pages);
int is_valid(unsigned char * addr);
int try_decomp(unsigned char* inBuf);
int decompress_page_1(unsigned char *start, unsigned char *rdBuf, int *blk, int *pages);
int decompress_page_2(unsigned char *start, unsigned char *rdBuf, int *blk, int *pages);







