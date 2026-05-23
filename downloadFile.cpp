/*
FILE : downloadFile.cpp
PROJECT : xstore
PROGRAMMER : 951261
DESCRIPTION : Downloads a file. If the file is larger than 4GB, it splits it into multiple parts. 
*/

/**
 * ----------------------------------------------------------------------------
 * XboxTLS Example Client
 * ----------------------------------------------------------------------------
 * This is a minimal demonstration of the XboxTLS library running on a
 * modded Xbox 360. It showcases how to perform a secure TLS 1.2 connection
 * to a remote server using the BearSSL cryptographic library (embedded into
 * the XboxTLS implementation).
 *
 * ----------------------------------------------------------------------------
 * Features:
 * - Initializes the Xbox 360 network stack (XNet, Winsock).
 * - Resolves a domain name using Xbox 360's DNS (XNetDnsLookup).
 * - Supports TLS 1.2 with both RSA and Elliptic Curve (EC) trust anchors.
 * - Performs a secure TLS handshake and sends an HTTPS GET(POST also supported) request.
 * - Prints the received HTTP response using XboxTLS_Read and debug output.
 *
 * ----------------------------------------------------------------------------
 * Library Dependencies:
 * - XboxTLS (this project's core TLS wrapper).
 * - BearSSL (used internally for TLS 1.2, x.509, ECDSA/RSA, HMAC, etc.).
 *
 * ----------------------------------------------------------------------------
 * Trust Anchor Notes:
 * This client supports two types of root certificate trust anchors:
 *   1. EC (Elliptic Curve) trust anchors - commonly used with modern CAs.
 *   2. RSA trust anchors - still widely used for backwards compatibility.
 *
 * The example provides pre-filled trust anchor values from:
 *   - Google Trust Services GTS Root R4 (EC P-384)
 *   - ISRG Root X1 (RSA 2048 / 65537)
 *
 * ----------------------------------------------------------------------------
 * Usage Notes:
 * - This client is designed for development/testing on modded Xbox 360 units.
 * - Built using the Xbox 360 XDK with support for WinSock and XNet.
 * - This implementation assumes you are targeting TLS 1.2 endpoints.
 *   It does not support TLS 1.3.
 *
 * ----------------------------------------------------------------------------
 * Author: Jakob Rangel
 * License: MIT / Open Source
 */
#include <iostream>

#include "XboxTLS.h"
#include "dns.h"
#include "parsing.h"

#include <xtl.h>
#include "goDaddyRootCA.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ERROR(s) (printf("\nERROR: %s\n", s))
#define DEBUG_PRINT(s) (printf(s));

unsigned long long outputBufferPointer = 0;

static const unsigned char EC_DN[] = { // GTS ROOT G4 Cert
    0x30, 0x47, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
    0x02, 0x55, 0x53, 0x31, 0x22, 0x30, 0x20, 0x06, 0x03, 0x55, 0x04, 0x0A,
    0x13, 0x19, 0x47, 0x6F, 0x6F, 0x67, 0x6C, 0x65, 0x20, 0x54, 0x72, 0x75,
    0x73, 0x74, 0x20, 0x53, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x73, 0x20,
    0x4C, 0x4C, 0x43, 0x31, 0x14, 0x30, 0x12, 0x06, 0x03, 0x55, 0x04, 0x03,
    0x13, 0x0B, 0x47, 0x54, 0x53, 0x20, 0x52, 0x6F, 0x6F, 0x74, 0x20, 0x52,
    0x34};

static const unsigned char EC_Q[] = { // GTS ROOT G4 Cert
    0x04, 0xF3, 0x74, 0x73, 0xA7, 0x68, 0x8B, 0x60, 0xAE, 0x43, 0xB8, 0x35,
    0xC5, 0x81, 0x30, 0x7B, 0x4B, 0x49, 0x9D, 0xFB, 0xC1, 0x61, 0xCE, 0xE6,
    0xDE, 0x46, 0xBD, 0x6B, 0xD5, 0x61, 0x18, 0x35, 0xAE, 0x40, 0xDD, 0x73,
    0xF7, 0x89, 0x91, 0x30, 0x5A, 0xEB, 0x3C, 0xEE, 0x85, 0x7C, 0xA2, 0x40,
    0x76, 0x3B, 0xA9, 0xC6, 0xB8, 0x47, 0xD8, 0x2A, 0xE7, 0x92, 0x91, 0x6A,
    0x73, 0xE9, 0xB1, 0x72, 0x39, 0x9F, 0x29, 0x9F, 0xA2, 0x98, 0xD3, 0x5F,
    0x5E, 0x58, 0x86, 0x65, 0x0F, 0xA1, 0x84, 0x65, 0x06, 0xD1, 0xDC, 0x8B,
    0xC9, 0xC7, 0x73, 0xC8, 0x8C, 0x6A, 0x2F, 0xE5, 0xC4, 0xAB, 0xD1, 0x1D,
    0x8A};

