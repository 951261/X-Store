/*
FILE : parsing.cpp
PROJECT : xstore
PROGRAMMER : 951261
DESCRIPTION : parses HTTP headers
*/

#include "parsing.h"

#include <string.h>
#include <cstdlib>
#include <ctype.h>
#include <stdio.h>

#define ERROR(s) (printf("\nERROR: %s\n", s))

char LowerAscii(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c + ('a' - 'A'));
    return c;
}

bool EqualsNoCase(const char *a, int aLen, const char *b)
{
    int i;
    for (i = 0; i < aLen && b[i] != '\0'; ++i)
    {
        if (LowerAscii(a[i]) != LowerAscii(b[i]))
            return false;
    }

    return i == aLen && b[i] == '\0';
}

bool ContainsNoCase(const char *data, int dataLen, const char *needle)
{
    int needleLen = (int)strlen(needle);
    int i, j;

    if (needleLen <= 0 || needleLen > dataLen)
        return false;

    for (i = 0; i <= dataLen - needleLen; ++i)
    {
        for (j = 0; j < needleLen; ++j)
        {
            if (LowerAscii(data[i + j]) != LowerAscii(needle[j]))
                break;
        }

        if (j == needleLen)
            return true;
    }

    return false;
}

bool IncrementSplitFilename(char *filename)
{
    int len = (int)strlen(filename);
    int i;

    for (i = len - 1; i >= 0; --i)
    {
        if (filename[i] < '0' || filename[i] > '9')
            break;
    }

    if (i == len - 1)
        return false;

    for (i = len - 1; i >= 0 && filename[i] >= '0' && filename[i] <= '9'; --i)
    {
        if (filename[i] < '9')
        {
            filename[i]++;
            return true;
        }

        filename[i] = '0';
    }

    return false;
}

int FindHeaderEnd(const char *data, int len)
{
    int i;

    for (i = 0; i <= len - 4; ++i)
    {
        if (data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n')
            return i + 4;
    }

    for (i = 0; i <= len - 2; ++i)
    {
        if (data[i] == '\n' && data[i + 1] == '\n')
            return i + 2;
    }

    return -1;
}

bool HeaderContainsToken(const char *headers, int headerLen, const char *name, const char *token)
{
    int pos = 0;

    while (pos < headerLen)
    {
        int lineStart = pos;
        int lineEnd;
        int colon = -1;
        int nameEnd;
        int i;

        while (pos < headerLen && headers[pos] != '\r' && headers[pos] != '\n')
            ++pos;
        lineEnd = pos;

        while (pos < headerLen && (headers[pos] == '\r' || headers[pos] == '\n'))
            ++pos;

        if (lineEnd == lineStart)
            break;

        for (i = lineStart; i < lineEnd; ++i)
        {
            if (headers[i] == ':')
            {
                colon = i;
                break;
            }
        }

        if (colon < 0)
            continue;

        nameEnd = colon;
        while (nameEnd > lineStart && (headers[nameEnd - 1] == ' ' || headers[nameEnd - 1] == '\t'))
            --nameEnd;

        if (EqualsNoCase(headers + lineStart, nameEnd - lineStart, name) &&
            ContainsNoCase(headers + colon + 1, lineEnd - colon - 1, token))
            return true;
    }

    return false;
}

int HexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool ParseChunkSize(const char *line, int len, unsigned long *size)
{
    unsigned long value = 0;
    bool sawDigit = false;
    int i = 0;

    while (i < len && (line[i] == ' ' || line[i] == '\t'))
        ++i;

    for (; i < len; ++i)
    {
        int hex;

        if (line[i] == ';' || line[i] == ' ' || line[i] == '\t')
            break;

        hex = HexValue(line[i]);
        if (hex < 0)
            return false;

        sawDigit = true;
        value = (value << 4) | (unsigned long)hex;
    }

    if (!sawDigit)
        return false;

    *size = value;
    return true;
}

void InitChunkedDecodeState(ChunkedDecodeState *state)
{
    state->remaining = 0;
    state->sizeLineLen = 0;
    state->state = 0;
}


int parseURL(const char *URL, char *domain, char *path)
{
    // parse a URL
    char *tempURL;
	if(( tempURL = (char*)malloc(strlen(URL) + 2) ) == NULL) {
		ERROR("MALLOC failed");
		return -1;
	}
	strcpy(tempURL, URL);
    tempURL[strlen("https://")] = '\0'; // https:// part of the string

    for (int i = 0; tempURL[i] && i < 10; i++)
    {
        tempURL[i] = (char)tolower((unsigned char)tempURL[i]); // lowercase. Sanatise the string
    }

    if (strcmp(tempURL, "https://") != 0)
    {
        printf("ERROR %s is not supported yet\n\n", tempURL);
		free(tempURL);
        return -1;
    }

    // HTTPS OK, now seperate domain and path
    strcpy(tempURL, &URL[8]); // get rid of HTTPS:// part

    bool foundPath = false;

    // we should now be left with "domain.com/path"
    for (int i = 0; tempURL[i]; i++)
    {
        if (tempURL[i] == '/')
        {
            strcpy(path, &tempURL[i]); // copy the path to path
            tempURL[i] = '\0';         // get just the domain
            foundPath = true;
            break;
        }
    }

    if (!foundPath)
    { // ensure path exists
        path[0] = '/';
        path[1] = '\0';
    }

    strcpy(domain, tempURL);

	free(tempURL);

	// return -1; //DELETE ME!!!

    return 0; // success
}


