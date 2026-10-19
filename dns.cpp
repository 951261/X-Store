/*
FILE : dns.cpp
PROJECT : xstore
PROGRAMMER : 951261
DESCRIPTION : Resolves a domain name (example.com) to an IP (384.528.845.259)
*/

#include <xtl.h>
#include "XboxTLS.h"

#include <string.h>
#include <string>
#include <iostream>
#include <map>

#include "dns.h"

#include "OutputConsole.h"

// can cache different domains
std::map<std::string, std::string> dnsCache;

/**
 * @brief Resolves a domain name to an IPv4 address string.
 *
 * @param domain The hostname to resolve (e.g., "example.com").
 * @param resolvedIP Output buffer to store the resolved IP address.
 * @param ipBufferSize Size of the resolvedIP buffer.
 * @return true if resolution succeeded, false otherwise.
 */
bool ResolveDNS(const char *domain, char *resolvedIP, int ipBufferSize)
{
    XNDNS *pxndns = NULL;
    int lookupResult;
    int i;
    bool success = false;

    if (domain == NULL || resolvedIP == NULL || ipBufferSize <= 0)
        return false;

    resolvedIP[0] = '\0';

    // XNetDnsLookup or the returned XNDNS block can fault if the network stack
    // is not ready yet, so keep the whole interaction guarded and fail cleanly.
    __try
    {
        lookupResult = XNetDnsLookup(domain, NULL, &pxndns);
        if (lookupResult == 0 && pxndns != NULL)
        {
            // Wait up to ~5 seconds for DNS resolution.
            for (i = 0; i < 50; ++i)
            {
                if (pxndns->iStatus != WSAEINPROGRESS)
                    break;

                Sleep(100);
            }

            if (pxndns->iStatus == 0 && pxndns->cina > 0)
            {
                XNetInAddrToString(pxndns->aina[0], resolvedIP, ipBufferSize);
                success = (resolvedIP[0] != '\0');
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (pxndns != NULL)
        {
            __try
            {
                XNetDnsRelease(pxndns);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        resolvedIP[0] = '\0';
        return false;
    }

    if (pxndns != NULL)
    {
        __try
        {
            XNetDnsRelease(pxndns);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    return success;
}

int searchDnsCache(const char *domain, char *ip, int ipSize)
{
    int i;

    if (domain == NULL || ip == NULL || ipSize <= 0)
        return -1;

    if (dnsCache.count(domain) <= 0) { // empty cache
        return -1;
    }

    const std::string ipString = dnsCache[domain];

    if(ipString.length() >= ipSize) {
        log_printf("ip buffer not large enough\n");
        return -1;
    }

    strcpy(ip, ipString.c_str());

    return 0; // failed to find DNS cache record
}

void writeDnsCache(const char *domain, const char *ip) {
    dnsCache[domain] = ip;
}

void clearDnsCache() {
    dnsCache.clear();
}