static const unsigned char TA0_DN[] = {
    0x30, 0x32, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
    0x02, 0x55, 0x53, 0x31, 0x16, 0x30, 0x14, 0x06, 0x03, 0x55, 0x04, 0x0A,
    0x13, 0x0D, 0x4C, 0x65, 0x74, 0x27, 0x73, 0x20, 0x45, 0x6E, 0x63, 0x72,
    0x79, 0x70, 0x74, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x03,
    0x13, 0x02, 0x45, 0x37};

static const unsigned char TA0_EC_Q[] = {
    0x04, 0x41, 0xE8, 0x04, 0x93, 0x08, 0x58, 0x7F, 0xBE, 0x37, 0x30, 0x0C,
    0xC0, 0xA0, 0x41, 0xEA, 0xFE, 0x56, 0xDA, 0x84, 0x93, 0x3E, 0xC9, 0x00,
    0xDB, 0xAB, 0x67, 0x12, 0xCF, 0xF9, 0x4F, 0x53, 0x09, 0xE8, 0xA8, 0x2F,
    0xAB, 0x29, 0xE5, 0x9F, 0x15, 0x46, 0xF4, 0x5B, 0x62, 0x4E, 0x0F, 0xD4,
    0x83, 0x41, 0x99, 0xB7, 0x9F, 0x40, 0x72, 0x45, 0x1C, 0x2C, 0x5C, 0x4A,
    0x32, 0xA6, 0xC2, 0xDB, 0xC6, 0x05, 0x6A, 0x65, 0xFF, 0xDA, 0xDA, 0xF0,
    0x75, 0xB4, 0x40, 0x3B, 0x14, 0x68, 0x95, 0xB6, 0xA8, 0xE2, 0x6A, 0x71,
    0xE2, 0x74, 0x65, 0x51, 0x53, 0xDE, 0x16, 0xD4, 0x1E, 0x27, 0xC1, 0x33,
    0xFD};

const unsigned char RSA_DN[] = {
    0x30, 0x31, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03,
    0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31,
    0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x0A,
    0x13, 0x0A, 0x49, 0x53, 0x52, 0x47, 0x20, 0x2C,
    0x49, 0x6E, 0x63, 0x2E, 0x31, 0x13, 0x30, 0x11,
    0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x0A, 0x49,
    0x53, 0x52, 0x47, 0x20, 0x52, 0x6F, 0x6F, 0x74,
    0x20, 0x58, 0x31};
const unsigned char RSA_N[] = {
    0x00, 0xaf, 0x2f, 0x62, 0xe9, 0xf5, 0x3d, 0x1f, 0x64, 0x2e, 0x98, 0x0f, 0x09, 0x3a, 0x65, 0x9b,
    0xf5, 0x77, 0x6f, 0x47, 0xdc, 0x96, 0xf9, 0x4e, 0x58, 0x91, 0x1f, 0x94, 0xb6, 0x1b, 0x7f, 0x7d,
    0x25, 0xa4, 0x0c, 0xc2, 0x55, 0x43, 0xd6, 0x62, 0xe3, 0xf3, 0x82, 0xc5, 0x0b, 0x12, 0x4d, 0xb0,
    0x0e, 0xb3, 0x4c, 0x4e, 0xf0, 0xac, 0x6a, 0x26, 0x4e, 0xd3, 0x93, 0xf4, 0x39, 0xd2, 0xc8, 0x2c,
    0x3b, 0xc6, 0x0a, 0xc7, 0x57, 0x18, 0x6c, 0xd1, 0x60, 0x60, 0x87, 0xd8, 0xac, 0x00, 0x11, 0x5d,
    0xb3, 0x69, 0x6a, 0x25, 0x80, 0xa5, 0x6f, 0x84, 0x2c, 0x1b, 0x33, 0x61, 0x4a, 0xe7, 0xd1, 0x8d,
    0x1f, 0xa2, 0xb0, 0x0d, 0x2d, 0xea, 0xbb, 0x0e, 0x5f, 0xe2, 0x7f, 0xa5, 0x80, 0xd2, 0x5f, 0xb7,
    0x25, 0x34, 0xb0, 0x4e, 0x76, 0x9e, 0x2c, 0x83, 0x25, 0xb2, 0x3e, 0x33, 0xe7, 0x2d, 0x5e, 0x45,
    0x93, 0xa4, 0xb2, 0x2b, 0x73, 0x1a, 0x6c, 0xf4, 0x30, 0x95, 0x28, 0x3b, 0x6b, 0xa3, 0x75, 0x4d,
    0x38, 0xbe, 0x7a, 0x11, 0x3c, 0xdf, 0x71, 0x33, 0x4f, 0x0e, 0x9e, 0x6d, 0xe5, 0xa6, 0x76, 0x7e,
    0x3e, 0xf6, 0xf4, 0x91, 0x8a, 0xbe, 0x3d, 0xf4, 0x11, 0xc4, 0x91, 0x0a, 0xe3, 0x5c, 0x2f, 0xbe,
    0x2e, 0x27, 0x3e, 0x61, 0x61, 0xb4, 0x12, 0xfa, 0xb9, 0xd4, 0x26, 0x44, 0xbd, 0x1a, 0xd3, 0x12,
    0x68, 0x96, 0xa2, 0x92, 0x7a, 0x8b, 0x86, 0x4d, 0x12, 0x29, 0xa1, 0x77, 0x53, 0x4a, 0x9a, 0x35,
    0xe2, 0xa1, 0x56, 0x45, 0xc5, 0xf3, 0xd7, 0x70, 0xd7, 0x91, 0x9f, 0x8c, 0x1b, 0xdf, 0x1c, 0x0b,
    0xb1, 0x3d, 0xa7, 0xf2, 0xbb, 0xd9, 0x6b, 0x75, 0x8d, 0x2d, 0x7b, 0xc7, 0x19, 0x5b, 0x9f, 0x32,
    0xbc, 0x3a, 0x1a, 0xd5, 0xa3, 0x93, 0xb3, 0xf9, 0x75, 0x26, 0x2e, 0x67, 0xf2, 0x77, 0x93, 0x41};
