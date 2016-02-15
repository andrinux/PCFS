/************************* Write ***********************************************/
/*  This file is  the basic function of PCFS: */
/*  4kB compression and 4kB Packing                            */
/*	Using deflate() with z_stream structure. */
/************************************************************************/

#include "zlib.h"
#include <stdio.h>


#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h> // open function
#include "compress_RW.h"

int totalW = 0;
unsigned char * wrBuf;
unsigned char * mark;
int blk = 0;


int PCFS_compress(int fd, unsigned char* buf, size_t size, uLong offset)
{
	wrBuf = (unsigned char *) calloc (size, sizeof(unsigned char));
	mark=(unsigned char *) calloc(PAGE_SIZE, sizeof(unsigned char));
	
	return testCompress(fd, buf, size);
}


//======================Compress buf to target=============
// Given: buf and size
// Write the compressed segments to pOut.
// fd is the file descriptor. 
int testCompress(int pOut, unsigned char* buf, size_t size)
{
	int ret = 0;
	z_stream c_stream; /* compression stream */
	int err;
	uLong len = size;
	uLong lastTotal=0;
	int blkCnt = (size%PAGE_SIZE == 0) ? (size/PAGE_SIZE-1):(size/PAGE_SIZE);

	int curSize = 0, lastSize = 0;
	unsigned char lastBuf[OUT_BUF_SIZE];
	unsigned char outBuf[OUT_BUF_SIZE];

	int total_In = 0, Avail_In = 0;
	// unsigned char * curPos_In = NULL; //current input segment pos.
	
	while(total_In < len){

		// Initializing Segments
		c_stream.zalloc = Z_NULL;
		c_stream.zfree = Z_NULL;
		c_stream.opaque = (voidpf)0;
		c_stream.avail_in = 0;
		c_stream.next_in = Z_NULL;

		err = deflateInit(&c_stream, Z_DEFAULT_COMPRESSION);
		CHECK_ERR(err, "deflateInit");

		c_stream.next_in  = (z_const unsigned char *)buf + total_In; 
		
		memset(outBuf, 0, OUT_BUF_SIZE);
		c_stream.next_out = outBuf;
		c_stream.avail_in = PAGE_SIZE;
		c_stream.avail_out = PAGE_SIZE;

		// call deflate, Z_FULL_FLUSH is to reset the dict each time.
		err = deflate(&c_stream, Z_FULL_FLUSH);
		CHECK_ERR(err, "deflate");
		curSize = c_stream.total_out;
		total_In += c_stream.total_in;
		if(DEBUG)	printf("[C] Compressed size is: %d. \n", curSize);

		//try_decomp_W(outBuf);

		//Flush to file.
		check_and_flush(&lastSize, &curSize, lastBuf, outBuf);
		//need to reset lastTotal to calculate the compressed seg size each time
		lastTotal = c_stream.total_out;
		//check the potential errors:
		err = deflateEnd(&c_stream);
		CHECK_ERR(err, "deflateEnd");
	}
	//Still need to check the last un-flushed one.
	if(lastSize || curSize){
		if(DEBUG) printf("[Warning!] the last piece left.\n");
		flush_page(lastBuf, lastSize, 0);
	}

	write_file(pOut, mark, wrBuf, blk);
	if(DEBUG) printf("Total effective bit written into file: %d, total compressed is: %ld\n", 
						 totalW, lastTotal);
	if(DEBUG) printf("Actual Write: %d ; Request Write: %ld", totalW, size);
	return size;
}


int try_decomp_W(unsigned char* inBuf){
	z_stream t_strm;
	int ret=0;
	int lastOut = 0;
	unsigned char * out = (unsigned char *) calloc
						  (2*PAGE_SIZE, sizeof(unsigned char));

	t_strm.zalloc = Z_NULL;
	t_strm.zfree = Z_NULL;
	t_strm.opaque = (voidpf)0;
	t_strm.avail_in = 0;
	t_strm.next_in = Z_NULL;

	ret = inflateInit(&t_strm);
	CHECK_ERR(ret, "inflateInit");

	//compressed data is already stored in 'inBuf'
	t_strm.avail_in = PAGE_SIZE;
	t_strm.next_in = (z_const unsigned char *) inBuf; // start point
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


int check_and_flush(int *lastSize, int *curSize,  unsigned char* lastBuf, 
					unsigned char* curBuf)
{
	int ret = 0;
	unsigned char outBuf[PAGE_SIZE];
	memset(outBuf, 0, PAGE_SIZE);// clean up
	//Four cases.
	if((*curSize) > PAGE_SIZE){
		flush_page(lastBuf, *lastSize, 0);
		flush_page(curBuf, *curSize, 0);
		memset(lastBuf,0, OUT_BUF_SIZE); 
		*lastSize=0;
		memset(curBuf,0, OUT_BUF_SIZE); 
		*curSize=0;
	}else if((*curSize) + (*lastSize) >PAGE_SIZE){
		flush_page(lastBuf, *lastSize, 0);
		memcpy(lastBuf, curBuf, OUT_BUF_SIZE);
		(*lastSize)=(*curSize);
		memset(curBuf, 0, OUT_BUF_SIZE);
		*curSize = 0;
	}else if((*lastSize) == 0){
		memcpy(lastBuf, curBuf, OUT_BUF_SIZE);
		*lastSize = *curSize;
		memset(curBuf, 0, OUT_BUF_SIZE);
		*curSize = 0;
	}else{
		//combine two pages. copy cur to last, will flush next time.
		memcpy(outBuf, lastBuf, *lastSize);
		memcpy(outBuf + *lastSize, curBuf, *curSize);
		flush_page(outBuf, *lastSize+*curSize, 1);
		memset(outBuf, 0, PAGE_SIZE);
		memset(lastBuf, 0, OUT_BUF_SIZE);
		*lastSize=0;
		memset(curBuf, 0, OUT_BUF_SIZE);
		*curSize=0;
	}
	return ret;
}
// type: 0 - only one page is packed; 1- two pages are packed into one sector.
int flush_page(unsigned char* Buf, int size,int type)
{
	int ret = 0;
	if (size > PAGE_SIZE){
		if(DEBUG) printf("flush data err: -1");
		return -1;
	}
	if(DEBUG) printf("[W] %d data written into file.\n", size);
	totalW += size;
	//fwrite (Buf , sizeof(char), PAGE_SIZE, pOut);
	memset(wrBuf+blk*PAGE_SIZE, 0, PAGE_SIZE); // <- clean up the output buffer.
	memcpy(wrBuf+blk*PAGE_SIZE , Buf, PAGE_SIZE); // <- update the start location each time!
	//set the mark(as metadata)
	if(type) 
		set_flag(mark, blk);
	blk++;
	return ret;
}

int write_file(int pOut, unsigned char *mark, unsigned char *wrBuf, int blk)
{
	int ret = 0;
	/*  Write in upper level C. */
	//fwrite (mark , sizeof(char), PAGE_SIZE, pOut);
	// TODO: has redundancy bits following here
	// fwrite (wrBuf , sizeof(char), blk*PAGE_SIZE, pOut);
	/*   */
	write(pOut, mark, PAGE_SIZE);
	write(pOut, wrBuf, blk*PAGE_SIZE);
	return ret;
}

int set_flag(unsigned char * mBuf, int blk)
{
	int ret = 0;
	int byte = blk / 8;
	int offset = blk % 8;
	char mask = 0x80 >> offset;
	*(mBuf+byte) = *(mBuf+byte) | mask;
	return ret;
}
