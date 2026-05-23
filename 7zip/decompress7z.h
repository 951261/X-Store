/*
FILE : decompress7z.h
PROJECT : xstore
PROGRAMMER : 951261
DESCRIPTION : Header file for 7z decompression
*/

#ifndef DECOMPRESS_7Z
#define DECOMPRESS_7Z

int decompressSevenZipFile(const char *inputFile, const char *outputPath, const bool isXBLA); // decompress a .7z file

#endif