#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>

int main(int argc, char* argv[])
{
	char text[] = "zlib compress and uncompress test. From Andrinux@Github.\n";
	uLong tlen = strlen(text) + 1;
	char* buf = NULL;
	uLong blen;

	blen = compressBound(tlen);	
	if((buf = (char*)malloc(sizeof(char) * blen)) == NULL)
	{
		printf("no enough memory!\n");
		return -1;
	}

	if(compress(buf, &blen, text, tlen) != Z_OK)
	{
		printf("compress failed!\n");
		return -1;
	}else{
        printf("original length is: %d, after compress is: %d\n", tlen, blen);
    }

	if(uncompress(text, &tlen, buf, blen) != Z_OK)
	{
		printf("uncompress failed!\n");
		return -1;
	}

	printf("%s", text);
	if(buf != NULL)
	{
		free(buf);
		buf = NULL;
	}

	return 0;
}