const unsigned char RSA_E[] = {0x01, 0x00, 0x01}; // 65537

static const unsigned char TA0_RSA_DN[] = {
    0x30, 0x33, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
    0x02, 0x55, 0x53, 0x31, 0x16, 0x30, 0x14, 0x06, 0x03, 0x55, 0x04, 0x0A,
    0x13, 0x0D, 0x4C, 0x65, 0x74, 0x27, 0x73, 0x20, 0x45, 0x6E, 0x63, 0x72,
    0x79, 0x70, 0x74, 0x31, 0x0C, 0x30, 0x0A, 0x06, 0x03, 0x55, 0x04, 0x03,
    0x13, 0x03, 0x52, 0x31, 0x32};

static const unsigned char TA0_RSA_N[] = {
    0xDA, 0x98, 0x28, 0x74, 0xAD, 0xBE, 0x94, 0xFE, 0x3B, 0xE0, 0x1E, 0xE2,
    0xE5, 0x4B, 0x75, 0xAB, 0x2C, 0x12, 0x7F, 0xED, 0xA7, 0x03, 0x32, 0x7E,
    0x36, 0x97, 0xEC, 0xE8, 0x31, 0x8F, 0xA5, 0x13, 0x8D, 0x0B, 0x99, 0x2E,
    0x1E, 0xCD, 0x01, 0x51, 0x3D, 0x4C, 0xE5, 0x28, 0x6E, 0x09, 0x55, 0x31,
    0xAA, 0xA5, 0x22, 0x5D, 0x72, 0xF4, 0x2D, 0x07, 0xC2, 0x4D, 0x40, 0x3C,
    0xDF, 0x01, 0x23, 0xB9, 0x78, 0x37, 0xF5, 0x1A, 0x65, 0x32, 0x34, 0xE6,
    0x86, 0x71, 0x9D, 0x04, 0xEF, 0x84, 0x08, 0x5B, 0xBD, 0x02, 0x1A, 0x99,
    0xEB, 0xA6, 0x01, 0x00, 0x9A, 0x73, 0x90, 0x6D, 0x8F, 0xA2, 0x07, 0xA0,
    0xD0, 0x97, 0xD3, 0xDA, 0x45, 0x61, 0x81, 0x35, 0x3D, 0x14, 0xF9, 0xC4,
    0xC0, 0x5F, 0x6A, 0xDC, 0x0B, 0x96, 0x1A, 0xB0, 0x9F, 0xE3, 0x2A, 0xEA,
    0xBD, 0x2A, 0xD6, 0x98, 0xC7, 0x9B, 0x71, 0xAB, 0x3B, 0x74, 0x0F, 0x3C,
    0xDB, 0xB2, 0x60, 0xBE, 0x5A, 0x4B, 0x4E, 0x18, 0xE9, 0xDB, 0x2A, 0x73,
    0x5C, 0x89, 0x61, 0x65, 0x9E, 0xFE, 0xED, 0x3C, 0xA6, 0xCB, 0x4E, 0x6F,
    0xE4, 0x9E, 0xF9, 0x00, 0x46, 0xB3, 0xFF, 0x19, 0x4D, 0x2A, 0x63, 0xB3,
    0x8E, 0x66, 0xC6, 0x18, 0x85, 0x70, 0xC7, 0x50, 0x65, 0x6F, 0x3B, 0x74,
    0xE5, 0x48, 0x83, 0x0F, 0x08, 0x58, 0x5D, 0x2D, 0x23, 0x9D, 0x5E, 0xA3,
    0xFE, 0xE8, 0xDB, 0x00, 0xA1, 0xD2, 0xF4, 0xE3, 0x19, 0x4D, 0xF2, 0xEE,
    0x7A, 0xF6, 0x27, 0x9E, 0xE5, 0xCD, 0x9C, 0x2D, 0xA2, 0xF2, 0x7F, 0x9C,
    0x17, 0xAD, 0xEF, 0x13, 0x37, 0x39, 0xD1, 0xB4, 0xC8, 0x2C, 0x41, 0xD6,
    0x86, 0xC0, 0xE9, 0xEC, 0x21, 0xF8, 0x59, 0x1B, 0x7F, 0xB9, 0x3A, 0x7C,
    0x9F, 0x5C, 0x01, 0x9D, 0x62, 0x04, 0xC2, 0x28, 0xBD, 0x0A, 0xAD, 0x3C,
    0xCA, 0x10, 0xEC, 0x1B};

