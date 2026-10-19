#ifndef DNS_H
#define DNS_H

bool ResolveDNS(const char *domain, char *resolvedIP, int ipBufferSize);

int searchDnsCache(const char *domain, char *ip, int ipSize);
void writeDnsCache(const char *domain, const char *ip);
void clearDnsCache();

#endif
