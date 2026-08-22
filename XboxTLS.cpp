/*
 ============================================================================
  XboxTLS - Lightweight TLS 1.2 Client for Xbox 360 using BearSSL
 ============================================================================
 
  Overview:
  ---------
  XboxTLS is a self-contained TLS 1.2 client implementation designed to work
  on retail or dev Xbox 360 consoles. It is built on top of BearSSL's minimal
  X.509 and SSL/TLS stack, providing secure HTTPS communication with support 
  for both RSA and EC (Elliptic Curve) trust anchors.

  The core features include:
    - Secure TLS 1.2 client using BearSSL
    - Minimal memory footprint, tuned for Xbox 360 constraints
    - Support for SHA-256, SHA-384, SHA-512, SHA-1, and SHA-224 hashes
    - X.509 certificate verification (minimal mode)
    - RSA and EC (P-256, P-384, etc.) trust anchor support
    - Works with real-world CA roots like ISRG Root X1, GTS Root R4, etc.

  Design Notes:
  -------------
  - This library is tailored for use in Xbox 360 homebrew apps and mod tools.
  - All entropy comes from XeCryptRandom (HMAC-DRBG from BearSSL).
  - Socket I/O is handled via Winsock 1.1 and raw `SOCKET` APIs.
  - Uses br_x509_minimal_context for certificate validation, not full path building.

  How to Use:
  -----------
    1. Call XboxTLS_CreateContext to initialize a TLS context.
    2. Add one or more trust anchors using XboxTLS_AddTrustAnchor_RSA or EC.
    3. Resolve the hostname and connect with XboxTLS_Connect.
    4. Use XboxTLS_Write / XboxTLS_Read to send/receive data.
    5. Call XboxTLS_Free to release memory and close the connection.
	Bonus: See ExampleClient.cpp for demonstration.
  License:
  --------
  MIT License (see LICENSE file)

  Dependencies:
  -------------
  - BearSSL (https://bearssl.org/)
  - XDK headers/libraries (e.g., xtl.h, winsockx.h)
  - Xbox 360-compatible compiler (Visual Studio 2010, Xenon SDK)
*/
#include "inc\bearssl.h"
#include <string.h>
#include <xtl.h>
#include <xboxmath.h>
#include <stdio.h>
#include <winsockx.h>
#include <winnt.h>
#include <xbox.h>
#include "Debug.h"
#include <xtl.h>
#include <stdint.h>
#include <limits.h>
#include "Debug.h"
#include "XboxTLS.h"
#define MAX_ANCHORS 255
// #define XBOXTLS_RECV_BUFFER_SIZE (128 * 1024)
#define XBOXTLS_RECV_BUFFER_SIZE (4 * 1024 * 1024)
// Xbox 360-specific socket option required for direct outbound sockets that do
// not tunnel through Microsoft's service stack.
#define XBOX_SO_BYPASS_SECURITY 0x5801
static br_hmac_drbg_context g_drbg;

extern "C" void XeCryptRandom(BYTE* pb, DWORD cb);


/*
 * Custom entropy seeder for XboxTLS.
 *
 * This function provides entropy to the BearSSL HMAC-DRBG (Deterministic Random Bit Generator)
 * using the Xbox 360's internal XeCryptRandom function. It seeds the global HMAC-DRBG instance
 * `g_drbg` with a 32-byte random seed and sets the context pointer for use in TLS operations.
 *
 * Returns:
 *   1 on success (as required by BearSSL API).
 */
static int XboxTLS_CustomSeeder(const br_prng_class **ctx) {
    unsigned char seed[32];
    XeCryptRandom(seed, sizeof(seed));
    br_hmac_drbg_init(&g_drbg, &br_sha256_vtable, seed, sizeof(seed));
    *ctx = g_drbg.vtable;
    return 1;
}
br_prng_seeder
br_prng_seeder_system(const char **name) {
    if (name) *name = "XboxTLS/XeCryptRandom";
    return &XboxTLS_CustomSeeder;
}




