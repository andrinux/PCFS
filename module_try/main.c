#include "zlib.h"
#include <stdio.h>


#include <string.h>
#include <stdlib.h>

#include "compress_RW.h"

#define TEST_SIZE 40960

int main()
{
	int pIn = open("./Traces.log", O_RDONLY);
	if(pIn == 0)	
		printf("ERR: Open input file. \n");
	mode_t mode = 0666;

	int pOut = open("./cTrace.bin", O_CREAT|O_WRONLY, mode);
	if(pOut == 0)	
		printf("ERR: Open output file. \n");

	unsigned char * buf = (unsigned char *) calloc (TEST_SIZE, sizeof(unsigned char));
	if(TEST_SIZE != read(pIn, buf, TEST_SIZE))
		printf("ERR: input file read\n");

	PCFS_compress(pOut, buf, TEST_SIZE, 0);

	close(pIn);
	close(pOut);


	return 0;
}