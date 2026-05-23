/*
FILE : Corona4G.c
PROJECT : xstore
PROGRAMMER : 951261
DESCRIPTION : Modified from Simple 360 NAND Flasher. Only useful function is to mount a drive (USB, HDD, etc...)
*/

#include <xtl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "Corona4G.h"
#include "OutputConsole.h"

#define BUF_SIZE	0x8000
DWORD flash_sz = 0, TTL_REPS = 0, totalBytes = 0, totalRead = 0, totalWrote = 0;
BYTE buf[BUF_SIZE];

HRESULT mount(const char* szDrive, char* szDevice)
{
	STRING DeviceName, LinkName;
	CHAR szDestinationDrive[MAX_PATH];
	sprintf_s(szDestinationDrive, MAX_PATH, "\\??\\%s", szDrive);
	RtlInitAnsiString(&DeviceName, szDevice);
	RtlInitAnsiString(&LinkName, szDestinationDrive);
	ObDeleteSymbolicLink(&LinkName);
	return (HRESULT)ObCreateSymbolicLink(&LinkName, &DeviceName);
}
/*
char sizeReadable[0x40] = { 0 };
char* GetSizeReadable(unsigned int i)
{
	if (i >= 0x40000000) // Gigabyte
		sprintf_s(sizeReadable, sizeof(sizeReadable), "%3.2f GB", (float)(i >> 20) / (float)1024);
	else if (i >= 0x100000) // Megabyte
		sprintf_s(sizeReadable, sizeof(sizeReadable), "%3.2f MB", (float)(i >> 10) / (float)1024);
	else if (i >= 0x400) // Kilobyte
		sprintf_s(sizeReadable, sizeof(sizeReadable), "%3.2f KB", (float)i / (float)1024);
	else
		sprintf_s(sizeReadable, sizeof(sizeReadable), "%3.2f B", (float)i / (float)1024);
	return sizeReadable;
}

int getflashsz(void)
{
	FILE * fd;
	dprintf(MSG_GETTING_NAND_SIZE);
	if (mount("Flash:", "\\Device\\Flash") != 0)
	{
		dprintf(MSG_ERROR MSG_UNABLE_TO_MOUNT_FLASH);
		return 0;
	}
	if (fopen_s(&fd, "Flash:", "rb") != 0)
	{
		dprintf(MSG_ERROR MSG_UNABLE_TO_OPEN_FLASH_FOR_READING);
		fclose(fd);
		return 0;
	}
	fseek(fd, 0L, SEEK_END);
	flash_sz = ftell(fd);
	fclose(fd);
	dprintf(MSG_SIZE, flash_sz, GetSizeReadable(flash_sz));
	return 1;
}

void try_rawdump4g(char* filename)
{
	HANDLE flash, outf;
	DWORD bRead, bWrote;
	DWORD i;
	totalRead = 0;
	totalWrote = 0;
#ifdef USE_UNICODE
	dprintf(L" * rawdump4g v1 started (by Swizzy)\n");
#else
	dprintf(" * rawdump4g v1 started (by Swizzy)\n");
#endif
	if (getflashsz() == 0)
	{
		dprintf(MSG_ERROR MSG_UNABLE_TO_GET_SIZE_OF_FLASH);
		return;
	}
	TTL_REPS = (flash_sz/BUF_SIZE);
	flash = CreateFile("Flash:", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);	
	if(flash == INVALID_HANDLE_VALUE)
	{
		dprintf(MSG_ERROR MSG_UNABLE_TO_OPEN_FLASH_FOR_READING);
		return;
	}
	outf = CreateFile(filename, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(outf == INVALID_HANDLE_VALUE)
	{
		dprintf(MSG_ERROR MSG_UNABLE_TO_OPEN_FOR_WRITING, filename);
		return;
	}
		dprintf(MSG_OPENED_OK_ATTEMPTING_READ_FROM_FLASH_READABLE, filename, flash_sz, GetSizeReadable(flash_sz));
		for(i = 0; i < TTL_REPS; i++)
		{
			ReadFile(flash, buf, BUF_SIZE, &bRead, NULL);
			if(bRead != BUF_SIZE)
				dprintf(MSG_ERROR_READING_BYTES, BUF_SIZE, totalBytes, bRead);
			WriteFile(outf, buf, BUF_SIZE, &bWrote, NULL);
			if(bWrote != BUF_SIZE)
				dprintf(MSG_ERROR_WRITING_BYTES, BUF_SIZE, totalBytes, bWrote);
			totalBytes += BUF_SIZE;
			totalRead += bRead;
			totalWrote += bWrote;
			if(i%0x20) // only update this every 32 buffer fills to speed things along...
				dprintf(MSG_PROCESSED_OF_BYTES, totalBytes, flash_sz, (((float)totalBytes / (float)flash_sz) * 100));
		}
#ifdef USE_UNICODE
		dprintf(L"\r");
#else
		dprintf("\r");
#endif
		CloseHandle(outf);
		CloseHandle(flash);
		if (totalRead - totalWrote == 0)
			dprintf(MSG_DONE_SUCCESS_DUMPED_BYTES, totalBytes, filename);
		else
			dprintf(MSG_DONE_READ_BYTES_FROM_NAND_WROTE, totalRead, totalWrote, filename);
}

void try_rawflash4g(char* filename)
{
	PBYTE data;
	DWORD filesize;
#ifdef USE_UNICODE
	dprintf(L" * rawflash4g v1 started (by Swizzy)\n");
#else
	dprintf(" * rawflash4g v1 started (by Swizzy)\n");
#endif
	if (getflashsz() == 0)
	{
		dprintf(MSG_ERROR MSG_UNABLE_TO_GET_SIZE_OF_FLASH);
		return;
	}
	dprintf(MSG_READING_TO_MEMORY);
	data = FileToBuffer(filename, &filesize);
	if (data == NULL)
		return;
	if (flash_sz < filesize)
	{
		dprintf(MSG_ERROR MSG_IS_BIGGER_THEN_FLASH, filename);
		free(data);
		return;
	}
	if (flash_sz > filesize)
	{
		dprintf(MSG_ERROR MSG_IS_SMALLER_THEN_FLASH, filename);
		free(data);
		return;
	}
	dprintf(MSG_WRITING_TO_FLASH);
	if (Corona4GWrite(data))
		dprintf(MSG_DONE_SUCCESS_WROTE_BYTES, filesize, filename);
	else
	dprintf(MSG_DONE_FAILED_WRITE_BYTES, filesize, filename);
}

BOOL Corona4GWrite(PBYTE data)
{
	HANDLE hFile;
	OBJECT_ATTRIBUTES atFlash;
	IO_STATUS_BLOCK ioFlash;
	DWORD sta = 0, bWrote = 0, len = flash_sz;
	PBYTE srcdata = data;
	LARGE_INTEGER lOffset;
	STRING nFlash;
	BOOL error;
	RtlInitAnsiString(&nFlash, "\\Device\\Flash");
	atFlash.RootDirectory = 0;
	atFlash.ObjectName = &nFlash;
	atFlash.Attributes = FILE_ATTRIBUTE_DEVICE;
	if (NtOpenFile(&hFile, GENERIC_WRITE|GENERIC_READ|SYNCHRONIZE, &atFlash, &ioFlash, OPEN_EXISTING, FILE_SYNCHRONOUS_IO_NONALERT) != 0)
	{
		dprintf(MSG_ERROR MSG_UNABLE_TO_OPEN_FLASH_FOR_WRITING);
		return FALSE;
	}
	lOffset.QuadPart = 0;
	error = FALSE;
	while (len > 0x4000)
	{
		KillControllers();
		bWrote = 0;
		sta = NtWriteFile(hFile, 0, 0, 0, &ioFlash, srcdata, 0x4000, &lOffset);
		if (sta < 0)
		{
#ifdef USE_UNICODE
			dprintf(L"\r" MSG_ERROR MSG_UNABLE_TO_WRITE_BYTES_TO_FLASH, 0x4000, GetLastError(), lOffset.LowPart);
#else
			dprintf("\r" MSG_ERROR MSG_UNABLE_TO_WRITE_BYTES_TO_FLASH, 0x4000, GetLastError(), lOffset.LowPart);
#endif
			error = TRUE;
		}
		else
			bWrote = 0x4000;
		len -= bWrote;
		srcdata += bWrote;
		lOffset.QuadPart += bWrote;
		if ((lOffset.LowPart%(0x4000*32)) == 0)
			dprintf(MSG_PROCESSED_OF_BYTES, lOffset.LowPart, flash_sz, (((float)lOffset.LowPart / (float)flash_sz) * 100));
	}
#ifdef USE_UNICODE
	dprintf(L"\r");
#else
	dprintf("\r");
#endif
	if (len > 0)
	{
		bWrote = 0;
		sta = NtWriteFile(hFile, 0, 0, 0, &ioFlash, srcdata, len, &lOffset);
		if (sta < 0)
		{
#ifdef USE_UNICODE
			dprintf(L"\r" MSG_ERROR MSG_UNABLE_TO_WRITE_BYTES_TO_FLASH, len, GetLastError(), lOffset.LowPart);
#else
			dprintf("\r" MSG_ERROR MSG_UNABLE_TO_WRITE_BYTES_TO_FLASH, len, GetLastError(), lOffset.LowPart);
#endif
			error = TRUE;
		}
		else
			bWrote = len;
		lOffset.QuadPart += bWrote;
	}
	NtClose(hFile);
	if (error)
		return FALSE;
	return TRUE;
}

PBYTE FileToBuffer(char * file, PDWORD filesize)
{
	HANDLE inf;
	PBYTE dest = NULL;
	DWORD bRead, lSize;
	filesize[0] = 0;
	inf = CreateFile(file, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (inf == INVALID_HANDLE_VALUE)
	{
		dprintf(MSG_ERROR MSG_UNABLE_TO_OPEN_FOR_READING, file, GetLastError());
		return dest;
	}
	lSize = GetFileSize(inf, NULL);
	dest = (PBYTE)malloc(lSize);
	if (dest == NULL)
	{
		dprintf(MSG_ERROR MSG_UNABLE_TO_ALLOCATE_BUFFER_FOR, lSize, file);
		CloseHandle(inf);
		return dest;
	}
	ReadFile(inf, dest, lSize, &bRead, NULL);
	if (bRead != lSize)
	{
		dprintf(MSG_ERROR MSG_UNABLE_TO_READ_FROM, lSize, file, bRead);
		CloseHandle(inf);
		return NULL;
	}
	CloseHandle(inf);
	filesize[0] = lSize;
	return dest;
}
*/