/*
 * Internal structure representing the state of an XboxTLS connection.
 * 
 * This struct encapsulates all the necessary BearSSL components and runtime
 * information for a client-side TLS session on Xbox 360, including:
 *   - TLS client context (sc)
 *   - Minimal X.509 certificate validation context (xc)
 *   - I/O wrapper context for socket reads/writes (ioc)
 *   - Underlying socket descriptor (sock)
 *   - Array of trust anchors (certificates) for X.509 validation (anchors)
 *   - Number of valid anchors loaded (anchor_count)
 *   - Bi-directional I/O buffer (iobuf)
 */
struct XboxTLSInternal {
    br_ssl_client_context sc;                    // BearSSL TLS client context
    br_x509_minimal_context xc;                  // X.509 cert validation context (minimal)
    br_sslio_context ioc;                        // I/O context for read/write abstraction
    SOCKET sock;                                 // Underlying socket handle
    br_x509_trust_anchor anchors[MAX_ANCHORS];   // Trust anchor certificates
    int anchor_count;                            // Count of loaded anchors
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];    // TLS I/O buffer
};


/*
 * Returns the BearSSL hash function vtable for a given XboxTLS hash enum.
 * 
 * This function is used to select the appropriate hash algorithm for HMAC-DRBG
 * or signature verification. It maps internal XboxTLSHash enum values to the
 * corresponding BearSSL `br_hash_class` implementations.
 *
 * Parameters:
 *   - hash: An XboxTLSHash enum value representing the desired hash algorithm.
 *
 * Returns:
 *   A pointer to the BearSSL hash vtable (br_hash_class), or NULL if unsupported.
 */
const br_hash_class* XboxTLS_GetHashVTable(XboxTLSHash hash) {
    switch (hash) {
        case XboxTLS_Hash_SHA256: return &br_sha256_vtable;
        case XboxTLS_Hash_SHA384: return &br_sha384_vtable;
        case XboxTLS_Hash_SHA512: return &br_sha512_vtable;
        case XboxTLS_Hash_SHA1:   return &br_sha1_vtable;
        case XboxTLS_Hash_SHA224: return &br_sha224_vtable;
        default: return NULL;
    }
}

/*
 * Low-level read function for BearSSL's I/O abstraction layer.
 *
 * This function is passed to BearSSL to perform socket reads.
 * It reads up to `len` bytes into `buf` from the underlying socket.
 *
 * Parameters:
 *   - ctx: Pointer to the socket (cast to void* by BearSSL)
 *   - buf: Buffer to fill with received data
 *   - len: Maximum number of bytes to read
 *
 * Returns:
 *   Number of bytes read, or a negative value on error.
 */
static int tls_socket_read(void* ctx, unsigned char* buf, size_t len) {
    SOCKET s = *(SOCKET*)ctx;
    int want = (len > (size_t)INT_MAX) ? INT_MAX : (int)len;
    int r = recv(s, (char*)buf, want, 0);

    if (r <= 0) {
        return -1;
    }

    return r;
}

/*
 * Low-level write function for BearSSL's I/O abstraction layer.
 *
 * This function is passed to BearSSL to perform socket writes.
 * It sends `len` bytes from `buf` through the underlying socket.
 *
 * Parameters:
 *   - ctx: Pointer to the socket (cast to void* by BearSSL)
 *   - buf: Buffer containing data to send
 *   - len: Number of bytes to send
 *
 * Returns:
 *   Number of bytes written, or a negative value on error.
 */
static int tls_socket_write(void* ctx, const unsigned char* buf, size_t len) {
    SOCKET s = *(SOCKET*)ctx;
    return send(s, (const char*)buf, (int)len, 0);
}

static void XboxTLS_SetSocketOptions(SOCKET sock) {
    BOOL opt_true = TRUE;
    int recvBufferSize = XBOXTLS_RECV_BUFFER_SIZE;

    setsockopt(sock, SOL_SOCKET, XBOX_SO_BYPASS_SECURITY, (PCSTR)&opt_true, sizeof(opt_true));
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (PCSTR)&recvBufferSize, sizeof(recvBufferSize));
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (PCSTR)&opt_true, sizeof(opt_true));
}

