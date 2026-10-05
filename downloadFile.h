#ifndef DOWNLOAD_FILE_H
#define DOWNLOAD_FILE_H

#include <string>
#include <map>

struct HttpResponseInfo {
    int status_code;                      // e.g., 200, 201, 400; 0 before a response
    std::string body;                     // The actual data returned by the server
    std::map<std::string, std::string> headers; // Response headers with lowercase names
    std::string error_message;            // Empty unless a network/protocol error occurred

    HttpResponseInfo() : status_code(0) {}
};

// HTTPS POST using the existing XboxTLS trust anchors and blocking I/O.
// Host, Content-Length and Connection are generated; redirects are returned as-is.
HttpResponseInfo HTTP_POST(
    const std::string& url,
    const std::map<std::string, std::string>& headers = std::map<std::string, std::string>(),
    const std::string& body = std::string()
);

HttpResponseInfo HTTP_GET(
    const std::string& url,
    const std::map<std::string, std::string>& headers = std::map<std::string, std::string>()
);

/// @brief downloads file over https using http 1.1 and tls 1.2
int downloadFileHTTPS(const std::string URL, const std::string fileName, char *dataBuffer, unsigned long long *outputBufferSize, bool downloadIntoFile, void printFunction(const char *_format, ...),
                        std::map<std::string, std::string> headers = std::map<std::string, std::string>());

#endif
