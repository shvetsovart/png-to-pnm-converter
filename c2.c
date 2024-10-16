#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "return_codes.h"

#define ZLIB
//#define LIBDEFLATE
//#define ISAL

#ifdef ZLIB
	#include <zlib.h>
#elif defined(LIBDEFLATE)
	#include <libdeflate.h>
#elif defined(ISAL)
	#include <include/igzip_lib.h>
#else
	#error no library define found
#endif

unsigned int swapEndians(unsigned int n)
{
	return ((n >> 24) & 0x000000ff) | ((n >> 8) & 0x0000ff00) | ((n << 8) & 0x00ff0000) | ((n << 24) & 0xff000000);
}

unsigned char PaethPredictor(unsigned char a, unsigned char b, unsigned char c)
{
	int p = a + b - c;	  // a + b - c
	int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
	if (pa <= pb && pa <= pc)
	{
		return a;
	}
	else if (pb <= pc)
	{
		return b;
	}
	else
	{
		return c;
	}
}

// IHDR tag structure
struct ihdrTag
{
	unsigned int length;	// 4
	char type[4];			// 4

	unsigned int width;				  // 4
	unsigned int height;			  // 4
	unsigned char bitDepth;			  // 1
	unsigned char colorType;		  // 1
	unsigned char compressionType;	  // 1
	unsigned char filterType;		  // 1
	unsigned char interlaceType;	  // 1

	unsigned int CRC;	 // 4
};

// union to scan the data of IHDR tag easier
union header
{
	unsigned char buffer[25];

	struct ihdrTag data;
};

// first 8 bytes of each tag
struct lengthTypeOfTag
{
	unsigned int length;	// 4
	char type[4];			// 4
};

// union for checking lengths and types of tags
union lengthType
{
	unsigned char buffer[8];

	struct lengthTypeOfTag data;
};

// make sure first 8 bytes of the signature match the PNG signature bytes
char pngCheckSignature(FILE *in)
{
	static int pngSignature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };

	unsigned char sig[8];
	fread(sig, 1, 8, in);

	for (int i = 0; i < 8; ++i)
	{
		if (pngSignature[i] != sig[i])
		{
			return 0;
		}
	}

	return 1;
}