static void XboxTLS_UseFastDownloadSuites(br_ssl_client_context* sc) {
    static const uint16_t suites[] = {
        BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
        BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_RSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_RSA_WITH_AES_128_CBC_SHA
    };

    br_ssl_engine_set_versions(&sc->eng, BR_TLS12, BR_TLS12);
    br_ssl_engine_set_suites(&sc->eng, suites, sizeof(suites) / sizeof(suites[0]));
}





/*
 * Initializes a new XboxTLSContext and allocates internal structures.
 *
 * This function prepares a new TLS context for use in a BearSSL-based
 * Xbox TLS connection. It allocates and zeroes an internal structure that
 * will be used to manage TLS state and certificate verification.
 *
 * Parameters:
 *   - ctx: Pointer to the XboxTLSContext to initialize
 *   - hostname: Server hostname for TLS SNI (not used at this stage)
 *
 * Returns:
 *   true if initialization succeeds, false on failure (e.g., OOM or bad input).
 */
bool XboxTLS_CreateContext(XboxTLSContext* ctx, const char* hostname) {
    if (!ctx || !hostname) return false;

    XboxTLSInternal* internal = (XboxTLSInternal*)malloc(sizeof(XboxTLSInternal));
    if (!internal) return false;
    memset(internal, 0, sizeof(XboxTLSInternal));

    ctx->internal = internal;
    return true;
}


/*
 * Adds an RSA trust anchor (root certificate public key) to the XboxTLS context.
 *
 * This function appends a new X.509 trust anchor based on a raw RSA public key.
 * It manually sets up the distinguished name (DN) and the RSA key parameters
 * (modulus and exponent), and stores them in the internal anchor list.
 *
 * Parameters:
 *   - ctx: Pointer to the XboxTLSContext
 *   - dn: Distinguished name (ASN.1-encoded)
 *   - dn_len: Length of the distinguished name
 *   - n: RSA modulus (big-endian)
 *   - n_len: Length of the modulus
 *   - e: RSA public exponent (big-endian)
 *   - e_len: Length of the exponent
 *
 * Returns:
 *   true on success, false on failure (e.g. out of space, invalid input)
 */
bool XboxTLS_AddTrustAnchor_RSA(XboxTLSContext* ctx,
    const unsigned char* dn, size_t dn_len,
    const unsigned char* n, size_t n_len,
    const unsigned char* e, size_t e_len) {

    if (!ctx || !ctx->internal || dn_len == 0 || n_len == 0 || e_len == 0) return false;
    XboxTLSInternal* ic = (XboxTLSInternal*)ctx->internal;
    if (ic->anchor_count >= MAX_ANCHORS) return false;

    br_x509_trust_anchor* ta = &ic->anchors[ic->anchor_count];
    br_rsa_public_key* key;

    memset(ta, 0, sizeof(*ta));

    ta->dn.data = (unsigned char*)malloc(dn_len);
    if (ta->dn.data == NULL) return false;
    memcpy((void*)ta->dn.data, dn, dn_len);
    ta->dn.len = dn_len;
    ta->flags = BR_X509_TA_CA;

    key = (br_rsa_public_key*)malloc(sizeof(br_rsa_public_key));
    if (key == NULL)
    {
        free((void*)ta->dn.data);
        ta->dn.data = NULL;
        ta->dn.len = 0;
        return false;
    }

    memset(key, 0, sizeof(*key));

    key->n = (unsigned char*)malloc(n_len);
    if (key->n == NULL)
    {
        free(key);
        free((void*)ta->dn.data);
        ta->dn.data = NULL;
        ta->dn.len = 0;
        return false;
    }
    memcpy(key->n, n, n_len);
    key->nlen = n_len;

    key->e = (unsigned char*)malloc(e_len);
    if (key->e == NULL)
    {
        free(key->n);
        free(key);
        free((void*)ta->dn.data);
        ta->dn.data = NULL;
        ta->dn.len = 0;
        return false;
    }
    memcpy(key->e, e, e_len);
    key->elen = e_len;

    ta->pkey.key_type = BR_KEYTYPE_RSA;
    ta->pkey.key.rsa = *key;
    free(key);
    ic->anchor_count++;

    return true;
}