static const unsigned char TA0_RSA_E[] = {
    0x01, 0x00, 0x01};

static bool WriteBody(FILE *file, const char *data, int len, unsigned long long *totalWritten, char *outputBuffer)
{
    if (len <= 0)
        return true;

    if (outputBuffer == NULL)
    {
        if (fwrite(data, 1, len, file) != (size_t)len)
            return false;
    }
    else
    {
        memcpy(outputBuffer + outputBufferPointer, data, len);
        outputBufferPointer += len;
    }

    *totalWritten += len;
    return true;
}

static bool WriteChunkedBody(ChunkedDecodeState *state, FILE *file, const char *data, int len, unsigned long long *totalWritten, char *outputBuffer)
{
    int pos = 0;

    while (pos < len && state->state != 3)
    {
        if (state->state == 0)
        {
            char c = data[pos++];

            if (c == '\r')
                continue;

            if (c == '\n')
            {
                unsigned long chunkSize;

                if (!ParseChunkSize(state->sizeLine, state->sizeLineLen, &chunkSize))
                    return false;

                state->sizeLineLen = 0;
                state->remaining = chunkSize;

                if (chunkSize == 0)
                    state->state = 3;
                else
                    state->state = 1;

                continue;
            }

            if (state->sizeLineLen >= (int)sizeof(state->sizeLine))
                return false;

            state->sizeLine[state->sizeLineLen++] = c;
        }
        else if (state->state == 1)
        {
            int bytesToWrite = (int)state->remaining;
            int available = len - pos;

            if (bytesToWrite > available)
                bytesToWrite = available;

            if (!WriteBody(file, data + pos, bytesToWrite, totalWritten, outputBuffer))
                return false;

            pos += bytesToWrite;
            state->remaining -= (unsigned long)bytesToWrite;

            if (state->remaining == 0)
                state->state = 2;
        }
        else if (state->state == 2)
        {
            char c = data[pos++];

            if (c == '\n')
                state->state = 0;
            else if (c != '\r')
                return false;
        }
    }

    return true;
}

unsigned long long parseContentLength(char *headerBuffer, size_t bufferLength)
{
    const char prevChar = headerBuffer[bufferLength - 1];
    headerBuffer[bufferLength - 1] = '\0';
    const char *contentLengthString = strstr(headerBuffer, "Content-Length: ");
    headerBuffer[bufferLength - 1] = prevChar;

    if (contentLengthString == NULL)
    { // no content length found in header
        return 0;
    }

    return _atoi64(contentLengthString + strlen("Content-Length: ")); // atoi should automatically stop when it sees the '\n' char
}

unsigned long long parseStatus(char *headerBuffer, size_t bufferLength)
{
    const char prevChar = headerBuffer[bufferLength - 1];
    headerBuffer[bufferLength - 1] = '\0';
    const char *statusString = strstr(headerBuffer, " ");
    headerBuffer[bufferLength - 1] = prevChar;

    if (statusString == NULL)
    { // no content length found in header
        return 0;
    }

    return _atoi64(statusString + strlen(" ")); // atoi should automatically stop when it sees the '\n' char
}

unsigned long long getClockLong()
{
    return (unsigned long long)clock();
}

