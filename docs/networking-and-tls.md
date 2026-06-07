# Networking And TLS

Network downloads use three layers:

- `downloadFileHTTPS()` owns app-level HTTPS request/response flow.
- `dns.cpp` resolves hostnames through XNet.
- `XboxTLS.cpp` wraps BearSSL TLS 1.2 over Xbox sockets.

## URL Parsing

`parseURL()` in `parsing.cpp` supports only HTTPS URLs.

It checks that the URL begins with `https://`, case-normalized for the prefix only. It then splits the rest into:

- `domain`: bytes before the first `/`,
- `path`: from the first `/` onward, or `/` if no path exists.

Unsupported schemes fail before any network stack setup.

## Network Stack Setup

Each `downloadFileHTTPS()` call starts XNet and Winsock:

- `XNetStartup()` with `XNET_STARTUP_BYPASS_SECURITY`
- `WSAStartup(MAKEWORD(2, 2), ...)`

The stack is cleaned up at the end of the request or on failure.

DNS retries may also clean up and restart XNet/Winsock between attempts.

## DNS Resolution

`ResolveDNS()` wraps `XNetDnsLookup()`.

It is defensive because XNet DNS can fail or fault if the network stack is not ready. The lookup and release calls are guarded with Xbox structured exception handling.

Behavior:

- rejects null/empty output buffers,
- starts `XNetDnsLookup(domain, NULL, &pxndns)`,
- waits up to about 5 seconds in 100 ms increments while `iStatus == WSAEINPROGRESS`,
- if resolution succeeds and at least one address exists, converts the first address with `XNetInAddrToString()`,
- always attempts `XNetDnsRelease()` when there is a DNS object.

`downloadFileHTTPS()` tries DNS up to 5 times. If normal DNS fails, it calls `searchDnsCache()`.

## DNS Cache

`dns.cpp` contains a small static `dnsCache[]` table. At the moment it contains only a placeholder:

```text
example.com -> 0.0.0.0
```

The cache function copies an exact domain match into the caller's IP buffer and returns `0`. Otherwise it returns `-1`.

The table is useful if a known service has DNS problems on Xbox 360 and a stable IPv4 fallback is acceptable.

## XboxTLS Public API

`XboxTLS.h` defines the public API:

- `XboxTLS_CreateContext(ctx, hostname)`
- `XboxTLS_AddTrustAnchor_RSA(ctx, dn, dn_len, n, n_len, e, e_len)`
- `XboxTLS_AddTrustAnchor_EC(ctx, dn, dn_len, q, q_len, curve_id)`
- `XboxTLS_Connect(ctx, ip, hostname, port)`
- `XboxTLS_Write(ctx, buf, len)`
- `XboxTLS_Read(ctx, buf, len)`
- `XboxTLS_Free(ctx)`

The caller creates an `XboxTLSContext` on the stack. The internal BearSSL state is heap-allocated and hidden behind `ctx->internal`.

## Entropy

BearSSL expects a PRNG seeder named `br_prng_seeder_system()`. This project supplies one backed by `XeCryptRandom()`.

The seeder fills a 32-byte seed, initializes a global BearSSL HMAC-DRBG using SHA-256, and returns that PRNG to BearSSL.

## Trust Anchors

The Xbox 360 does not provide a normal desktop-style certificate store for this app to use. Trust anchors are embedded manually.

`downloadFile.cpp` contains raw DN/public key arrays for several roots/intermediates, including Google/Let's Encrypt/GitHub-related anchors and GoDaddy/Internet Archive data through included headers.

`addTrustAnchors()`:

- sets `ctx->hashAlgo = XboxTLS_Hash_SHA384`,
- adds EC trust anchors with `XboxTLS_AddTrustAnchor_EC()`,
- adds RSA trust anchors with `XboxTLS_AddTrustAnchor_RSA()`.

Each added anchor copies the DN and public key bytes into memory owned by the TLS context. `XboxTLS_Free()` later frees those allocations.

## Socket Setup

`XboxTLS_Connect()` creates a TCP socket and sets Xbox-specific options:

- `XBOX_SO_BYPASS_SECURITY`
- large receive buffer (`4 MB`)
- `TCP_NODELAY`

It connects to the resolved IPv4 address and requested port, normally 443.

## BearSSL Setup

After TCP connect:

1. `XboxTLS_GetHashVTable()` maps `ctx->hashAlgo` to a BearSSL hash vtable.
2. `br_x509_minimal_init()` initializes certificate validation with the loaded anchors.
3. RSA and ECDSA verification functions are installed.
4. `br_ssl_client_init_full()` initializes the client.
5. `XboxTLS_UseFastDownloadSuites()` pins TLS to TLS 1.2 and supplies a small cipher-suite list.
6. `br_ssl_engine_set_buffer()` installs the bidirectional TLS buffer.
7. `br_ssl_client_reset()` sets the hostname for SNI/certificate validation.
8. `br_sslio_init()` wires BearSSL reads/writes to the socket functions.

The selected cipher suites prefer ChaCha20-Poly1305 and AES-128-GCM, with an AES-128-CBC fallback.

## TLS Reads And Writes

`XboxTLS_Write()` uses `br_sslio_write_all()` followed by `br_sslio_flush()`. It returns the plaintext byte count on success or `-1` on error.

`XboxTLS_Read()` first checks if BearSSL already has received application data available in `br_ssl_engine_recvapp_buf()`. If so, it copies from that internal buffer and acknowledges the bytes. Otherwise it calls `br_sslio_read()`.

On BearSSL errors, TLS error codes are written through `debug_tls()`.

## Cleanup

`XboxTLS_Free()` closes the socket, frees every trust anchor allocation, frees the internal struct, and clears `ctx->internal`.

It is safe to call after partial setup because it checks for null context/internal pointers.

## HTTPS Request Shape

`downloadFileHTTPS()` sends a plain HTTP/1.1 GET over TLS:

- `Host`
- desktop-like `User-Agent`
- `Accept: */*`
- `Accept-Encoding: identity, *;q=0`
- `Connection: close`
- `referer: https://vimm.net/`

The `Accept-Encoding` line asks servers not to compress the response body with gzip/br/etc. That keeps response handling simple and streamable.
