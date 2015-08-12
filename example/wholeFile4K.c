//gcc -Wall testZlib.c -o test -lz
//Code to use 4kB compression.
//Author: X.Z.

#include <stdio.h>
#include <time.h>
#include <zlib.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h> // open function
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>

	
/*
int ZEXPORT gzwrite( gzFile file,  voidpc buf, unsigned len);
*/

#define PAGE_SIZE (4096)
#define FAIL (-1)
//#define DEBUG 1
#define SEG 10

struct compBlk{
	Bytef dst[PAGE_SIZE];
	uLongf dst_len;
	Bytef flag; //If uncompressable, set flag as 0, otherwise 1.
};

unsigned char DATA_BUFFER[PAGE_SIZE];
unsigned char BUFFER[PAGE_SIZE];
unsigned char BUFFER[PAGE_SIZE];

//This is the function to get the file size
uLong getFileSize(const char* path)
{
	struct stat *stbuf = (struct stat*) malloc (sizeof(struct stat));
	int res = lstat(path, stbuf);
	int size = stbuf->st_size;
	free(stbuf);
	if (res == -1)
		return -1;
	else
		return size;
}

//The data is already in buf, size is known, and file descriptor is assigned
//Just compress them one by one.
//**********************//
//Deal with sequential write first, and random W/R thereafter.
//To do random Write, we need to designate the ppos.
int write4K(int fd, Bytef* buf, uLong size)
{
	int count =  size / PAGE_SIZE; //index: 0->count
	int index = 0;
	int nchar = 0;
	//Create count CompBlocks, do we need initialize here?
	struct compBlk* cBlk;
	Bytef* tmpBuf;
	cBlk = (struct compBlk*) malloc((1+count)*sizeof(struct compBlk));
	for(index = 0; index <= count; index++){
		//Initialize cBlk
		memset(cBlk[index].dst, 0, PAGE_SIZE);
		cBlk[index].dst_len =  2*PAGE_SIZE;
		//From (buf + index*PAGE_SIZE)
		struct compBlk* curBlk = cBlk+index;
		uLong blkSize = (index < count)? PAGE_SIZE : size % PAGE_SIZE;

		tmpBuf=(Bytef*) malloc(sizeof(Bytef) * 2*4096);
		int ret = compress(curBlk->dst, &curBlk->dst_len, 
							(Bytef*) (buf + index*PAGE_SIZE), (uLong) blkSize);
		if(curBlk->dst_len <= PAGE_SIZE) 
			curBlk->flag = 1;
		else
			curBlk->flag = 0;
		printf("Ret is %d. dst_len is %ld.", ret, curBlk->dst_len);
		if(Z_OK != ret ){
			printf("%d block Error.\n",index);
			return FAIL;
		}else{
			printf("%2d Block: %ld becomes %ld bytes.\n",
					index, blkSize, curBlk->dst_len);
		}
	}
	//The compressed data are stored in cBlk[0:count] now.
	//Reorgranise and write to file.
	int cur = 0;
	while(cur < count){
		memset(DATA_BUFFER, 0, PAGE_SIZE);
		//assert(cur < count);
		if(cBlk[cur].dst_len + cBlk[cur+1].dst_len <= PAGE_SIZE){
			memcpy(DATA_BUFFER, cBlk[cur].dst, cBlk[cur].dst_len);
			memcpy(DATA_BUFFER + cBlk[cur].dst_len, cBlk[cur+1].dst, cBlk[cur+1].dst_len);
			//padding.
			memset(DATA_BUFFER + cBlk[cur].dst_len + cBlk[cur+1].dst_len, 0, (PAGE_SIZE - cBlk[cur].dst_len - cBlk[cur+1].dst_len));
			cur = cur + 2;
		}else{
			//uncompressed
			memcpy(DATA_BUFFER, buf+cur*PAGE_SIZE, PAGE_SIZE);
			cur++;
		}		
		nchar = write(fd, DATA_BUFFER, PAGE_SIZE);
		printf("Write %d Bytes.Now cur = %d.\n", nchar, cur);
	}
	if(cur == count){
		memcpy(DATA_BUFFER, buf+cur*PAGE_SIZE, PAGE_SIZE);
		nchar = write(fd, DATA_BUFFER, PAGE_SIZE);
		printf("Write %d Bytes. Now cur = %d.\n", nchar, cur);
		}
	if(cur > count){
		printf("cur>count now. Do nothing.\n");
	}
	free(tmpBuf);
	return nchar;
}



