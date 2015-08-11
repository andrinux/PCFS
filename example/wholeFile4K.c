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
/*
int ZEXPORT gzwrite( gzFile file,  voidpc buf, unsigned len);
*/

#define PAGE_SIZE (4096)
#define FAIL (-1)
//#define DEBUG 1


struct compBlk{
	Bytef dst[PAGE_SIZE];
	uLongf dst_len;
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
	//Create count CompBlocks, do we need initialize here?
	struct compBlk* cBlk;
	Bytef* tmpBuf;
	cBlk = (struct compBlk*) malloc((1+count)*sizeof(struct compBlk));
	for(index = 0; index <= count; index++){
		//Initialize cBlk
		memset(cBlk[index].dst, 0, PAGE_SIZE);
		cBlk[index].dst_len =  PAGE_SIZE;
		//From (buf + index*PAGE_SIZE)
		struct compBlk* curBlk = cBlk+index;
		uLong blkSize = (index <= count)? PAGE_SIZE : size % PAGE_SIZE;

#ifdef DEBUG
		printf("DEBUG: No. %2d blkSize is %ld. \n", index, blkSize);
		int i;
		//memcpy(curBlk->dst, buf, PAGE_SIZE);
		for (i=0; i< PAGE_SIZE; i++)
			printf("%d",*(curBlk->dst + i));
		printf("\n");
#endif
		tmpBuf=(Bytef*) malloc(sizeof(Bytef) * 4096);
		int ret =compress(tmpBuf, &curBlk->dst_len, 
							(Bytef*) (buf + index*PAGE_SIZE), (uLong) blkSize);
		printf("Ret is %d.dst_len is %ld.\n", ret, curBlk->dst_len);
		if(Z_OK != ret ){
			printf("%d block Error.\n",index);
			return FAIL;
		}else{
			printf("%d Block: %ld becomes %ld bytes.\n",
					index, blkSize, curBlk->dst_len);
		}
	}
	free(tmpBuf);
	return 0;
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
    
    //Read Data into buf
    //Call write4K
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

//Question is when to stop. More than one page is squeezed into one physical page.
void testDecompress()
{
	const char * in = "cTrace.bin";
	const char * out ="newTrace.log";

	int fin=open(in, O_RDONLY);
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
	int nchar = 849;
	read(fin, BUFFER, nchar);
	uLong dst_len = PAGE_SIZE;
	Bytef* dst=(Bytef*) DATA_BUFFER;
	uncompress(dst, &dst_len, (Bytef* )BUFFER, (uLong)nchar);
	write(fout, DATA_BUFFER, dst_len);
	close(fin);
	close(fout);

}

//===================Main Function of Test ================
int main()
{
	const char * srcPath = "/home/xuebinzhang/Trace.log";
	const char * dstPath = "/home/xuebinzhang/zTrace.log";
	testCompress(srcPath);
	//testDecompress(dstPath);
	return 0;
}
