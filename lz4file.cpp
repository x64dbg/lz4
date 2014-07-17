/*
    LZ4Demo - Demo CLI program using LZ4 compression
    Copyright (C) Yann Collet 2011,

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

	You can contact the author at :
	- LZ4 homepage : http://fastcompression.blogspot.com/p/lz4.html
	- LZ4 source repository : http://code.google.com/p/lz4/
*/
/*
	Note : this is *only* a demo program, an example to show how LZ4 can be used.
	It is not considered part of LZ4 compression library.
	The license of LZ4 is BSD.
	The license of the demo program is GPL.
*/

//****************************
// Includes
//****************************
#include <stdio.h>		// fprintf, fopen, fread, _fileno(?)
#include <stdlib.h>		// malloc
#include <string.h>		// strcmp
#include <time.h>		// clock
#ifdef _WIN32
#include <io.h>			// _setmode
#include <fcntl.h>		// _O_BINARY
#endif
#include "lz4.h"
#include "lz4file.h"
#include <windows.h>


//**************************************
// Compiler functions
//**************************************
#define GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)

#if defined(_MSC_VER)    // Visual Studio
#define swap32 _byteswap_ulong
#elif GCC_VERSION >= 402
#define swap32 __builtin_bswap32
#else
static inline unsigned int swap32(unsigned int x)
{
    return	((x << 24) & 0xff000000 ) |
            ((x <<  8) & 0x00ff0000 ) |
            ((x >>  8) & 0x0000ff00 ) |
            ((x >> 24) & 0x000000ff );
}
#endif

#define CHUNKSIZE (8<<20)    // 8 MB
#define CACHELINE 64
#define ARCHIVE_MAGICNUMBER 0x184C2102
#define ARCHIVE_MAGICNUMBER_SIZE 4


//**************************************
// Architecture Macros
//**************************************
static const int one = 1;
#define CPU_LITTLE_ENDIAN (*(char*)(&one))
#define CPU_BIG_ENDIAN (!CPU_LITTLE_ENDIAN)
#define LITTLE_ENDIAN32(i)   if (CPU_BIG_ENDIAN) { i = swap32(i); }


static LZ4_STATUS get_fileHandle(const char* input_filename, const char* output_filename, FILE** pfinput, FILE** pfoutput)
{
    if(fopen_s(pfinput, input_filename, "rb"))
        return LZ4_FAILED_OPEN_INPUT;
    if(fopen_s(pfoutput, output_filename, "wb"))
        return LZ4_FAILED_OPEN_OUTPUT;
    return LZ4_SUCCESS;
}


LZ4_STATUS LZ4_compress_file(const char* input_filename, const char* output_filename)
{
    unsigned long long filesize = 0;
    unsigned long long compressedfilesize = ARCHIVE_MAGICNUMBER_SIZE;
    unsigned int u32var;
    char* in_buff;
    char* out_buff;
    FILE* finput;
    FILE* foutput;
    LZ4_STATUS r;
    char real_output[MAX_PATH]="";
    const char* output=output_filename;
    if(!_stricmp(input_filename, output_filename))
    {
        output=real_output;
        sprintf(output, "temp_%X.lz4", GetTickCount());
    }

    r = get_fileHandle(input_filename, output, &finput, &foutput);
    if (r!=LZ4_SUCCESS)
    {
        DeleteFileA(output);
        return r;
    }

    // Allocate Memory
    in_buff = (char*)malloc(CHUNKSIZE);
    out_buff = (char*)malloc(LZ4_compressBound(CHUNKSIZE));
    if (!in_buff || !out_buff)
    {
        if(in_buff)
            free(in_buff);
        if(out_buff)
            free(out_buff);
        fclose(finput);
        fclose(foutput);
        DeleteFileA(output);
        return LZ4_NOT_ENOUGH_MEMORY;
    }

    // Write Archive Header
    u32var = ARCHIVE_MAGICNUMBER;
    LITTLE_ENDIAN32(u32var);
    *(unsigned int*)out_buff = u32var;
    fwrite(out_buff, 1, ARCHIVE_MAGICNUMBER_SIZE, foutput);

    // Main Loop
    while (1)
    {
        int outSize;
        // Read Block
        int inSize = fread(in_buff, 1, CHUNKSIZE, finput);
        if( inSize<=0 )
            break;
        filesize += inSize;

        // Compress Block
        outSize = LZ4_compress(in_buff, out_buff+4, inSize);
        compressedfilesize += outSize+4;

        // Write Block
        LITTLE_ENDIAN32(outSize);
        * (unsigned int*) out_buff = outSize;
        LITTLE_ENDIAN32(outSize);
        fwrite(out_buff, 1, outSize+4, foutput);
    }

    // Close & Free
    free(in_buff);
    free(out_buff);
    fclose(finput);
    fclose(foutput);

    if(output!=output_filename)
    {
        DeleteFileA(output_filename);
        MoveFileA(output, output_filename);
    }

    return LZ4_SUCCESS;
}