// Function to check if a string ends with another string
bool endsWith(const std::string &fullString,
              const std::string &ending)
{
    // Check if the ending string is longer than the full
    // string
    if (ending.size() > fullString.size())
        return false;

    // Compare the ending of the full string with the target
    // ending
    return fullString.compare(fullString.size() - ending.size(),
                              ending.size(), ending) == 0;
}

int DumpResponse(XboxTLSContext *ctx, const std::string filename, char *outputBuffer, unsigned long long *outputBufferSize, void printFunction(const char *_format, ...))
{
    unsigned long long outputBufferSizeConst = 0;
    int responseCode = 0; // HTTP status code

    if (outputBuffer != NULL)
    {
        outputBufferSizeConst = *outputBufferSize;
    }

    outputBufferPointer = 0; // Whether writing to a buffer or not, it doesn't hurt to clear this

    const unsigned long long BUFFER_SIZE = 4 * 1024 * 1024;
    const unsigned long long MAX_FILE_SIZE = 0xF0000000;

    char *buffer = (char *)malloc(BUFFER_SIZE);

    if (buffer == NULL)
    {
        ERROR("MALLOC failed");
        return EXIT_FAILURE;
    }

    char *fileBuffer = (char *)malloc(BUFFER_SIZE);

    if (fileBuffer == NULL)
    {
        ERROR("MALLOC failed");
        free(buffer);
        return EXIT_FAILURE;
    }

    char headerBuffer[16 * 1024];
    unsigned long long headerLen = 0;
    bool headersDone = false;
    bool chunked = false;
    ChunkedDecodeState chunkedState;
    int r = 0;

    bool writeToBuffer = false; // should we write to a buffer instead of a file???
    bool splitFile = false;

    if (outputBuffer != NULL)
    {
        writeToBuffer = true;
    }

    FILE *file = NULL;
    if (!writeToBuffer)
    {
        if (endsWith(filename, std::string(".001")))
        {
            splitFile = true;
            std::cout << "Split file detected\n";
        }
        file = fopen(filename.c_str(), "wb");

        if (file == NULL)
        {
            printFunction("Failed to open file %s\n", filename.c_str());
            free(buffer);
            free(fileBuffer);
            return EXIT_FAILURE;
        }
        setvbuf(file, fileBuffer, _IOFBF, BUFFER_SIZE);
    }

    InitChunkedDecodeState(&chunkedState);

    unsigned long long startTime = (getClockLong() * (unsigned long long)1000) / (CLOCKS_PER_SEC);
    unsigned long long endTime = (getClockLong() * (unsigned long long)1000) / (CLOCKS_PER_SEC);

    unsigned long long totalWritten = 0;

    unsigned long long lastKBWritten = 0;
    unsigned long long KBdownloaded = 0;

    unsigned long long totalContentLength = 0;

    unsigned long long beginTime = startTime;

    unsigned long long bytesWrittenToCurrentFile = 0;

    char *splitFilename = NULL;

    if (!writeToBuffer)
    {
        splitFilename = strdup(filename.c_str());
    }

    while ((!headersDone || writeToBuffer) && (r = XboxTLS_Read(ctx, buffer, BUFFER_SIZE)) > 0)
    {
        const char *body = buffer;
        int bodyLen = r;

        if (writeToBuffer && totalWritten + r >= outputBufferSizeConst)
        {
            printFunction("Output Buffer Exhausted\n");
            goto failure;
        }

        if (!headersDone)
        {
            int copyLen = r;
            int headerEnd;

            if (copyLen > (int)sizeof(headerBuffer) - headerLen)
                copyLen = (int)sizeof(headerBuffer) - headerLen;

            memcpy(headerBuffer + headerLen, buffer, copyLen);
            headerLen += copyLen;

            headerEnd = FindHeaderEnd(headerBuffer, headerLen);
            if (headerEnd < 0)
            {
                if (headerLen >= (int)sizeof(headerBuffer))
                {
                    printFunction("HTTP headers too large\n");
                    goto failure;
                }

                continue;
            }

            const char tempChar = buffer[r - 1];
            buffer[r - 1] = '\0';
            // std::cout << buffer << "\n\n";
            buffer[r - 1] = tempChar;

            headersDone = true;
            chunked = HeaderContainsToken(headerBuffer, headerEnd, "Transfer-Encoding", "chunked");
            body = headerBuffer + headerEnd;
            bodyLen = headerLen - headerEnd;
            totalContentLength = parseContentLength(headerBuffer, headerEnd);

            int status = parseStatus(headerBuffer, headerEnd);
            responseCode = status; //value to return

            if (status != 200)
            {
                printFunction("Status: %d\n", status);

                switch (status)
                {
                case 429:
                    printFunction("Error: A download is already in progress.\nVimms Lair only allows one download at a time. Go to https://dl3.vimm.net/cancel.php to fix this error. \n");
                    break;
                case 404:
                    printFunction("Error: Page not found\n");
                    break;
                default:
                    break;
                }

                goto failure;
            }
        }

        if (chunked)
        {
            if (!WriteChunkedBody(&chunkedState, file, body, bodyLen, &totalWritten, outputBuffer))
            {
                printFunction("Failed to decode chunked HTTP body\n");
                goto failure;
            }
        }
        else if (!WriteBody(file, body, bodyLen, &totalWritten, outputBuffer))
        {
            printFunction("Failed to write to file\n");
            goto failure;
        }

        endTime = (getClockLong() * (unsigned long long)1000) / (CLOCKS_PER_SEC);

        if (endTime - startTime >= 1000)
        {
            int downloadSpeed = (int)((totalWritten * 1000) / (endTime - startTime));
            printFunction("Speed: %d Bytes/sec \n", downloadSpeed);
        }
    }

    // Check for chunked download (unlikely)
    if (chunked && splitFile)
    {
        printFunction("\nERROR: chunked split file not supported yet\n");

        if (file != NULL)
        {
            fclose(file);
        }
        free(buffer);
        free(fileBuffer);
        free(splitFilename);
        return EXIT_FAILURE;
    }

    while (chunked && (r = XboxTLS_Read(ctx, buffer, BUFFER_SIZE)) > 0) // Chunked download
    {
        const char *body = buffer;
        int bodyLen = r;

        if (chunked)
        {
            if (!WriteChunkedBody(&chunkedState, file, body, bodyLen, &totalWritten, outputBuffer))
            {
                printFunction("Failed to decode chunked HTTP body _2\n");
                break;
            }
        }

        endTime = (getClockLong() * (unsigned long long)1000) / (CLOCKS_PER_SEC);

        if (endTime - startTime >= 3000)
        {
            int downloadSpeed = (int)((totalWritten * 1000) / (endTime - startTime));
            printFunction("Speed: %d Bytes/sec \n", downloadSpeed);
        }
    }

    bytesWrittenToCurrentFile = totalWritten;

    if (!chunked) // When dowloading from Vimms lair, we request that the files is NOT chunked, so this loop 99% of the time should be what is used to download the majority of the file (except the header of course!!!)
    {
        while ((r = XboxTLS_Read(ctx, buffer, BUFFER_SIZE)) > 0)
        {
            if (r + bytesWrittenToCurrentFile > MAX_FILE_SIZE && splitFile) // As both XFat and FAT32 only support 4GB files, all downloaded files must be split (file.001, file.002, etc..). Up to 9 parts (.009) is supported
            {
                printFunction("File overflowed, creating next split file part. ");

                size_t bytesThisFile = (size_t)(MAX_FILE_SIZE - bytesWrittenToCurrentFile);
                size_t bytesNextFile = (size_t)r - bytesThisFile;

                if (bytesThisFile > 0 && fwrite(buffer, 1, bytesThisFile, file) != bytesThisFile)
                {
                    printFunction("Failed to write to file\n");
                    goto failure;
                }

                totalWritten += bytesThisFile;
                bytesWrittenToCurrentFile += bytesThisFile;

                fclose(file);

                if (!IncrementSplitFilename(splitFilename))
                {
                    printFunction("\nERROR: failed to increment split filename %s\n\n", splitFilename);
                    break;
                }

                file = fopen(splitFilename, "wb");

                if (file == NULL)
                {
                    printFunction("\nERROR: failed to open %s\n\n", splitFilename);
                    ERROR("Faile to open new split file part");
                    break;
                }
                setvbuf(file, fileBuffer, _IOFBF, BUFFER_SIZE);

                if (bytesNextFile > 0 && fwrite(buffer + bytesThisFile, 1, bytesNextFile, file) != bytesNextFile)
                {
                    printFunction("Failed to write to file\n");
                    break;
                }

                totalWritten += bytesNextFile;
                r = (int)bytesNextFile;
                bytesWrittenToCurrentFile = 0;
                bytesWrittenToCurrentFile += bytesNextFile;

                printFunction("Created new split file part");

                continue;
            }

            if (fwrite(buffer, sizeof(buffer[0]), r, file) < r)
            {
                printFunction("Failed to write to file\n");
                goto failure;
            }

            totalWritten += r;
            bytesWrittenToCurrentFile += r;

            endTime = (getClockLong() * (unsigned long long)1000) / (CLOCKS_PER_SEC);

            if (endTime - startTime >= 3000)
            {
                unsigned long long tempTotalWritten = totalWritten;
                if (tempTotalWritten / (endTime - startTime) == 0 || tempTotalWritten / (endTime - beginTime) == 0)
                {
                    std::cout << "Divide by 0 error found\n";
                    // tempTotalWritten++;
                    printFunction("Downloaded %llu KB out of %llu KB, time: %llu\n", tempTotalWritten / (unsigned long long)1024, totalContentLength / (unsigned long long)1024, (endTime - beginTime) / (unsigned long long)1000);

                    endTime = (getClockLong() * (unsigned long long)1000) / (CLOCKS_PER_SEC);
                    startTime = endTime;
                    lastKBWritten = tempTotalWritten / (1024);
                    continue;
                }
                else
                {

                    unsigned long long KBtotalSize = totalContentLength / 1024;
                    unsigned long long KBdownloaded = tempTotalWritten / (1024);
                    unsigned long long downloadSpeed = ((KBdownloaded - lastKBWritten) * 1000) / (endTime - startTime);
                    unsigned long long timeRemaining = (KBtotalSize - KBdownloaded) / (tempTotalWritten / (endTime - beginTime)); // KB / (KB/sec)
                    endTime = (getClockLong() * (unsigned long long)1000) / (CLOCKS_PER_SEC);
                    startTime = endTime;
                    lastKBWritten = KBdownloaded;
                    printFunction("Downloaded %llu KB out of %llu KB, Speed: %llu KB/s, Average: %llu KB/s, Time %llu s, Time Remaining %llu:%llu:%llu s\n",
                                  KBdownloaded, KBtotalSize, downloadSpeed, tempTotalWritten / (endTime - beginTime), (endTime - beginTime) / (unsigned long long)1000, timeRemaining / (unsigned long long)3600, (timeRemaining / (unsigned long long)60) % (unsigned long long)60, timeRemaining % (unsigned long long)60);
                }
            }
        }
    }

    if (outputBufferSize != NULL)
    {
        *outputBufferSize = totalWritten;
    }

    if (writeToBuffer)
    {
        outputBuffer[totalWritten] = '\0';
    }

    if (totalWritten != totalContentLength && totalContentLength != 0)
    {
        ERROR("Download has likely failed");
        goto failure;
    }

    if (file != NULL)
        fclose(file);

    if (splitFilename != NULL)
        free(splitFilename);

    if (buffer != NULL)
        free(buffer);

    if (fileBuffer != NULL)
        free(fileBuffer);

    std::cout << "Download Complete\n";

    return responseCode;

failure:

    if(responseCode == 200) {
        responseCode = -1;
    }

    if (file != NULL)
        fclose(file);

    printFunction("Download Failed\n");

    if (splitFilename != NULL)
        free(splitFilename);

    if (buffer != NULL)
        free(buffer);

    if (fileBuffer != NULL)
        free(fileBuffer);

    return responseCode;
}