//Core compression implementation
void testCompress(const char* path)
{
	const char * file = path;
	const char * out = "/home/xuebinzhang/zTrace.log";
	printf("DEBUG: Size of Input file %s: %ld\n", out, getFileSize(file));

	int fin = open(file, O_RDONLY);
	if(fin){
		puts("Open OK.");
	}else{
		puts("Cannot Open input file");
	}
	mode_t mode = 0666;
	int fout = open(out, O_CREAT|O_WRONLY, mode);
	if(fout)
		puts("Output file OK.");
	else
		puts("Cannot create Output file.");
    
    //Read Data into buf And Call write4K
    //File openning finished
	lseek(fin, 0, SEEK_SET);
	int nchar = 0;
	Bytef *buf;
	uLong fsize = getFileSize(file);
	buf = (Bytef *) malloc(fsize * sizeof(Bytef));
	nchar = read(fin, buf, fsize);
	if(nchar != fsize){
		printf("Error occured in Reading to buf...\n");
		return;
	}
	//Key calling...
	nchar = write4K(fout, buf, fsize);
	
	if(nchar == FAIL){
		printf("Error occured in compression.\n");
		return;
	}
	else{
		printf("%d written into disk, %ld request .\n", nchar, fsize);
		return;
	}

	free(buf);
    close(fin);
	close(fout);
}

//Read compressed data chunks and do decompression.
int read4K(int fd, Bytef* buf, uLong size)
{
	int count = size / PAGE_SIZE;
	int index = 0;
	Bytef decBuf[PAGE_SIZE * 2] = { 0 };
	uLong decLen1 = PAGE_SIZE;
	uLong decLen2 = PAGE_SIZE;
	//Store the compression information: offset and flag.
	uLong offset[SEG]={873, 657, 727, 749, 910, 821};
	int FLAG[SEG] = {1, 1, 1, 1, 1, 1};
	int ret1 = -99, ret2 = -99;
	int nchar = 0;
	int total = 0;
	while(index < count){
		memset(decBuf, 0, PAGE_SIZE * 2);
		//Need to find a way to continue decompressing for two sectors.
		uLong inLen = PAGE_SIZE;
		if(FLAG[index] == 1){
			ret1 = uncompress(decBuf, &decLen1, buf+PAGE_SIZE*index, inLen);
			ret2 = uncompress(decBuf+PAGE_SIZE, &decLen2, 
									buf+PAGE_SIZE*index+offset[index], inLen-offset[index]);
			printf("Index = %d: dec1Len=%ld. dec2Len=%ld.\n", index, decLen1, decLen2);
		}else{
			//no compression
			ret1 = Z_OK; ret2 = Z_OK;
			decLen1 = PAGE_SIZE; decLen2 = 0;
			memcpy(decBuf, buf+PAGE_SIZE*index, PAGE_SIZE);
		}

		if(ret1 == Z_OK && ret2 == Z_OK){
			nchar = write(fd, decBuf, decLen1 + decLen2);
			total += nchar;
			index++;
			printf("Ret is %d, Write %d Bytes into file.\n", ret1, nchar);
		}else{
			printf("Ret is %d, Something happened.\n", ret1 + ret2);
			return FAIL;
		}
	}
	return total;
}

//Question is when to stop. More than one page is squeezed into one physical page.
void testDecompress(const char* path)
{
	const char * in = path;
	const char * out ="/home/xuebinzhang/newTrace.log";

	int fin = open(in, O_RDONLY);
	if(fin)
		puts("Open compressed data source file: OK.");
	else
		puts("Error to open compressed source file.");

	mode_t mode = 0666;
	int fout = open(out, O_WRONLY|O_CREAT, mode);
	if(fout)
		puts("Decompressed Output file: OK.");
	else
		puts("Error: Cannot create Output file.");

	//Finish openning file.
	lseek(fin, 0, SEEK_SET);
	int nchar = 0;
	Bytef *buf;
	uLong fsize = getFileSize(in);
	buf = (Bytef *) malloc(fsize * sizeof(Bytef));
	nchar = read(fin, buf, fsize);
	if(nchar != fsize){
		printf("Error occured in Reading to buf...\n");
		return;
	}
	//Call read4K, deompress one by one. How to store the flag information?
	nchar = read4K(fout, buf, fsize);
	
	if(nchar == FAIL){
		printf("Error occured in decompression.\n");
		return;
	}
	else{
		printf("%ld read out from file, %d put into memory .\n", fsize, nchar);
		return;
	}

	close(fin);
	close(fout);

}

//===================Main Function of Test ================
int main()
{
	const char * srcPath = "/home/xuebinzhang/Trace.log";
	const char * dstPath = "/home/xuebinzhang/zTrace.log";
	testCompress(srcPath);
	testDecompress(dstPath);
	return 0;
}