LZ4_STATUS LZ4_decompress_file(const char* input_filename, const char* output_filename)
{
    unsigned long long filesize = 0;
    char* in_buff;
    char* out_buff;
    size_t uselessRet;
    int sinkint;
    unsigned int nextSize;
    FILE* finput;
    FILE* foutput;
    LZ4_STATUS r;

    char real_output[MAX_PATH]="";
    const char* output=output_filename;
    if(!_stricmp(input_filename, output_filename))
    {
        output=real_output;
        sprintf(output, "temp_%X.lz4", GetTickCount());
    }

    r = get_fileHandle(input_filename, output, &finput, &foutput);
    if (r!=LZ4_SUCCESS)
    {
        DeleteFileA(output);
        return r;
    }

    // Allocate Memory
    in_buff = (char*)malloc(LZ4_compressBound(CHUNKSIZE));
    out_buff = (char*)malloc(CHUNKSIZE);
    if (!in_buff || !out_buff)
    {
        if(in_buff)
            free(in_buff);
        if(out_buff)
            free(out_buff);
        fclose(finput);
        fclose(foutput);
        DeleteFileA(output);
        return LZ4_NOT_ENOUGH_MEMORY;
    }

    // Check Archive Header
    uselessRet = fread(out_buff, 1, ARCHIVE_MAGICNUMBER_SIZE, finput);
    nextSize = *(unsigned int*)out_buff;
    LITTLE_ENDIAN32(nextSize);
    if (nextSize != ARCHIVE_MAGICNUMBER)
    {
        free(in_buff);
        free(out_buff);
        fclose(finput);
        fclose(foutput);
        DeleteFileA(output);
        return LZ4_INVALID_ARCHIVE;
    }

    // First Block
    *(unsigned int*)in_buff = 0;
    uselessRet = fread(in_buff, 1, 4, finput);
    nextSize = *(unsigned int*)in_buff;
    LITTLE_ENDIAN32(nextSize);

    // Main Loop
    while (1)
    {
        // Read Block
        uselessRet = fread(in_buff, 1, nextSize, finput);

        // Check Next Block
        uselessRet = (size_t) fread(&nextSize, 1, 4, finput);
        if( uselessRet==0 )
            break;   // Nothing read : file read is completed
        LITTLE_ENDIAN32(nextSize);

        // Decode Block
        sinkint = LZ4_uncompress(in_buff, out_buff, CHUNKSIZE);
        if (sinkint < 0)
        {
            free(in_buff);
            free(out_buff);
            fclose(finput);
            fclose(foutput);
            DeleteFileA(output);
            return LZ4_CORRUPTED_ARCHIVE;
        }
        filesize += CHUNKSIZE;

        // Write Block
        fwrite(out_buff, 1, CHUNKSIZE, foutput);
    }

    // Last Block (which size is <= CHUNKSIZE, but let LZ4 figure that out)
    uselessRet = fread(in_buff, 1, nextSize, finput);
    sinkint = LZ4_uncompress_unknownOutputSize(in_buff, out_buff, nextSize, CHUNKSIZE);
    filesize += sinkint;
    fwrite(out_buff, 1, sinkint, foutput);

    // Close & Free
    free(in_buff);
    free(out_buff);
    fclose(finput);
    fclose(foutput);

    if(output!=output_filename)
    {
        DeleteFileA(output_filename);
        MoveFileA(output, output_filename);
    }

    return LZ4_SUCCESS;
}