/*
 * Adds an EC (Elliptic Curve) trust anchor to the XboxTLS context.
 *
 * This function appends a new X.509 trust anchor based on a raw EC public key.
 * It stores the distinguished name and curve parameters to be used for verifying
 * server certificates signed using EC keys (e.g. P-256, P-384).
 *
 * Parameters:
 *   - ctx: Pointer to the XboxTLSContext
 *   - dn: Distinguished name (ASN.1-encoded)
 *   - dn_len: Length of the distinguished name
 *   - q: Elliptic curve public point (uncompressed format)
 *   - q_len: Length of the EC point
 *   - curve_id: Identifier for the EC curve (e.g. BR_EC_secp256r1)
 *
 * Returns:
 *   true on success, false on failure (e.g. out of space, invalid input)
 */
bool XboxTLS_AddTrustAnchor_EC(XboxTLSContext* ctx,
    const unsigned char* dn, size_t dn_len,
    const unsigned char* q, size_t q_len,
    XboxTLSCurve curve_id) {

    if (!ctx || !ctx->internal || dn_len == 0 || q_len == 0) return false;
    XboxTLSInternal* ic = (XboxTLSInternal*)ctx->internal;
    if (ic->anchor_count >= MAX_ANCHORS) return false;

    br_x509_trust_anchor* ta = &ic->anchors[ic->anchor_count];
    br_ec_public_key* key;

    memset(ta, 0, sizeof(*ta));

    ta->dn.data = (unsigned char*)malloc(dn_len);
    if (ta->dn.data == NULL) return false;
    memcpy((void*)ta->dn.data, dn, dn_len);
    ta->dn.len = dn_len;
    ta->flags = BR_X509_TA_CA;

    key = (br_ec_public_key*)malloc(sizeof(br_ec_public_key));
    if (key == NULL)
    {
        free((void*)ta->dn.data);
        ta->dn.data = NULL;
        ta->dn.len = 0;
        return false;
    }

    memset(key, 0, sizeof(*key));

    key->q = (unsigned char*)malloc(q_len);
    if (key->q == NULL)
    {
        free(key);
        free((void*)ta->dn.data);
        ta->dn.data = NULL;
        ta->dn.len = 0;
        return false;
    }
    memcpy(key->q, q, q_len);
    key->qlen = q_len;
    key->curve = (int)curve_id;

    ta->pkey.key_type = BR_KEYTYPE_EC;
    ta->pkey.key.ec = *key;
    free(key);
    ic->anchor_count++;

    return true;
}


/*
 * Establishes a secure TLS connection using the provided XboxTLS context.
 *
 * This function creates a TCP socket to the given IP address and port, performs a TLS
 * handshake using BearSSL, and prepares the TLS engine to send and receive encrypted
 * data over the established socket. It also initializes the certificate verification
 * engine and configures the chosen hash algorithm and trust anchors.
 *
 * Parameters:
 *   - ctx: Pointer to the XboxTLSContext previously initialized by XboxTLS_CreateContext.
 *   - ip: Target server IP address (IPv4 string, e.g., "192.168.1.1").
 *   - hostname: Hostname for TLS SNI (Server Name Indication) and certificate validation.
 *   - port: TCP port to connect to (usually 443 for HTTPS).
 *
 * Returns:
 *   true on successful socket and TLS setup, false on error (e.g., invalid input,
 *   socket creation failure, connection error, invalid hash algorithm).
 *
 * Notes:
 *   - This function uses BearSSL's minimal X.509 engine.
 *   - Only one connection can be active per XboxTLSContext at a time.
 *   - Use XboxTLS_Write / XboxTLS_Read for I/O after a successful connection.
 */
