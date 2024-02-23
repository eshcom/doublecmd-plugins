#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <lz4.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <string.h>
#include "wcxplugin.h"

#define HEADER "mozLz40\0"
#define HEADER_SIZE 8
#define LEN_BLOCK_SIZE 4

typedef struct sArcData
{
	FILE *ifp;
	size_t size;
	size_t src_size;
	char arcname[PATH_MAX];
	tProcessDataProc ProcessDataProc;
} tArcData;

typedef tArcData* ArcData;

tProcessDataProc gProcessDataProc = NULL;

HANDLE DCPCALL OpenArchive(tOpenArchiveData *ArchiveData)
{
	tArcData *data;
	data = malloc(sizeof(tArcData));

	if (data == NULL)
	{
		ArchiveData->OpenResult = E_NO_MEMORY;
		return E_SUCCESS;
	}

	memset(data, 0, sizeof(tArcData));

	data->ifp = fopen(ArchiveData->ArcName, "r");

	if (!data->ifp)
	{
		ArchiveData->OpenResult = E_EOPEN;
		free(data);
		return E_SUCCESS;
	}

	char head_block[HEADER_SIZE];
	fread(head_block, 1, HEADER_SIZE, data->ifp);

	if (strcmp(head_block, HEADER) != 0)
	{
		ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
		fclose(data->ifp);
		free(data);
		return E_SUCCESS;
	}

	char len_block[LEN_BLOCK_SIZE];
	size_t ret = fread(len_block, 1, LEN_BLOCK_SIZE, data->ifp);

	if (ret == 0)
	{
		ArchiveData->OpenResult = E_BAD_DATA;
		fclose(data->ifp);
		free(data);
		return E_SUCCESS;
	}

	data->src_size = *(uint32_t*)len_block;
	snprintf(data->arcname, PATH_MAX, "%s", ArchiveData->ArcName);

	return (HANDLE)data;
}

int DCPCALL ReadHeader(HANDLE hArcData, tHeaderData *HeaderData)
{
	return E_NOT_SUPPORTED;
}

int DCPCALL ReadHeaderEx(HANDLE hArcData, tHeaderDataEx *HeaderDataEx)
{
	ArcData data = (ArcData)hArcData;

	if (data->arcname[0] == '\0')
		return E_END_ARCHIVE;

	struct stat st;
	memset(HeaderDataEx, 0, sizeof(&HeaderDataEx));

	if (stat(data->arcname, &st) == 0)
	{
		HeaderDataEx->PackSizeHigh = (st.st_size & 0xFFFFFFFF00000000) >> 32;
		HeaderDataEx->PackSize = st.st_size & 0x00000000FFFFFFFF;
		HeaderDataEx->UnpSizeHigh = (data->src_size & 0xFFFFFFFF00000000) >> 32;
		HeaderDataEx->UnpSize = data->src_size & 0x00000000FFFFFFFF;
		HeaderDataEx->FileTime = st.st_mtime;
		data->size = st.st_size;
		HeaderDataEx->FileAttr = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	}

	char *name = basename(data->arcname);
	char *dot = strrchr(name, '.');

	if (dot != NULL)
		*dot = '\0';

	snprintf(HeaderDataEx->FileName, sizeof(HeaderDataEx->FileName) - 1, "%s.json", name);

	return E_SUCCESS;
}

int DCPCALL ProcessFile(HANDLE hArcData, int Operation, char *DestPath, char *DestName)
{
	ArcData data = (ArcData)hArcData;

	if (Operation == PK_EXTRACT)
	{
		if (data->size <= HEADER_SIZE + LEN_BLOCK_SIZE || data->size - LEN_BLOCK_SIZE > UINT32_MAX) 
			return E_NOT_SUPPORTED;

		size_t data_size = data->size - (HEADER_SIZE + LEN_BLOCK_SIZE);
		char* compressed_data = (char*)malloc(data_size);
		memset(compressed_data, 0, data_size);
		size_t ret = fread(compressed_data, 1, data_size, data->ifp);

		if (ret == 0)
		{
			free(compressed_data);
			return E_EREAD;
		}

		char* regen_buffer = (char*)malloc(data->src_size);
		memset(regen_buffer, 0, data->src_size);
		const int decompressed_size = LZ4_decompress_safe(compressed_data, regen_buffer, ret, data->src_size);
		free(compressed_data);

		if (data->ProcessDataProc)
			data->ProcessDataProc(DestName, decompressed_size);

		FILE *ofp = fopen(DestName, "wb");

		if (!ofp)
		{
			free(regen_buffer);
			return E_EWRITE;
		}

		ret = fwrite(regen_buffer, 1, data->src_size, ofp);
		free(regen_buffer);
		fclose(ofp);
	}

	data->arcname[0] = '\0';

	return E_SUCCESS;
}

int DCPCALL CloseArchive(HANDLE hArcData)
{
	ArcData data = (ArcData)hArcData;

	fclose(data->ifp);
	free(data);

	return E_SUCCESS;
}

void DCPCALL SetProcessDataProc(HANDLE hArcData, tProcessDataProc pProcessDataProc)
{
	ArcData data = (ArcData)hArcData;

	if ((int)(long)hArcData == -1 || !data)
		gProcessDataProc = pProcessDataProc;
	else
		data->ProcessDataProc = pProcessDataProc;
}

int DCPCALL GetPackerCaps(void)
{
	return PK_CAPS_BY_CONTENT | PK_CAPS_SEARCHTEXT;
}

BOOL DCPCALL CanYouHandleThisFile(char *FileName)
{
	FILE *fp = fopen(FileName, "r");

	if (!fp)
		return false;

	char head_block[HEADER_SIZE];
	fread(head_block, 1, HEADER_SIZE, fp);
	fclose(fp);

	if (strcmp(head_block, HEADER) == 0)
		return true;

	return false;
}