int addTrustAnchors(XboxTLSContext *ctx) {
    // Set the hash algorithm to use for certificate validation (used by both EC and RSA trust anchors).
    // Note: SHA-384 is commonly used with modern certificates (e.g., GTS Root R4, ISRG Root X1),
    //       but this can be changed to SHA-256, SHA-512, etc., depending on the CA's signature.
    ctx->hashAlgo = XboxTLS_Hash_SHA384;

    // Step 3.1: Add EC trust anchor (if site uses this key type)
    if (!XboxTLS_AddTrustAnchor_EC(ctx, TA0_DN, sizeof(TA0_DN), TA0_EC_Q, sizeof(TA0_EC_Q), XboxTLS_Curve_secp384r1))
    {
        return EXIT_FAILURE;
    }

    if (!XboxTLS_AddTrustAnchor_EC(ctx, EC_DN, sizeof(EC_DN), EC_Q, sizeof(EC_Q), XboxTLS_Curve_secp384r1))
    {
        return EXIT_FAILURE;
    }

    if (!XboxTLS_AddTrustAnchor_RSA(ctx, RSA_DN, sizeof(RSA_DN), RSA_N, sizeof(RSA_N), RSA_E, sizeof(RSA_E)))
    {
        return EXIT_FAILURE;
    }

    if (!XboxTLS_AddTrustAnchor_RSA(ctx, TA0_RSA_DN, sizeof(TA0_RSA_DN), TA0_RSA_N, sizeof(TA0_RSA_N), TA0_RSA_E, sizeof(TA0_RSA_E)))
    {
        return EXIT_FAILURE;
    }

    if (!XboxTLS_AddTrustAnchor_RSA(ctx, IA_TA0_RSA_DN, sizeof(IA_TA0_RSA_DN), IA_TA0_RSA_N, sizeof(IA_TA0_RSA_N), IA_TA0_RSA_E, sizeof(IA_TA0_RSA_E)))
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int downloadFileHTTPS(const std::string URL, const std::string fileName, char *dataBuffer, unsigned long long *outputBufferSize, bool downloadIntoFile, void printFunction(const char *_format, ...))
{
    char *domain;
    char *path;

    int httpStatus = 0;

    if ((domain = (char *)malloc(URL.length() + 1)) == NULL)
    {
        ERROR("MALLOC failed");
        return false;
    }
    if ((path = (char *)malloc(URL.length() + 1)) == NULL)
    {
        ERROR("MALLOC failed");
        free(domain);
        return false;
    }

    if (parseURL(URL.c_str(), domain, path) != 0)
    {
        std::cout << "ERROR parsing URL\n\n";
        free(domain);
        free(path);
        return false;
    }

    // Step 1: Network stack setup
    XNetStartupParams xnsp = {0};
    xnsp.cfgSizeOfStruct = sizeof(xnsp);
    xnsp.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;
    if (XNetStartup(&xnsp) != 0)
    {
        ERROR("Couldn't initialize network stack");
        free(domain);
        free(path);
        return false;
    }

    WSADATA wsadata;
    if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
    {
        ERROR("Couldn't start WSA");
        XNetCleanup();
        free(domain);
        free(path);
        return false;
    }

    // Step 2: Create TLS context
    XboxTLSContext ctx;
    if (!XboxTLS_CreateContext(&ctx, domain))
    {
        ERROR("Couldn't create TLS context");
        XNetCleanup();
        WSACleanup();
        free(domain);
        free(path);
        return false;
    }

    if(addTrustAnchors(&ctx) != EXIT_SUCCESS) {
        ERROR("Couldn't add trust anchor");
        XboxTLS_Free(&ctx);
        WSACleanup();
        XNetCleanup();

        free(domain);
        free(path);
        return false;
    }

    // Step 4: DNS resolution
    printFunction("Resolving DNS\n");
    char ip[64] = {0};

    for (int i = 0; i < 5; i++) // try DNS 5 times until it works.
    {
        if (ip[0] != '\0')
        {
            break;
        }

        ip[0] = '\0';

        if (ResolveDNS(domain, ip, sizeof(ip)))
        {
            printFunction("Init: DNS resolved %s -> %s\n", domain, ip);
            break;
        }

        printFunction("Failed to resolve %s\n", domain);

        if (searchDnsCache(domain, ip, sizeof(ip)) == 0)
        {
            break; // found a cached DNS address.
        }

        WSACleanup();
        XNetCleanup();

        XNetStartup(&xnsp);
        WSAStartup(MAKEWORD(2, 2), &wsadata);
        Sleep(3000);
    }

    if (ip[0] == 0)
    {
        ERROR("Failed to resolve DNS");
        goto downloadFailed;
    }

    // Step 5: Connect + TLS handshake
    if (!XboxTLS_Connect(&ctx, ip, domain, 443))
    {
        goto downloadFailed;
    }

    // Step 6: Send GET request
    char request[1024];
    int requestLen;

    printFunction("Request: formatting\n");
    requestLen = _snprintf(request, sizeof(request),
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/147.0.0.0 Safari/537.36\r\n"
                           "Accept: */*\r\n"
                           "Accept-Encoding: identity, *;q=0\r\n"
                           "Connection: close\r\n"
                           "referer: https://vimm.net/\r\n\r\n",
                           path, domain);

    if (requestLen < 0 || requestLen >= (int)sizeof(request))
    {
        ERROR("HTTP request buffer too small");
        goto downloadFailed;
    }

    printFunction("Request: formatted %d bytes\n", requestLen);
    printFunction("Request: sending\n");
    if (XboxTLS_Write(&ctx, request, requestLen) < 0)
    {
        ERROR("Failed to send HTTPS request");
        goto downloadFailed;
    }
    printFunction("Request: send OK\n");

    // Step 7: Read and print response
    if (downloadIntoFile)
    {
        if ( (httpStatus = DumpResponse(&ctx, fileName, NULL, NULL, printFunction)) != 200)
        {
            ERROR("Failed to download data over HTTPS");
            goto downloadFailed;
        }
    } else {
        if ( (httpStatus = DumpResponse(&ctx, "", dataBuffer, outputBufferSize, printFunction)) != 200)
        {
            ERROR("Failed to download data over HTTPS");
            goto downloadFailed;
        }
    }

    // Step 8: Cleanup
    XboxTLS_Free(&ctx);
    WSACleanup();
    XNetCleanup();

    free(domain);
    free(path);

    return httpStatus;

downloadFailed:
    XboxTLS_Free(&ctx);
    WSACleanup();

    XNetCleanup();

    free(domain);
    free(path);

    return httpStatus;
}
