//gcc -Wall testZlib.c -o test -lz

#include <stdio.h>
#include <time.h>
#include <zlib.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h> // open function



#define LEN 4096

unsigned char DATA_BUFFER[LEN];
unsigned char BUFFER[LEN];

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
	read(fin, BUFFER, LEN);

	uLong dst_len = LEN;
	Bytef* dst=(Bytef*) DATA_BUFFER;
	compress(dst, &dst_len, (Bytef*)BUFFER, (uLong) LEN);
	write(fout, DATA_BUFFER, dst_len);
	close(fin);
	close(fout);
}

void testDecompress()
{

}

//===================Main Function of Test ================
int main()
{
	testCompress();
	testDecompress();
	return 0;
}