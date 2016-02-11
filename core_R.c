/************************* Write ***********************************************/
/*  This file is used to prototype the basic function of PCFS: */
/*  4kB compression and 4kB Packing                            */
/*	Using deflate() with z_stream structure. */
/************************************************************************/


#include "zlib.h"
#include <stdio.h>

#include <string.h>
#include <stdlib.h>


#define CHECK_ERR(err, msg) { \
	if(DEBUG) {\
	if (err != Z_OK) { \
	fprintf(stderr, "%s error: %d\n", msg, err); \
	/*exit(1);*/ \
	} \
	}\
}

#define DEBUG 1

#define PAGE_SIZE (4096)
#define PACK_SIZE (4096)
#define OUT_BUF_SIZE (2*PAGE_SIZE)
#define FLUSH_CHUNK (4*PAGE_SIZE)

int get_type(unsigned char * mark, int blk);
int testDecompress(FILE *pIn, FILE *pOut, unsigned char *rdBuf, uLong reqSize );
int decompress_page(z_stream* d_stream, int type, unsigned char *inBuf, 
	unsigned char * rdBuf, int* blk, int* pages);
int is_valid(unsigned char * addr);
int try_decomp(unsigned char* inBuf);

int try_decomp(unsigned char* inBuf){
	z_stream t_strm;
	int ret=0;
	int lastOut = 0;
	unsigned char * out =(unsigned char *) calloc(2*PAGE_SIZE, sizeof(unsigned char));

	t_strm.zalloc = Z_NULL;
	t_strm.zfree = Z_NULL;
	t_strm.opaque = (voidpf)0;
	t_strm.avail_in = 0;
	t_strm.next_in = Z_NULL;

	ret = inflateInit(&t_strm);
	CHECK_ERR(ret, "inflateInit");

	//compressed data is already stored in 'inBuf'
	t_strm.avail_in = PAGE_SIZE;
	t_strm.next_in = (z_const unsigned char *) inBuf+PAGE_SIZE; // start point
	t_strm.next_out = out;
	t_strm.avail_out = 2*PAGE_SIZE;


	while(t_strm.total_out < 2*PAGE_SIZE ){
		 ret = inflate(&t_strm, Z_NO_FLUSH);
		 if(t_strm.total_out == lastOut)
			 break;
		 lastOut=t_strm.total_out;
	}
	ret = inflateEnd(&t_strm);
	CHECK_ERR(ret, "inflateEnd");

	return ret;
}
// read out the whole input file and decompress:
// Note the output space should be large enough.
int testDecompress(FILE *pIn, FILE *pOut, unsigned char *rdBuf, uLong reqSize )
{
	int ret = 0;
	uLong tLen = 0, space =0;
	unsigned char * inBuf = NULL;
	unsigned char * mark = NULL;
	int blk = 0; // index of compressed sector.
	int pages = 0; //count of pages.
	int type = 0;//Sector type
	int lasttotal=0;

	unsigned char * curSec = NULL;

	z_stream d_stream; /* decompression stream */

	fseek ( pIn , 0 , SEEK_END );
	tLen = ftell(pIn);
	fseek ( pIn , 0 , SEEK_SET );

	//read mark ->> mark.
	mark = (unsigned char *) calloc(PAGE_SIZE, sizeof(unsigned char));
	if( PAGE_SIZE != fread(mark, 1, PAGE_SIZE, pIn))
		printf("ERR: cannot read marks in input file.\n");
	
	//read compressed sectors data.->>inBuf
	inBuf = (unsigned char *) calloc(tLen-PAGE_SIZE, sizeof(unsigned char));
	if((tLen-PAGE_SIZE) != fread(inBuf, 1, tLen-PAGE_SIZE, pIn))
		printf("ERR: cannot read data in input file.\n");

	/************************************************************************/
	try_decomp(inBuf);
	/************************************************************************/

	// pre-allocte a large enough space according to 'tLen'
	space = (1 + tLen/PAGE_SIZE)*PAGE_SIZE * 2;
	rdBuf = (unsigned char *) calloc(space, sizeof(unsigned char));
	
	//initialize inflate()
	d_stream.zalloc = Z_NULL;
	d_stream.zfree = Z_NULL;
	d_stream.opaque = (voidpf)0;
	d_stream.avail_in = 0;
	d_stream.next_in = Z_NULL;
	
	ret = inflateInit(&d_stream);
	CHECK_ERR(ret, "inflateInit");

	//compressed data is already stored in 'inBuf'
	d_stream.avail_in = tLen;
	d_stream.next_in = inBuf; // start point
	d_stream.next_out = rdBuf;
	d_stream.avail_out = space;
	
	//termination condition: still have unprocessed input.
	while(d_stream.total_out < reqSize ){
		//record current page start location.
		curSec = d_stream.next_in;
		type = get_type(mark, blk);
		decompress_page(&d_stream, type, inBuf, rdBuf, &blk, &pages);
		if(lasttotal ==  d_stream.total_out) // no further output: Quit.
			break;
		d_stream.next_in = curSec + PACK_SIZE;
		lasttotal = d_stream.total_out;
	}

	//All data are pushed into rdBuf
	fwrite(rdBuf, 1, d_stream.total_out, pOut);
	return ret;
}

int decompress_page(z_stream* d_stream, int type, unsigned char *inBuf, 
	unsigned char * rdBuf, int* blk, int* pages)
{
	int ret = 0;
	if(DEBUG) printf("blk: %d, Sector type: %d\n", *blk, type);
	if(type == 0){
		ret = inflate(d_stream, Z_SYNC_FLUSH);
		CHECK_ERR(ret, "inflate");
		(*pages) = (*pages) + 1;
	}else{
		ret = inflate(d_stream, Z_SYNC_FLUSH);
		CHECK_ERR(ret, "inflate");
		(*pages) = (*pages) + 2;
	}
	(*blk)++;
	return ret;
}

int get_type(unsigned char * mark, int blk)
{
	int byte = *(mark + blk /8);
	int offset =blk % 8;
	int ret = 0;
	ret = byte & (0x80 >> offset);
	if(ret == 0)
		return 0;
	else
		return 1;
}

int is_valid(unsigned char * addr)
{
	int i=0;
	int ret = 0;
	for (i = 0; i < 5; i++)
		if((*addr+i) != 0x00)
			ret = 1;
	return ret;
}

//===================Main Function of Test ================
int main()
{
	unsigned char * rdBuf;
	unsigned char * mark;

	FILE *pIn = fopen("./cTrace.bin", "rb");
	FILE *pOut = fopen("./Trace_d.log", "wb");

	uLong reqSize = PAGE_SIZE *10.5;
	uLong tLen = 0;

	if(pIn == NULL) printf("ERR: Cannot open input file.\n");
	if(pOut == NULL) printf("ERR: Cannot open output file.\n");
	fseek ( pIn , 0 , SEEK_END );
	tLen = ftell(pIn);
	fseek ( pIn , 0 , SEEK_SET );

	//this is to simulate the cmd of 'read': given a FILE ptr
	// And the data will be stored into rdBuf
	rdBuf = (unsigned char*) calloc(reqSize, sizeof(unsigned char));
	mark=(unsigned char *) calloc(PAGE_SIZE, sizeof(unsigned char));
	//File read will be done in testDec().
	testDecompress(pIn, pOut,rdBuf, reqSize );

	fclose(pIn); fclose(pOut);
	return 0;
}