bool XboxTLS_Connect(XboxTLSContext* ctx, const char* ip, const char* hostname, int port) {
    if (!ctx || !ctx->internal || !ip || !hostname) {
        debug_tls("XboxTLS_Connect: Invalid arguments.");
        return false;
    }

    XboxTLSInternal* ic = (XboxTLSInternal*)ctx->internal;
    SOCKET sock = INVALID_SOCKET;
    bool success = false;

    printf("Connect: socket(%s:%d)\n", ip, port);

    __try
    {
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            debug_tls("Socket creation failed.");
            int wsaErr = WSAGetLastError();
            char debugBuf[64];
            sprintf(debugBuf, "WSA Error: %d", wsaErr);
            debug_tls(debugBuf);
            return false;
        }
        printf("Connect: socket OK\n");

        XboxTLS_SetSocketOptions(sock);
        printf("Connect: socket options OK\n");

        sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        sa.sin_addr.s_addr = inet_addr(ip);
        if (sa.sin_addr.s_addr == INADDR_NONE) {
            debug_tls("inet_addr failed: Invalid IP format.");
            closesocket(sock);
            return false;
        }
        printf("Connect: inet_addr OK\n");

        if (connect(sock, (struct sockaddr*)&sa, sizeof(sa)) == SOCKET_ERROR) {
            int wsaErr = WSAGetLastError();
            char debugBuf[64];
            debug_tls("connect() failed.");
            sprintf(debugBuf, "WSA Error: %d", wsaErr);
            debug_tls(debugBuf);
            closesocket(sock);
            return false;
        }
        printf("Connect: TCP connect OK\n");

        ic->sock = sock;

        const br_hash_class* hash = XboxTLS_GetHashVTable(ctx->hashAlgo);
        if (!hash) {
            debug_tls("Invalid hash algorithm specified.");
            closesocket(sock);
            return false;
        }
        printf("Connect: hash OK\n");

        br_x509_minimal_init(&ic->xc, hash, ic->anchors, ic->anchor_count);
        br_x509_minimal_set_rsa(&ic->xc, br_rsa_pkcs1_vrfy_get_default());
        br_x509_minimal_set_ecdsa(&ic->xc, br_ec_get_default(), br_ecdsa_vrfy_asn1_get_default());
        printf("Connect: x509 init OK\n");

        br_ssl_client_init_full(&ic->sc, &ic->xc, ic->anchors, ic->anchor_count);
        XboxTLS_UseFastDownloadSuites(&ic->sc);
        br_ssl_engine_set_buffer(&ic->sc.eng, ic->iobuf, sizeof(ic->iobuf), 1);
        br_ssl_client_reset(&ic->sc, hostname, 0);
        br_sslio_init(&ic->ioc, &ic->sc.eng, tls_socket_read, &ic->sock, tls_socket_write, &ic->sock);
        printf("Connect: TLS init OK\n");

        success = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        debug_tls("XboxTLS_Connect: structured exception.");
        if (sock != INVALID_SOCKET)
            closesocket(sock);
        return false;
    }

    return success;
}



/*
 * Writes encrypted data over the active TLS connection.
 *
 * This function writes the provided plaintext buffer through BearSSL’s TLS engine,
 * which handles encryption and framing. It also flushes the write buffer to ensure
 * all data is transmitted immediately.
 *
 * Parameters:
 *   - ctx: Pointer to an active XboxTLSContext with a live TLS connection.
 *   - buf: Pointer to the data to send.
 *   - len: Length of the data in bytes.
 *
 * Returns:
 *   The number of bytes written on success, or -1 on failure.
 *
 * Notes:
 *   - Always flushes the stream after writing.
 *   - Uses BearSSL’s `br_sslio_write_all` and `br_sslio_flush`.
 */