int main(int argc, char *argv[])
{
	if (argc != 3)
	{
		return ERROR_INVALID_PARAMETER;
	}
	FILE *in = fopen(argv[1], "rb");
	if (in == NULL)
	{
		return ERROR_FILE_NOT_FOUND;
	}
	// invalid PNG check,
	// no need to check file name (to make sure it's *.png)
	// because it'd be seen in signature check anyways
	if (!pngCheckSignature(in))
	{
		fclose(in);
		return ERROR_INVALID_DATA;
	}

	FILE *out = fopen(argv[2], "wb");
	if (out == NULL)
	{
		fclose(in);
		return ERROR_PATH_NOT_FOUND;
	}

	union header ihdr;
	size_t fr = fread(ihdr.buffer, 1, 25, in);
	if (fr != 25)
	{
		fclose(in);
		fclose(out);
		return ERROR_INVALID_DATA;
	}

	unsigned int colorType = ihdr.data.colorType, width = swapEndians(ihdr.data.width), height = swapEndians(ihdr.data.height);

	union lengthType lt;
	fr = fread(lt.buffer, 1, 8, in);
	if (fr != 8)
	{
		fclose(in);
		fclose(out);
		return ERROR_INVALID_DATA;
	}

	// skip not important tags
	while (lt.data.type[0] != 'I' || lt.data.type[1] != 'D' || lt.data.type[2] != 'A' || lt.data.type[3] != 'T')
	{
		unsigned int frame_length = swapEndians(lt.data.length);
		//        fprintf(out, "%d\n", frame_length);
		int fs = fseek(in, frame_length + 4, SEEK_CUR);	   // +4 because of the CRC
		if (fs != 0)
		{
			fclose(in);
			fclose(out);
			return ERROR_INVALID_DATA;
		}
		fr = fread(lt.buffer, 1, 8, in);	// read length and type
		if (fr != 8)
		{
			fclose(in);
			fclose(out);
			return ERROR_INVALID_DATA;
		}
	}

	unsigned int widthMult = (colorType == 2 ? 3 : 1);
	unsigned long uncompressedDataLength = height + height * width * widthMult;
	unsigned char *uncompressedData;
	unsigned char *data;
	unsigned long dataLength = 0;

	// check all IDAT tags
	while (lt.data.type[0] == 'I' && lt.data.type[1] == 'D' && lt.data.type[2] == 'A' && lt.data.type[3] == 'T')
	{
		//        printf("Current tag: %c%c%c%c\n", lt.data.type[0], lt.data.type[1], lt.data.type[2], lt.data.type[3]);
		unsigned long curDataLength = swapEndians(lt.data.length);
		//        printf("Current data length: %lu\n", curDataLength);

		unsigned char *curData = malloc(curDataLength);
		if (curData == NULL)
		{
			fclose(in);
			fclose(out);
			free(data);
			return ERROR_NOT_ENOUGH_MEMORY;
		}

		fr = fread(curData, 1, curDataLength, in);
		if (fr != curDataLength)
		{
			fclose(in);
			fclose(out);
			free(curData);
			free(data);
			return ERROR_INVALID_DATA;
		}

		//        printf("dataLength: %lu, curDataLength: %lu, data: %d\n", dataLength, curDataLength, data);

		if (dataLength == 0)
			data = malloc(curDataLength);
		else
		{
			unsigned char *tmp = realloc(data, dataLength + curDataLength);

			if (tmp == NULL)
			{
				fclose(in);
				fclose(out);
				free(curData);
				free(data);
				return ERROR_NOT_ENOUGH_MEMORY;
			}
			else
			{
				data = tmp;
			}
		}

		if (data == NULL)
		{
			fclose(in);
			fclose(out);
			free(curData);
			return ERROR_NOT_ENOUGH_MEMORY;
		}

		memcpy(data + dataLength, curData, curDataLength);	  // copy data from curData to data
		dataLength += curDataLength;

		free(curData);
		int fs = fseek(in, 4, SEEK_CUR);	// skip crc
		if (fs != 0)
		{
			fclose(in);
			fclose(out);
			free(data);
			return ERROR_INVALID_DATA;
		}
		fr = fread(lt.buffer, 1, 8, in);	// scan type and size
		if (fr != 8)
		{
			fclose(in);
			fclose(out);
			free(data);
			return ERROR_INVALID_DATA;
		}
	}
	uncompressedData = malloc(uncompressedDataLength);
	if (uncompressedData == NULL)
	{
		fclose(in);
		fclose(out);
		free(data);
		return ERROR_NOT_ENOUGH_MEMORY;
	}

#ifdef ZLIB
	// zlib uncompress
	int uncompressed = uncompress(uncompressedData, &uncompressedDataLength, data, dataLength);
	if (uncompressed != Z_OK)
	{
		fclose(in);
		fclose(out);
		free(data);
		free(uncompressedData);
		return ERROR_INVALID_DATA;
	}
#elif defined(LIBDEFLATE)
	// libdeflate uncompress
	int uncompressed =
		libdeflate_zlib_decompress(libdeflate_alloc_decompressor(), data, dataLength, uncompressedData, uncompressedDataLength, NULL);
	if (uncompressed != 0)
	{
		fclose(in);
		fclose(out);
		free(data);
		free(uncompressedData);
		return ERROR_INVALID_DATA;
	}
#elif defined(ISAL)
	// isa-l uncompress
	struct inflate_state infl;
	isal_inflate_init(&infl);
	infl.next_in = data;
	infl.avail_in = dataLength;
	infl.next_out = uncompressedData;
	infl.avail_out = uncompressedDataLength;
	infl.crc_flag = IGZIP_ZLIB;
	if (isal_inflate(&infl) != ISAL_DECOMP_OK)
	{
		fclose(in);
		fclose(out);
		free(data);
		free(uncompressedData);
		return ERROR_INVALID_DATA;
	}
#endif
	// write type of .pnm
	if (colorType == 0)
	{
		fprintf(out, "P5\n");
	}
	else if (colorType == 2)
	{
		fprintf(out, "P6\n");
	}
	else
	{
		fclose(in);
		fclose(out);
		free(uncompressedData);
		free(data);
		return ERROR_INVALID_DATA;
	}

	fprintf(out, "%d %d\n255\n", width, height);	// print width, height, max value of the
													// colour components

	int bpp = 0;
	if (colorType == 2)
	{
		bpp = 3 * (ihdr.data.bitDepth / 8);
	}
	else
	{
		bpp = (ihdr.data.bitDepth / 8);
	}
	if (bpp == 0)
	{
		bpp = 1;
	}

	// unfilter
	unsigned char *finalData = malloc(uncompressedDataLength - height);
	if (finalData == NULL)
	{
		fclose(in);
		fclose(out);
		free(uncompressedData);
		free(data);
		return ERROR_NOT_ENOUGH_MEMORY;
	}
	int x = 1;

	for (int i = 0; i < uncompressedDataLength; ++i)
	{
		if (uncompressedData[i] == 0)
		{
			for (int j = 1; j <= widthMult * width; ++j)
			{
				finalData[i + j - x] = uncompressedData[i + j];
			}
		}
		else if (uncompressedData[i] == 1)
		{
			for (int j = 1; j <= widthMult * width; ++j)
			{
				if (j - bpp < 1)
				{
					finalData[i + j - x] = uncompressedData[i + j];
				}
				else
				{
					finalData[i + j - x] = uncompressedData[i + j] + finalData[i + j - x - bpp];
				}
			}
		}
		else if (uncompressedData[i] == 2)
		{
			for (int j = 1; j <= widthMult * width; ++j)
			{
				if (i == 0)
				{
					finalData[i + j - x] = uncompressedData[i + j];
				}
				else
				{
					finalData[i + j - x] = uncompressedData[i + j] + finalData[i + j - x - widthMult * width];
				}
			}
		}
		else if (uncompressedData[i] == 3)
		{
			for (int j = 1; j <= widthMult * width; ++j)
			{
				unsigned char raw = (j - bpp < 1 ? 0 : finalData[i + j - x - bpp]);
				unsigned char prior = (i == 0 ? 0 : finalData[i + j - x - widthMult * width]);
				finalData[i + j - x] = uncompressedData[i + j] + (raw + prior) / 2;
			}
		}
		else if (uncompressedData[i] == 4)
		{
			for (int j = 1; j <= widthMult * width; ++j)
			{
				unsigned char raw = (j - bpp < 1 ? 0 : finalData[i + j - x - bpp]);
				unsigned char prior = (i == 0 ? 0 : finalData[i + j - x - widthMult * width]);
				unsigned char priorBpp = ((i == 0 || j - bpp < 1) ? 0 : finalData[i + j - x - widthMult * width - bpp]);
				finalData[i + j - x] = uncompressedData[i + j] + PaethPredictor(raw, prior, priorBpp);
			}
		}
		else
		{
			fclose(in);
			fclose(out);
			free(uncompressedData);
			free(finalData);
			free(data);
			return ERROR_INVALID_DATA;
		}

		x++;
		i += widthMult * width;
	}

	fwrite(finalData, 1, uncompressedDataLength - height, out);	   // write data in binary mode

	fclose(in);
	fclose(out);
	free(uncompressedData);
	free(finalData);
	free(data);

	return ERROR_SUCCESS;
}
