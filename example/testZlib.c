//gcc -Wall testZlib.c -o test -lz
//Code to use 4kB compression.

#include <stdio.h>
#include <time.h>
#include <zlib.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h> // open function
#include <string.h>


#define LEN 4096
#define PAGE_SIZE (4096)


unsigned char DATA_BUFFER[LEN];
unsigned char BUFFER[LEN];

struct compBlk{
	Bytef dst[PAGE_SIZE];
	uLong dst_len;
};

void testCompress()
{
	const char * file = "Trace.log";
	const char * out = "cTrace.bin";

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
    
	//File openning finished
	lseek(fin, 0, SEEK_SET);
	int nchar = 0;
	//DATA_BUFFER is used to store the compressed data.
	//BUFFER is used to store the 4KB data read from original file.
	int i = 0;
    while((nchar = read(fin, BUFFER, LEN))>0){
		uLong dst_len = 0;
		Bytef* dst=(Bytef*) DATA_BUFFER;
		int ret =compress(dst, &dst_len, (Bytef*)BUFFER, (uLong) LEN);
		printf("Ret is %d\n", ret);
		if (Z_OK == ret){
			write(fout, DATA_BUFFER, dst_len);
			printf("%ld-, ", dst_len);
            i++;
            if(i % 10 == 0) 
                printf("\n");
			memset(DATA_BUFFER, 0, sizeof(DATA_BUFFER));
			memset(BUFFER, 0, sizeof(BUFFER));
		}
		else
			puts("Error occured in compressing.");
	}	
	
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
	uLong dst_len = LEN;
	Bytef* dst=(Bytef*) DATA_BUFFER;
	uncompress(dst, &dst_len, (Bytef* )BUFFER, (uLong)nchar);
	write(fout, DATA_BUFFER, dst_len);
	close(fin);
	close(fout);

}

void getVal(struct compBlk * Blks, int count){
	Blks =  (struct compBlk*) malloc (count * sizeof(struct compBlk));
	int index = 0;
	for(index =0; index < count; index++){
		(Blks + index)->dst_len = index *10;
	}
	printf("Test Memory: %ld\n",(Blks+4)->dst_len);
}

//===================Main Function of Test ================
int main()
{
	//struct compBlk *Blks;
	//getVal(Blks, 10);
	printf("%ld\n", sizeof(short));
	testCompress();
	testDecompress();
	//printf("Test Memory: %ld\n",(Blks+4)->dst_len);
	return 0;
}