int XboxTLS_Write(XboxTLSContext* ctx, const void* buf, int len) {
    if (!ctx || !ctx->internal || !buf || len <= 0) {
        debug_tls("XboxTLS_Write: Invalid arguments.");
        return -1;
    }

    XboxTLSInternal* ic = (XboxTLSInternal*)ctx->internal;
    int writeResult;

    printf("Write: br_sslio_write_all(%d)\n", len);
    __try
    {
        writeResult = br_sslio_write_all(&ic->ioc, buf, len);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        debug_tls("XboxTLS_Write: structured exception in write_all.");
        return -1;
    }

    if (ic->xc.err == BR_ERR_X509_NOT_TRUSTED) {
        debug_tls("WARNING: TLS certificate chain is not trusted; continuing anyway.");
    }

    if (writeResult < 0) {
        int err = br_ssl_engine_last_error(&ic->sc.eng);
        char msg[64];
        sprintf(msg, "TLS write error code: %d", err);
        debug_tls(msg);
        return -1;
    }
    printf("Write: br_sslio_write_all OK\n");

    printf("Write: br_sslio_flush\n");
    __try
    {
        writeResult = br_sslio_flush(&ic->ioc);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        debug_tls("XboxTLS_Write: structured exception in flush.");
        return -1;
    }

    if (writeResult != 0) {
        int err = br_ssl_engine_last_error(&ic->sc.eng);
        char msg[64];
        sprintf(msg, "TLS flush error code: %d", err);
        debug_tls(msg);
        return -1;
    }
    printf("Write: br_sslio_flush OK\n");

    return len;
}


/*
 * Reads decrypted data from the active TLS connection.
 *
 * This function receives data from the TLS socket and decrypts it using BearSSL.
 * It blocks until data is available or an error occurs.
 *
 * Parameters:
 *   - ctx: Pointer to an active XboxTLSContext with a live TLS connection.
 *   - buf: Buffer to store the received plaintext data.
 *   - len: Maximum number of bytes to read into the buffer.
 *
 * Returns:
 *   The number of bytes read on success, or -1 on error.
 */
int XboxTLS_Read(XboxTLSContext* ctx, void* buf, int len) {
    if (!ctx || !ctx->internal || !buf || len <= 0) {
        debug_tls("XboxTLS_Read: Invalid arguments.");
        return -1;
    }

    XboxTLSInternal* ic = (XboxTLSInternal*)ctx->internal;
    unsigned char* src;
    size_t available;

    src = br_ssl_engine_recvapp_buf(&ic->sc.eng, &available);
    if (src != NULL && available > 0) {
        size_t copyLen = (size_t)len;

        if (copyLen > available) {
            copyLen = available;
        }

        memcpy(buf, src, copyLen);
        br_ssl_engine_recvapp_ack(&ic->sc.eng, copyLen);
        return (int)copyLen;
    }

    int r = br_sslio_read(&ic->ioc, buf, (size_t)len);
    if (r < 0) {
        int err = br_ssl_engine_last_error(&ic->sc.eng);
        char msg[64];
        sprintf(msg, "TLS read error code: %d", err);
        debug_tls(msg);
    }

    return r;
}

/*
 * Frees all internal memory and closes the TLS connection.
 *
 * This function shuts down the TLS socket and releases any dynamic memory
 * used by the TLS context, including trust anchors and cryptographic structures.
 *
 * Parameters:
 *   - ctx: Pointer to the XboxTLSContext to be cleaned up.
 *
 * Notes:
 *   - Safe to call even if the context is partially initialized.
 *   - After calling, the context's internal pointer is set to NULL.
 */
void XboxTLS_Free(XboxTLSContext* ctx) {
    if (!ctx || !ctx->internal) return;
    XboxTLSInternal* ic = (XboxTLSInternal*)ctx->internal;

    closesocket(ic->sock);

    for (int i = 0; i < ic->anchor_count; ++i) {
        free((void*)ic->anchors[i].dn.data);
        if (ic->anchors[i].pkey.key_type == BR_KEYTYPE_RSA) {
            free(ic->anchors[i].pkey.key.rsa.n);
            free(ic->anchors[i].pkey.key.rsa.e);
        } else if (ic->anchors[i].pkey.key_type == BR_KEYTYPE_EC) {
            free(ic->anchors[i].pkey.key.ec.q);
        }
    }

    free(ic);
    ctx->internal = NULL;
}
