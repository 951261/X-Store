#include <xtl.h>
#include <xcompress.h>
#include <xgraphics.h>
#include <d3dx9.h>
#include <stdint.h>
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <vector>
#include <string>
#include <algorithm>
#include "iso2god.h"
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif
typedef void (*GodLogCallback)(const char *fmt, ...);
static const int OUT_MODE_GOD = 0;
static const int OUT_MODE_FOLDER = 1;
static const int OUT_MODE_XISO = 2;
std::vector<std::string> g_needSaveTitles;
extern IDirect3DDevice9 *g_pSharedD3DDevice;

extern "C" {
    typedef struct _XECRYPT_SHA_STATE {
        DWORD count;
        DWORD state[5];
        BYTE buffer[64];
    } XECRYPT_SHA_STATE, *PXECRYPT_SHA_STATE;

    VOID WINAPI XeCryptShaInit(PXECRYPT_SHA_STATE pShaState);
    VOID WINAPI XeCryptShaUpdate(PXECRYPT_SHA_STATE pShaState, const BYTE *pbInp, DWORD cbInp);
    VOID WINAPI XeCryptShaFinal(PXECRYPT_SHA_STATE pShaState, BYTE *pbOut, DWORD cbOut);
    typedef struct _XECRYPT_AES_STATE {
        BYTE keytabenc[11][4][4];
        BYTE keytabdec[11][4][4];
    } XECRYPT_AES_STATE, *PXECRYPT_AES_STATE;

    VOID WINAPI XeCryptAesKey(PXECRYPT_AES_STATE pAesState, const BYTE *pbKey);
    VOID WINAPI XeCryptAesEcb(PXECRYPT_AES_STATE pAesState, const BYTE *pbInp, BYTE *pbOut, BOOL fEncrypt);
    VOID WINAPI XeCryptAesCbc(PXECRYPT_AES_STATE pAesState, const BYTE *pbInp, DWORD cbInp, BYTE *pbOut, BYTE *pbFeed, BOOL fEncrypt);
}

#define SECTOR_SIZE 2048ULL
#define BLOCK_SIZE 4096ULL
#define BLOCKS_PER_SUBTABLE 204ULL
#define SUBTABLES_PER_PART 203ULL
#define DATA_BLOCKS_PER_PART (BLOCKS_PER_SUBTABLE * SUBTABLES_PER_PART)

static inline uint16_t Swap16(uint16_t v) { return _byteswap_ushort(v); }
static inline uint32_t Swap32(uint32_t v) { return _byteswap_ulong(v); }
static inline uint64_t Swap64(uint64_t v) { return _byteswap_uint64(v); }

static void ComputeSha1(const void *data, size_t len, uint8_t *outHash) {
    XECRYPT_SHA_STATE ctx;
    XeCryptShaInit(&ctx);
    XeCryptShaUpdate(&ctx, (const BYTE *)data, (DWORD)len);
    XeCryptShaFinal(&ctx, outHash, 20);
}

void ForceCreateDirectoryTree(const char *path) {
    if (!path || !*path) return;
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (char *p = tmp; *p; ++p) {
        if (*p == '/') *p = '\\';
        if (*p == '\\' && p > tmp && *(p - 1) != ':') {
            *p = '\0';
            CreateDirectoryA(tmp, NULL);
            *p = '\\';
        }
    }
    CreateDirectoryA(tmp, NULL);
}

bool DeletePathRecursive(const char *path) {
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return DeleteFileA(path) != FALSE;
    }
    std::string pattern = std::string(path) + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            std::string child = std::string(path) + "\\" + fd.cFileName;
            DeletePathRecursive(child.c_str());
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    return RemoveDirectoryA(path) != FALSE;
}

class DiscStream {
public:
    virtual ~DiscStream() {}
    virtual uint64_t TotalBytes() const = 0;
    virtual bool ReadBytes(uint64_t offset, void *buf, size_t len) = 0;
};

struct IsoSegment {
    HANDLE hFile;
    uint64_t startByte;
    uint64_t length;
};

class SplitIsoStream : public DiscStream {
    std::vector<IsoSegment> segs;
    uint64_t baseOffset;
    uint64_t totalDataSize;

    static bool GetFileSize64(HANDLE h, uint64_t &outSize) {
        LARGE_INTEGER li;
        if (!GetFileSizeEx(h, &li)) return false;
        outSize = (uint64_t)li.QuadPart;
        return true;
    }

public:
    SplitIsoStream() : baseOffset(0), totalDataSize(0) {}
    ~SplitIsoStream() {
        for (size_t i = 0; i < segs.size(); ++i) {
            if (segs[i].hFile != INVALID_HANDLE_VALUE) CloseHandle(segs[i].hFile);
        }
    }

    bool Open(const char *firstPath) {
        std::string p = firstPath;
        uint64_t cursor = 0;

        size_t dotPos = p.find_last_of('.');
        bool isNumberedSplit = false;
        std::string prefix;
        int startIdx = 1;
        int width = 3;

        if (dotPos != std::string::npos && p.size() - dotPos - 1 >= 2) {
            std::string ext = p.substr(dotPos + 1);
            bool allDigits = true;
            for (size_t i = 0; i < ext.size(); ++i) {
                if (!isdigit((unsigned char)ext[i])) { allDigits = false; break; }
            }
            if (allDigits) {
                isNumberedSplit = true;
                prefix = p.substr(0, dotPos + 1);
                startIdx = atoi(ext.c_str());
                width = (int)ext.size();
            }
        }

        if (isNumberedSplit) {
            for (int idx = startIdx; idx < 999; ++idx) {
                char segPath[512];
                _snprintf(segPath, sizeof(segPath), "%s%0*d", prefix.c_str(), width, idx);
                HANDLE h = CreateFileA(segPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                if (h == INVALID_HANDLE_VALUE) break;
                uint64_t sz = 0;
                if (!GetFileSize64(h, sz) || sz == 0) { CloseHandle(h); break; }
                IsoSegment s = { h, cursor, sz };
                segs.push_back(s);
                cursor += sz;
            }
        } else {
            HANDLE h = CreateFileA(firstPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) return false;
            uint64_t sz = 0;
            if (!GetFileSize64(h, sz) || sz == 0) { CloseHandle(h); return false; }
            IsoSegment s = { h, 0, sz };
            segs.push_back(s);
            cursor = sz;
        }

        if (segs.empty()) return false;
        totalDataSize = cursor;

        const uint64_t offsets[4] = { 0ULL, 0x2080000ULL, 0xFD90000ULL, 0x18300000ULL };
        char magic[20] = {0};
        for (size_t i = 0; i < 4; ++i) {
            if (cursor > offsets[i] + 32 * SECTOR_SIZE + 20) {
                if (ReadRaw(offsets[i] + 32 * SECTOR_SIZE, magic, 20) && memcmp(magic, "MICROSOFT*XBOX*MEDIA", 20) == 0) {
                    baseOffset = offsets[i];
                    totalDataSize = cursor - baseOffset;
                    return true;
                }
            }
        }
        return false;
    }

    bool ReadRaw(uint64_t absOffset, void *buf, size_t len) {
        uint8_t *dst = (uint8_t *)buf;
        size_t rem = len;
        uint64_t pos = absOffset;

        for (size_t i = 0; i < segs.size() && rem > 0; ++i) {
            if (pos >= segs[i].startByte + segs[i].length) continue;
            uint64_t localOff = pos - segs[i].startByte;
            uint64_t avail = segs[i].length - localOff;
            size_t toRead = rem < avail ? rem : (size_t)avail;

            LARGE_INTEGER li;
            li.QuadPart = (LONGLONG)localOff;
            if (!SetFilePointerEx(segs[i].hFile, li, NULL, FILE_BEGIN)) return false;
            DWORD readBytes = 0;
            if (!ReadFile(segs[i].hFile, dst, (DWORD)toRead, &readBytes, NULL) || readBytes != (DWORD)toRead) return false;

            dst += toRead;
            pos += toRead;
            rem -= toRead;
        }
        return rem == 0;
    }

    uint64_t TotalBytes() const { return totalDataSize; }

    bool ReadBytes(uint64_t offset, void *buf, size_t len) {
        memset(buf, 0, len);
        if (offset >= totalDataSize) return true;
        size_t toRead = len;
        if (offset + toRead > totalDataSize) toRead = (size_t)(totalDataSize - offset);
        return ReadRaw(baseOffset + offset, buf, toRead);
    }
};

struct VEntry {
    std::string name;
    std::string hostPath;
    bool isDir;
    uint32_t size;
    uint32_t startSector;
    std::vector<VEntry> children;
    std::vector<uint8_t> dirTable;
};

static bool VEntryLess(const VEntry &a, const VEntry &b) {
    size_t n = a.name.size() < b.name.size() ? a.name.size() : b.name.size();
    for (size_t i = 0; i < n; ++i) {
        unsigned char ca = (unsigned char)toupper((unsigned char)a.name[i]);
        unsigned char cb = (unsigned char)toupper((unsigned char)b.name[i]);
        if (ca != cb) return ca < cb;
    }
    return a.name.size() < b.name.size();
}

static void ScanDirWin32(const std::string &dirPath, VEntry &rootNode, GodLogCallback logFn) {
    WIN32_FIND_DATAA *pfd = (WIN32_FIND_DATAA *)malloc(sizeof(WIN32_FIND_DATAA));
    if (!pfd) return;
    std::vector<VEntry *> q;
    rootNode.hostPath = dirPath;
    q.push_back(&rootNode);

    for (size_t idx = 0; idx < q.size(); ++idx) {
        if (logFn && (idx % 20 == 0)) logFn("  Scanning folder %u / %u...\n", (unsigned int)(idx + 1), (unsigned int)q.size());
        VEntry *cur = q[idx];
        std::string pattern = cur->hostPath;
        if (!pattern.empty() && pattern[pattern.size() - 1] != '\\') pattern += "\\";
        pattern += "*";

        HANDLE hFind = FindFirstFileA(pattern.c_str(), pfd);
        if (hFind == INVALID_HANDLE_VALUE) continue;
        do {
            if (strcmp(pfd->cFileName, ".") == 0 || strcmp(pfd->cFileName, "..") == 0) continue;
            if (idx == 0 && (_stricmp(pfd->cFileName, "$SystemUpdate") == 0 || _stricmp(pfd->cFileName, "$$ystemUpdate") == 0 || _stricmp(pfd->cFileName, "Content") == 0)) continue;
            std::string childPath = cur->hostPath;
            if (!childPath.empty() && childPath[childPath.size() - 1] != '\\') childPath += "\\";
            childPath += pfd->cFileName;

            VEntry child;
            child.name = pfd->cFileName;
            child.hostPath = childPath;
            child.isDir = (pfd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            child.size = child.isDir ? 0 : pfd->nFileSizeLow;
            child.startSector = 0;
            cur->children.push_back(child);
        } while (FindNextFileA(hFind, pfd));
        FindClose(hFind);
        std::sort(cur->children.begin(), cur->children.end(), VEntryLess);
        for (size_t c = 0; c < cur->children.size(); ++c) {
            if (cur->children[c].isDir) q.push_back(&cur->children[c]);
        }
    }
    free(pfd);
}

static void BuildDirTableIterative(const std::vector<VEntry> &items, std::vector<uint8_t> &buf, std::vector<uint32_t> &sectorPatchOffsets) {
    if (items.empty()) return;
    struct Range { int lo, hi; uint32_t parentLinkOff; };
    std::vector<Range> stk;
    Range r0 = { 0, (int)items.size() - 1, 0xFFFFFFFFU };
    stk.push_back(r0);
    while (!stk.empty()) {
        Range cur = stk.back(); stk.pop_back();
        if (cur.lo > cur.hi) continue;
        int mid = (cur.lo + cur.hi) / 2;
        size_t entryPos = buf.size();
        uint8_t nameLen = (uint8_t)std::min<size_t>(items[mid].name.size(), 255);
        size_t recSize = (14 + nameLen + 3) & ~3U;
        if ((entryPos / SECTOR_SIZE) != ((entryPos + recSize - 1) / SECTOR_SIZE)) {
            size_t nextSec = ((entryPos / SECTOR_SIZE) + 1) * (size_t)SECTOR_SIZE;
            buf.resize(nextSec, 0xFF);
            entryPos = buf.size();
        }
        uint16_t myDwordOff = (uint16_t)(entryPos / 4);
        if (cur.parentLinkOff != 0xFFFFFFFFU) {
            buf[cur.parentLinkOff + 0] = (uint8_t)(myDwordOff & 0xFF);
            buf[cur.parentLinkOff + 1] = (uint8_t)((myDwordOff >> 8) & 0xFF);
        }
        buf.resize(entryPos + recSize, 0xFF);
        uint8_t *p = &buf[entryPos];
        memset(p, 0, 14);
        p[12] = items[mid].isDir ? 0x10 : 0x20;
        p[13] = nameLen;
        memcpy(p + 14, items[mid].name.data(), nameLen);
        sectorPatchOffsets[mid] = (uint32_t)(entryPos + 4);
        uint32_t sz = items[mid].size;
        p[8] = (uint8_t)(sz & 0xFF); p[9] = (uint8_t)((sz >> 8) & 0xFF);
        p[10] = (uint8_t)((sz >> 16) & 0xFF); p[11] = (uint8_t)((sz >> 24) & 0xFF);
        if (mid + 1 <= cur.hi) { Range rr = { mid + 1, cur.hi, (uint32_t)(entryPos + 2) }; stk.push_back(rr); }
        if (cur.lo <= mid - 1) { Range rl = { cur.lo, mid - 1, (uint32_t)(entryPos + 0) }; stk.push_back(rl); }
    }
}

struct VExtent {
    uint64_t startByte;
    uint64_t length;
    int type;
    const VEntry *entry;
};

extern "C" {
    typedef struct _XBOX_ANSI_STR { USHORT Length; USHORT MaximumLength; PCHAR Buffer; } XBOX_ANSI_STR;
    typedef struct _XBOX_OBJ_ATTR { HANDLE RootDirectory; XBOX_ANSI_STR *ObjectName; ULONG Attributes; } XBOX_OBJ_ATTR;
    typedef struct _XBOX_IOSB { LONG Status; ULONG Information; } XBOX_IOSB;
    LONG WINAPI NtOpenFile(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, XBOX_OBJ_ATTR *ObjectAttributes, XBOX_IOSB *IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions);
    LONG WINAPI NtReadFile(HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext, XBOX_IOSB *IoStatusBlock, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset);
    LONG WINAPI NtClose(HANDLE Handle);
}

class RawCdromStream : public DiscStream {
    HANDLE hDev;
    uint64_t partBase;
    uint64_t partBytes;
    uint8_t *secCache;
    uint64_t cacheStart;
    size_t cacheLen;
    bool ReadRawAligned(uint64_t devOff, void *dst, size_t len) {
        LARGE_INTEGER li; li.QuadPart = (LONGLONG)devOff;
        XBOX_IOSB iosb; iosb.Status = 0; iosb.Information = 0;
        LONG st = NtReadFile(hDev, NULL, NULL, NULL, &iosb, dst, (ULONG)len, &li);
        return (st >= 0 && iosb.Information == (ULONG)len);
    }
public:
    RawCdromStream() : hDev(INVALID_HANDLE_VALUE), partBase(0), partBytes(8500ULL * 1024ULL * 1024ULL), secCache(NULL), cacheStart(0), cacheLen(0) {}
    ~RawCdromStream() { if (hDev != INVALID_HANDLE_VALUE) NtClose(hDev); if (secCache) _aligned_free(secCache); }
    bool Open(GodLogCallback logFn) {
        char devName[] = "\\Device\\Cdrom0";
        XBOX_ANSI_STR sDev = { (USHORT)strlen(devName), (USHORT)strlen(devName) + 1, devName };
        XBOX_OBJ_ATTR oa = { NULL, &sDev, 0x40 };
        XBOX_IOSB iosb = { 0, 0 };
        HANDLE h = INVALID_HANDLE_VALUE;
        LONG st = NtOpenFile(&h, GENERIC_READ | SYNCHRONIZE, &oa, &iosb, FILE_SHARE_READ | FILE_SHARE_WRITE, 0x20 | 0x40);
        if (st < 0 || h == INVALID_HANDLE_VALUE) {
            if (logFn) logFn("  NtOpenFile(\\Device\\Cdrom0) failed: 0x%08X\n", (unsigned int)st);
            return false;
        }
        hDev = h;
        secCache = (uint8_t *)_aligned_malloc(65536, 2048);
        if (!secCache) return false;
        uint64_t probes[] = { 0x00000000ULL, 0x02080000ULL, 0x0FD90000ULL, 0x18300000ULL };
        const char *names[] = { "XISO", "XGD3", "XGD2", "XGD1" };
        for (int p = 0; p < 4; ++p) {
            if (ReadRawAligned(probes[p] + 32ULL * 2048ULL, secCache, 2048) && memcmp(secCache, "MICROSOFT*XBOX*MEDIA", 20) == 0) {
                partBase = probes[p];
                cacheStart = probes[p] + 32ULL * 2048ULL;
                cacheLen = 2048;
                if (logFn) logFn("Detected retail %s game partition on optical disc!\n", names[p]);
                return true;
            }
        }
        if (logFn) logFn("  No Xbox game partition signature found on Cdrom0.\n");
        return false;
    }
    uint64_t TotalBytes() const { return partBytes; }
    bool ReadBytes(uint64_t offset, void *buf, size_t len) {
        memset(buf, 0, len);
        uint8_t *dst = (uint8_t *)buf;
        size_t rem = len;
        while (rem > 0) {
            uint64_t devOff = partBase + offset;
            if (devOff < cacheStart || devOff >= cacheStart + cacheLen) {
                uint64_t alignedOff = devOff & ~2047ULL;
                if (!ReadRawAligned(alignedOff, secCache, 65536)) {
                    if (!ReadRawAligned(alignedOff, secCache, 2048)) return false;
                    cacheStart = alignedOff; cacheLen = 2048;
                } else {
                    cacheStart = alignedOff; cacheLen = 65536;
                }
            }
            size_t offInCache = (size_t)(devOff - cacheStart);
            size_t avail = cacheLen - offInCache;
            size_t take = rem < avail ? rem : avail;
            memcpy(dst, secCache + offInCache, take);
            dst += take; offset += take; rem -= take;
        }
        return true;
    }
};

class VirtualFolderStream : public DiscStream {
    VEntry root;
    uint8_t *headerSectors;
    std::vector<VExtent> extents;
    uint64_t totalBytes;
    HANDLE cachedHandle;
    std::string cachedPath;
    uint8_t *seqCache;
    uint64_t seqCacheStart;
    size_t seqCacheLen;

    uint8_t rootMediaId[16];
    bool hasRootMediaId;

    void DetectRootMediaId() {
        hasRootMediaId = false;
        memset(rootMediaId, 0, 16);
        for (size_t i = 0; i < root.children.size(); ++i) {
            if (!root.children[i].isDir && _stricmp(root.children[i].name.c_str(), "default.xex") == 0) {
                FILE *f = fopen(root.children[i].hostPath.c_str(), "rb");
                if (f) {
                    uint8_t h[0x400] = {0};
                    size_t nr = fread(h, 1, sizeof(h), f);
                    fclose(f);
                    if (nr >= 0x100 && memcmp(h, "XEX2", 4) == 0) {
                        uint32_t cOff = ((uint32_t)h[0x10] << 24) | ((uint32_t)h[0x11] << 16) | ((uint32_t)h[0x12] << 8) | (uint32_t)h[0x13];
                        if (cOff + 0x164 <= nr) {
                            memcpy(rootMediaId, h + cOff + 0x154, 16);
                            hasRootMediaId = true;
                        }
                    }
                }
                break;
            }
        }
    }

    void PatchSecondaryXexBuffer(const VEntry *ent, uint64_t srcOff, uint8_t *dstPtr, size_t chunk) {
        if (!hasRootMediaId || !ent || srcOff >= 0x400) return;
        size_t nlen = ent->name.size();
        if (nlen <= 4 || _stricmp(ent->name.c_str() + nlen - 4, ".xex") != 0 || _stricmp(ent->name.c_str(), "default.xex") == 0) return;
        FILE *f = fopen(ent->hostPath.c_str(), "rb");
        if (!f) return;
        uint8_t h[0x400] = {0};
        size_t nr = fread(h, 1, sizeof(h), f);
        fclose(f);
        if (nr >= 0x100 && memcmp(h, "XEX2", 4) == 0) {
            uint32_t cOff = ((uint32_t)h[0x10] << 24) | ((uint32_t)h[0x11] << 16) | ((uint32_t)h[0x12] << 8) | (uint32_t)h[0x13];
            if (cOff + 0x16C <= nr) {
                memcpy(h + cOff + 0x154, rootMediaId, 16);
                memset(h + cOff + 0x168, 0xFF, 4);
                size_t endCopy = std::min<size_t>(nr, (size_t)(srcOff + chunk));
                if (endCopy > (size_t)srcOff) {
                    memcpy(dstPtr, h + (size_t)srcOff, endCopy - (size_t)srcOff);
                }
            }
        }
    }

    void LayoutAllIterative(VEntry &rootNode, uint32_t &curSector) {
        std::vector<VEntry *> dirs;
        dirs.push_back(&rootNode);
        for (size_t d = 0; d < dirs.size(); ++d) {
            VEntry &node = *dirs[d];
            if (node.children.empty()) {
                node.dirTable.resize(2048, 0);
                node.startSector = curSector;
                node.size = 2048;
                VExtent ex = { (uint64_t)curSector * SECTOR_SIZE, 2048, 1, &node };
                extents.push_back(ex);
                curSector += 1;
                continue;
            }
            std::vector<uint32_t> patchOffs(node.children.size(), 0);
            BuildDirTableIterative(node.children, node.dirTable, patchOffs);
            size_t padded = (node.dirTable.size() + 2047) & ~2047U;
            node.dirTable.resize(padded, 0xFF);
            node.startSector = curSector;
            node.size = (uint32_t)padded;
            VExtent ex = { (uint64_t)curSector * SECTOR_SIZE, padded, 1, &node };
            extents.push_back(ex);
            curSector += (uint32_t)(padded / SECTOR_SIZE);

            for (size_t i = 0; i < node.children.size(); ++i) {
                if (node.children[i].isDir) {
                    dirs.push_back(&node.children[i]);
                } else {
                    node.children[i].startSector = curSector;
                    uint32_t secCount = (node.children[i].size + 2047) / 2048;
                    if (secCount == 0) secCount = 1;
                    if (node.children[i].size > 0) {
                        VExtent fex = { (uint64_t)curSector * SECTOR_SIZE, node.children[i].size, 2, &node.children[i] };
                        extents.push_back(fex);
                    }
                    curSector += secCount;
                }
            }
        }
        for (size_t d = 0; d < dirs.size(); ++d) {
            VEntry &node = *dirs[d];
            if (node.children.empty()) continue;
            std::vector<uint32_t> patchOffs(node.children.size(), 0);
            node.dirTable.clear();
            BuildDirTableIterative(node.children, node.dirTable, patchOffs);
            size_t padded = (node.dirTable.size() + 2047) & ~2047U;
            node.dirTable.resize(padded, 0xFF);
            for (size_t i = 0; i < node.children.size(); ++i) {
                uint32_t st = node.children[i].startSector;
                uint32_t po = patchOffs[i];
                node.dirTable[po + 0] = (uint8_t)(st & 0xFF);
                node.dirTable[po + 1] = (uint8_t)((st >> 8) & 0xFF);
                node.dirTable[po + 2] = (uint8_t)((st >> 16) & 0xFF);
                node.dirTable[po + 3] = (uint8_t)((st >> 24) & 0xFF);
            }
        }
    }

public:
    VirtualFolderStream(const std::string &folder, GodLogCallback logFn = NULL) : headerSectors((uint8_t *)calloc(1, 33 * 2048)), cachedHandle(INVALID_HANDLE_VALUE), seqCache(NULL), seqCacheStart(0), seqCacheLen(0) {
        if (!headerSectors) return;
        root.isDir = true;
        ScanDirWin32(folder, root, logFn); if (logFn) logFn("  Building XISO layout (%u folders)...\n", (unsigned int)root.children.size());
        uint32_t curSector = 33;
        LayoutAllIterative(root, curSector);

        uint8_t *sec32 = headerSectors + (32 * 2048);
        memcpy(sec32, "MICROSOFT*XBOX*MEDIA", 20);
        *(uint32_t *)(sec32 + 20) = Swap32(root.startSector);
        *(uint32_t *)(sec32 + 24) = Swap32(root.size);
        memcpy(sec32 + 2028, "MICROSOFT*XBOX*MEDIA", 20);
        totalBytes = (uint64_t)curSector * SECTOR_SIZE;
    }

    ~VirtualFolderStream() { if (headerSectors) free(headerSectors); if (cachedHandle != INVALID_HANDLE_VALUE) CloseHandle(cachedHandle); if (seqCache) free(seqCache); }
    uint64_t TotalBytes() const { return totalBytes; }

    bool ReadBytes(uint64_t offset, void *buf, size_t len) {
        memset(buf, 0, len);
        if (offset >= totalBytes) return true;
        uint8_t *dst = (uint8_t *)buf;
        uint64_t end = std::min<uint64_t>(offset + len, totalBytes);

        if (offset < (33ULL * 2048ULL)) {
            uint64_t hEnd = std::min<uint64_t>(end, (33ULL * 2048ULL));
            memcpy(dst, headerSectors + offset, (size_t)(hEnd - offset));
        }

        size_t lo = 0, hi = extents.size(), startIdx = extents.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (extents[mid].startByte + extents[mid].length <= offset) lo = mid + 1;
            else { startIdx = mid; hi = mid; }
        }
        for (size_t i = startIdx; i < extents.size(); ++i) {
            const VExtent &ex = extents[i];
            if (ex.startByte + ex.length <= offset) continue;
            if (ex.startByte >= end) break;
            uint64_t s = std::max<uint64_t>(offset, ex.startByte);
            uint64_t e = std::min<uint64_t>(end, ex.startByte + ex.length);
            size_t chunk = (size_t)(e - s);
            size_t dstOff = (size_t)(s - offset);
            uint64_t srcOff = s - ex.startByte;

            if (ex.type == 1) {
                memcpy(dst + dstOff, &ex.entry->dirTable[(size_t)srcOff], chunk);
            } else if (ex.type == 2) {
                if (cachedPath != ex.entry->hostPath) {
                    if (cachedHandle != INVALID_HANDLE_VALUE) { CloseHandle(cachedHandle); cachedHandle = INVALID_HANDLE_VALUE; }
                    cachedPath = ex.entry->hostPath;
                    seqCacheLen = 0;
                    cachedHandle = CreateFileA(cachedPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                    if (cachedHandle == INVALID_HANDLE_VALUE) return false;
                }
                if (!seqCache) seqCache = (uint8_t *)malloc(65536);
                if (seqCache && chunk <= 65536) {
                    if (srcOff < seqCacheStart || srcOff + chunk > seqCacheStart + seqCacheLen) {
                        LARGE_INTEGER li; li.QuadPart = (LONGLONG)srcOff;
                        SetFilePointerEx(cachedHandle, li, NULL, FILE_BEGIN);
                        DWORD rb = 0;
                        ReadFile(cachedHandle, seqCache, 65536, &rb, NULL);
                        seqCacheStart = srcOff;
                        seqCacheLen = (size_t)rb;
                    }
                    if (srcOff >= seqCacheStart && srcOff + chunk <= seqCacheStart + seqCacheLen) {
                        memcpy(dst + dstOff, seqCache + (size_t)(srcOff - seqCacheStart), chunk);
                    } else {
                        LARGE_INTEGER li; li.QuadPart = (LONGLONG)srcOff;
                        SetFilePointerEx(cachedHandle, li, NULL, FILE_BEGIN);
                        DWORD rb = 0;
                        ReadFile(cachedHandle, dst + dstOff, (DWORD)chunk, &rb, NULL);
                    }
                } else {
                    LARGE_INTEGER li; li.QuadPart = (LONGLONG)srcOff;
                    SetFilePointerEx(cachedHandle, li, NULL, FILE_BEGIN);
                    DWORD rb = 0;
                    ReadFile(cachedHandle, dst + dstOff, (DWORD)chunk, &rb, NULL);
                }
            }
        }
        return true;
    }
};

struct GameMeta {
    uint32_t titleId;
    uint8_t mediaId[16];
    uint8_t discNum;
    uint8_t totalDiscs;
    uint8_t platform;
    uint8_t execType;
    wchar_t titleName[128];
    std::vector<uint8_t> pngIcon;
};

struct XisoDirEntry {
    std::string name;
    uint32_t sector;
    uint32_t size;
    bool isDir;
};

static bool ListXisoDirectory(DiscStream *disc, uint32_t dirSector, uint32_t dirSize, std::vector<XisoDirEntry> &outEntries) {
    if (dirSize == 0 || dirSize > 16 * 1024 * 1024) return false;
    std::vector<uint8_t> table(dirSize);
    if (!disc->ReadBytes((uint64_t)dirSector * SECTOR_SIZE, &table[0], dirSize)) return false;

    std::vector<uint8_t> visited(dirSize / 4 + 2, 0);
    std::vector<uint32_t> stack;
    stack.push_back(0);
    while (!stack.empty()) {
        uint32_t off = stack.back();
        stack.pop_back();
        if (off == 0xFFFF * 4 || off + 14 > dirSize || visited[off / 4]) continue;
        visited[off / 4] = 1;

        uint16_t left = Swap16(*(uint16_t *)&table[off]);
        uint16_t right = Swap16(*(uint16_t *)&table[off + 2]);
        uint32_t sec = Swap32(*(uint32_t *)&table[off + 4]);
        uint32_t sz = Swap32(*(uint32_t *)&table[off + 8]);
        uint8_t attr = table[off + 12];
        uint8_t nlen = table[off + 13];
        if (off + 14 + nlen > dirSize) continue;

        if (left != 0 && left != 0xFFFF) stack.push_back((uint32_t)left * 4);
        if (right != 0 && right != 0xFFFF) stack.push_back((uint32_t)right * 4);

        if (nlen > 0) {
            XisoDirEntry ent = { std::string((const char *)&table[off + 14], nlen), sec, sz, (attr & 0x10) != 0 };
            outEntries.push_back(ent);
        }
    }
    return true;
}

static uint64_t SumNonContentBytes(DiscStream *disc, uint32_t dirSector, uint32_t dirSize, bool isRoot) {
    std::vector<XisoDirEntry> entries;
    if (!ListXisoDirectory(disc, dirSector, dirSize, entries)) return 0;
    uint64_t total = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (isRoot && _stricmp(entries[i].name.c_str(), "Content") == 0) continue;
        if (entries[i].isDir) total += SumNonContentBytes(disc, entries[i].sector, entries[i].size, false);
        else total += entries[i].size;
    }
    return total;
}

static bool ExtractXisoTree(DiscStream *disc, uint32_t dirSector, uint32_t dirSize, const std::string &destDir, GodLogCallback logFn) {
    std::vector<XisoDirEntry> entries;
    if (!ListXisoDirectory(disc, dirSector, dirSize, entries)) return false;
    ForceCreateDirectoryTree(destDir.c_str());

    const size_t BUF_SZ = 256 * 1024;
    uint8_t *buf = (uint8_t *)malloc(BUF_SZ);
    if (!buf) return false;

    for (size_t i = 0; i < entries.size(); ++i) {
        if (_stricmp(entries[i].name.c_str(), "$SystemUpdate") == 0 || _stricmp(entries[i].name.c_str(), "$$ystemUpdate") == 0) continue;
        std::string childDst = destDir + "\\" + entries[i].name;
        if (entries[i].isDir) {
            if (!ExtractXisoTree(disc, entries[i].sector, entries[i].size, childDst, logFn)) { free(buf); return false; }
        } else {
            if (logFn) logFn("DLC/Install: Extracting %s (%.1f MB)...\n", entries[i].name.c_str(), entries[i].size / (1024.0 * 1024.0));
            HANDLE hOut = CreateFileA(childDst.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hOut == INVALID_HANDLE_VALUE) { free(buf); return false; }

            uint64_t rem = entries[i].size;
            uint64_t srcOff = (uint64_t)entries[i].sector * SECTOR_SIZE;
            while (rem > 0) {
                size_t chunk = rem > BUF_SZ ? BUF_SZ : (size_t)rem;
                DWORD written = 0;
                if (!disc->ReadBytes(srcOff, buf, chunk) || !WriteFile(hOut, buf, (DWORD)chunk, &written, NULL) || written != (DWORD)chunk) {
                    CloseHandle(hOut); free(buf); return false;
                }
                srcOff += chunk;
                rem -= chunk;
            }
            CloseHandle(hOut);
        }
    }
    free(buf);
    return true;
}

static bool FindAnyXbeInXiso(DiscStream *disc, uint32_t dirSector, uint32_t dirSize, uint32_t &outSector, uint32_t &outSize) {
    
    if (dirSize == 0 || dirSize > 16 * 1024 * 1024) return false;
    std::vector<uint8_t> table(dirSize);
    if (!disc->ReadBytes((uint64_t)dirSector * SECTOR_SIZE, &table[0], dirSize)) return false;

    bool found = false;
    uint32_t bestSize = 0;
    std::vector<uint8_t> visited(dirSize / 4 + 2, 0);
    std::vector<uint32_t> stack;
    stack.push_back(0);
    while (!stack.empty()) {
        uint32_t off = stack.back();
        stack.pop_back();
        if (off == 0xFFFF * 4 || off + 14 > dirSize || visited[off / 4]) continue;
        visited[off / 4] = 1;

        uint16_t left = Swap16(*(uint16_t *)&table[off]);
        uint16_t right = Swap16(*(uint16_t *)&table[off + 2]);
        uint32_t sec = Swap32(*(uint32_t *)&table[off + 4]);
        uint32_t sz = Swap32(*(uint32_t *)&table[off + 8]);
        uint8_t attr = table[off + 12];
        uint8_t nlen = table[off + 13];
        if (off + 14 + nlen > dirSize) continue;

        if (left != 0 && left != 0xFFFF) stack.push_back((uint32_t)left * 4);
        if (right != 0 && right != 0xFFFF) stack.push_back((uint32_t)right * 4);

        if (!(attr & 0x10) && nlen > 4 && _strnicmp((const char *)&table[off + 14 + nlen - 4], ".xbe", 4) == 0) {
            if (!found || _strnicmp((const char *)&table[off + 14], "default.xbe", nlen) == 0 || sz > bestSize) {
                found = true;
                bestSize = sz;
                outSector = sec;
                outSize = sz;
            }
        }
    }
    return found;
}

static uint16_t RdBE16(const uint8_t *p) { return ((uint16_t)(*p) << 8) | (uint16_t)*(p + 1); }
static uint32_t RdBE32(const uint8_t *p) { return ((uint32_t)(*p) << 24) | ((uint32_t)*(p + 1) << 16) | ((uint32_t)*(p + 2) << 8) | (uint32_t)*(p + 3); }

static void FitPngTo16KB(const uint8_t *srcPng, uint32_t srcLen, std::vector<uint8_t> &outPng) {
    if (!srcPng || srcLen < 16) return;
    if (srcLen <= 16384) { outPng.assign(srcPng, srcPng + srcLen); return; }
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return;
    D3DPRESENT_PARAMETERS pp; ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = 64; pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9 *dev = NULL;
    if (SUCCEEDED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev)) && dev) {
        IDirect3DTexture9 *tex = NULL;
        if (SUCCEEDED(D3DXCreateTextureFromFileInMemoryEx(dev, srcPng, srcLen, 64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, D3DX_FILTER_LINEAR, D3DX_FILTER_NONE, 0, NULL, NULL, &tex)) && tex) {
            ID3DXBuffer *pngBuf = NULL;
            if (SUCCEEDED(D3DXSaveTextureToFileInMemory(&pngBuf, D3DXIFF_PNG, tex, NULL)) && pngBuf) {
                DWORD pSz = pngBuf->GetBufferSize();
                if (pSz > 16 && pSz <= 16384) outPng.assign((const uint8_t *)pngBuf->GetBufferPointer(), (const uint8_t *)pngBuf->GetBufferPointer() + pSz);
                pngBuf->Release();
            }
            tex->Release();
        }
        dev->Release();
    }
    d3d->Release();
}

static void ScanBufferForXdbfTitle(const uint8_t *tbuf, uint32_t toRead, wchar_t *outTitle, std::vector<uint8_t> *outPng = NULL) {
    for (uint32_t p = 0; p + 0x40 < toRead; p += 4) {
        if (memcmp(&tbuf[p], "XDBF", 4) != 0) continue;
        uint32_t entTblLen = RdBE32(&tbuf[p + 0x08]);
        uint32_t entCount  = RdBE32(&tbuf[p + 0x0C]);
        uint32_t freeLen   = RdBE32(&tbuf[p + 0x10]);
        if (entCount == 0 || entCount > 4096 || entTblLen > 4096 || freeLen > 4096) continue;
        uint64_t dataBase = (uint64_t)p + 0x18ULL + (uint64_t)entTblLen * 18ULL + (uint64_t)freeLen * 8ULL;
        if (dataBase >= toRead) continue;
        for (uint32_t e = 0; e < entCount; ++e) {
            uint32_t eOff = p + 0x18 + e * 18;
            if (eOff + 18 > toRead) break;
            uint16_t ns = RdBE16(&tbuf[eOff]);
            uint32_t idLo = RdBE32(&tbuf[eOff + 6]);
            uint32_t dOff = RdBE32(&tbuf[eOff + 10]);
            uint32_t dSz  = RdBE32(&tbuf[eOff + 14]);
            if (outPng && outPng->empty() && (ns == 1 || ns == 2) && dataBase + dOff + dSz <= toRead && dSz >= 16 && dSz <= 131072) {
                uint32_t ip = (uint32_t)(dataBase + dOff);
                for (uint32_t sOff = 0; sOff + 8 <= dSz && sOff <= 32; ++sOff) {
                    if (tbuf[ip + sOff] == 0x89 && tbuf[ip + sOff + 1] == 0x50 && tbuf[ip + sOff + 2] == 0x4E && tbuf[ip + sOff + 3] == 0x47) {
                        FitPngTo16KB(&tbuf[ip + sOff], dSz - sOff, *outPng);
                        break;
                    }
                }
            }
            if (ns == 3 && idLo == 1 && dataBase + dOff + 16 <= toRead && dSz >= 16) {
                uint32_t xp = (uint32_t)(dataBase + dOff);
                if (memcmp(&tbuf[xp], "XSTR", 4) == 0) {
                    uint16_t sc = RdBE16(&tbuf[xp + 12]);
                    uint32_t cur = xp + 14;
                    for (uint16_t s = 0; s < sc && cur + 4 <= toRead; ++s) {
                        uint16_t sid = RdBE16(&tbuf[cur]);
                        uint16_t blen = RdBE16(&tbuf[cur + 2]);
                        cur += 4;
                        if (cur + blen > toRead) break;
                        if ((sid == 0x8000 || outTitle[0] == 0) && blen >= 2) {
                            int chars = blen < 127 ? blen : 127;
                            for (int k = 0; k < chars; ++k) outTitle[k] = (wchar_t)(unsigned char)tbuf[cur + k];
                            outTitle[chars] = 0;
                        }
                        cur += blen;
                    }
                }
            }
        }
        if (outTitle[0] != 0) return;
    }
}

static void ExtractTitleFromXexTail(DiscStream *disc, uint32_t xexSector, uint32_t xexSize, wchar_t *outTitle, std::vector<uint8_t> *outPng = NULL) {
    if (xexSize < 0x100) return;
    if (outTitle != 0 && (!outPng || !outPng->empty())) return;

    // 1. Fast scan of unencrypted tail & head (works for decrypted/dev XEXs)
    const uint32_t CHUNK = 262144U;
    std::vector<uint8_t> tbuf(CHUNK + 32768U, 0);
    uint32_t tailStart = (xexSize > 1048576U) ? (xexSize - 1048576U) : 0U;
    for (uint32_t base = tailStart; base < xexSize; base += CHUNK) {
        uint32_t toRead = (xexSize - base) > (CHUNK + 32768U) ? (CHUNK + 32768U) : (xexSize - base);
        if (!disc->ReadBytes((uint64_t)xexSector * SECTOR_SIZE + base, &tbuf.front(), toRead)) break;
        ScanBufferForXdbfTitle(&tbuf.front(), toRead, outTitle, outPng);
        if (outTitle != 0 && (!outPng || !outPng->empty())) return;
    }

    // 2. Read XEX2 header & derive per-game AES-128 key if retail-encrypted (encType == 1)
    std::vector<uint8_t> hdrV(0x2000, 0); uint8_t *hdr = &hdrV.front();
    uint32_t hdrLen = (xexSize < 0x2000U) ? xexSize : 0x2000U;
    if (!disc->ReadBytes((uint64_t)xexSector * SECTOR_SIZE, hdr, hdrLen)) return;
    uint32_t peOff = RdBE32(&hdr[0x08]), certOff = RdBE32(&hdr[0x10]), optCount = RdBE32(&hdr[0x14]);
    uint16_t encType = 0, compType = 0;
    uint32_t winSize = 0x20000, firstBlockSz = 0;
    for (uint32_t i = 0; i < optCount && 0x18 + i * 8 + 8 <= hdrLen; ++i) {
        uint32_t id = RdBE32(&hdr[0x18 + i * 8]), val = RdBE32(&hdr[0x18 + i * 8 + 4]);
        if (id == 0x000003FF && val + 16 <= hdrLen) {
            encType = RdBE16(&hdr[val + 4]);
            compType = RdBE16(&hdr[val + 6]);
            if (compType == 2) { winSize = RdBE32(&hdr[val + 8]); firstBlockSz = RdBE32(&hdr[val + 12]); }
        }
    }

    XECRYPT_AES_STATE gameAes;
    BYTE aesIv[16];
    memset(aesIv, 0, sizeof(aesIv));
    bool hasAes = false;
    if (encType == 1 && certOff + 0x160 <= xexSize) {
        BYTE encKey[16], decKey[16];
        if (disc->ReadBytes((uint64_t)xexSector * SECTOR_SIZE + certOff + 0x150, encKey, 16)) {
            static const BYTE retailMasterKey[16] = { 0x20,0xB1,0x85,0xA5, 0x9D,0x28,0xFD,0xC3, 0x40,0x58,0x3F,0xBB, 0x08,0x96,0xBF,0x91 };
            XECRYPT_AES_STATE masterAes;
            XeCryptAesKey(&masterAes, retailMasterKey);
            XeCryptAesEcb(&masterAes, encKey, decKey, FALSE);
            XeCryptAesKey(&gameAes, decKey);
            hasAes = true;
        }
    }

    // 3. Normal LZX compressed XEX2 (compType == 2): decrypt each block with AES-128-CBC, then LZX decompress
    if (compType == 2 && firstBlockSz > 24 && peOff < xexSize) {
        XMEMCODEC_PARAMETERS_LZX lzxP = { 0, winSize, 0 };
        XMEMDECOMPRESSION_CONTEXT ctx = NULL;
        remove("game:\\xex_scan.tmp"); if (SUCCEEDED(XMemCreateDecompressionContext(XMEMCODEC_LZX, &lzxP, XMEMCOMPRESS_STREAM, &ctx)) && ctx) {
            uint64_t curOff = (uint64_t)xexSector * SECTOR_SIZE + peOff;
            uint32_t blkSz = firstBlockSz;
            std::vector<uint8_t> compBuf, decBuf(2097152);
            ScanBufferForXdbfTitle(hdr, hdrLen, outTitle, outPng);
            while (blkSz > 24 && blkSz < 32 * 1024 * 1024 && curOff + blkSz <= (uint64_t)xexSector * SECTOR_SIZE + xexSize) {
                compBuf.resize(blkSz);
                if (!disc->ReadBytes(curOff, &compBuf.front(), blkSz)) break;
                if (hasAes) XeCryptAesCbc(&gameAes, &compBuf.front(), (DWORD)(blkSz & ~15U), &compBuf.front(), aesIv, FALSE);
                uint32_t nextBlk = RdBE32(&compBuf.front());
                const uint8_t *pSrc = &compBuf.front() + 24;
                SIZE_T srcRem = blkSz - 24;
                while (srcRem > 0) {
                    SIZE_T dstLen = decBuf.size(), srcUsed = srcRem;
                    XMemDecompressStream(ctx, &decBuf.front(), &dstLen, pSrc, &srcUsed);
                    if (dstLen > 0) {
                        FILE *tf = fopen("game:\\xex_scan.tmp", "ab");
                        if (tf) { fwrite(&decBuf.front(), 1, dstLen, tf); fclose(tf); }
                    }
                    if (srcUsed == 0) break;
                    pSrc += srcUsed; srcRem -= srcUsed;
                }
                if (nextBlk == 0) break;
                curOff += blkSz; blkSz = nextBlk;
            }
            XMemDestroyDecompressionContext(ctx);
            FILE *rf = fopen("game:\\xex_scan.tmp", "rb");
            if (rf) {
                fseek(rf, 0, SEEK_END); long fLen = ftell(rf); fseek(rf, 0, SEEK_SET);
                std::vector<uint8_t> wBuf(CHUNK + 32768U, 0);
                for (long pos = 0; pos < fLen; pos += (long)CHUNK) {
                    fseek(rf, pos, SEEK_SET);
                    size_t nRead = fread(&wBuf.front(), 1, CHUNK + 32768U, rf);
                    if (nRead == 0) break;
                    ScanBufferForXdbfTitle(&wBuf.front(), (uint32_t)nRead, outTitle, outPng);
                    if (outTitle != 0 && (!outPng || !outPng->empty())) break;
                }
                fclose(rf);
            }
            remove("game:\\xex_scan.tmp");
            if (outTitle != 0 && (!outPng || !outPng->empty())) return;
        }
    }

    // 4. Basic / Uncompressed XEX2 (compType == 1): decrypt PE payload in chunks with AES-128-CBC & scan
    if (peOff < xexSize) {
        std::vector<uint8_t> peChunk(CHUNK);
        for (uint32_t off = peOff; off + 16 <= xexSize; off += CHUNK) {
            uint32_t toRead = (xexSize - off) > CHUNK ? CHUNK : (xexSize - off);
            uint32_t aligned = toRead & ~15U;
            if (aligned == 0) break;
            if (!disc->ReadBytes((uint64_t)xexSector * SECTOR_SIZE + off, &peChunk.front(), aligned)) break;
            if (hasAes) XeCryptAesCbc(&gameAes, &peChunk.front(), (DWORD)aligned, &peChunk.front(), aesIv, FALSE);
            ScanBufferForXdbfTitle(&peChunk.front(), aligned, outTitle, outPng);
            if (outTitle != 0 && (!outPng || !outPng->empty())) return;
        }
    }
}

static bool ParseXexMetadata(DiscStream *disc, uint32_t sec, uint32_t sz, GameMeta &meta) {
    if (sz < 0x100) return false;
    size_t hdrRead = std::min<uint32_t>(sz, 524288);
    std::vector<uint8_t> buf(hdrRead);
    if (!disc->ReadBytes((uint64_t)sec * SECTOR_SIZE, &buf.front(), hdrRead) || memcmp(&buf.front(), "XEX2", 4) != 0) return false;

    uint64_t certOff = RdBE32(&buf[0x10]);
    uint32_t optCount = RdBE32(&buf[0x14]);
    if (certOff + 0x180ULL <= (uint64_t)hdrRead) memcpy(meta.mediaId, &buf[(size_t)certOff + 0x154], 16);

    for (uint32_t i = 0; i < optCount && (uint64_t)0x18 + (uint64_t)i * 8ULL + 8ULL <= (uint64_t)hdrRead; ++i) {
        uint32_t id = RdBE32(&buf[0x18 + i * 8]);
        uint64_t val = RdBE32(&buf[0x18 + i * 8 + 4]);
        if (id == 0x00040006 && val + 0x18ULL <= (uint64_t)hdrRead) {
            memcpy(meta.mediaId, &buf[(size_t)val], 4);
            meta.titleId = RdBE32(&buf[(size_t)val + 0x0C]);
            meta.platform = buf[(size_t)val + 0x10];
            meta.execType = buf[(size_t)val + 0x11];
            meta.discNum = buf[(size_t)val + 0x12];
            meta.totalDiscs = buf[(size_t)val + 0x13];
        }
    }
    ExtractTitleFromXexTail(disc, sec, sz, meta.titleName, &meta.pngIcon);
    if (meta.discNum == 0) meta.discNum = 1;
    if (meta.totalDiscs == 0) meta.totalDiscs = 1;
    return meta.titleId != 0;
}

static void ExtractXbeXtimagePng(DiscStream *disc, uint32_t sec, uint32_t sz, const uint8_t *hdr, uint32_t baseAddr, std::vector<uint8_t> &outPng) {
    uint32_t numSecs = Swap32(*(uint32_t *)&hdr[0x11C]);
    uint32_t secHdrAddr = Swap32(*(uint32_t *)&hdr[0x120]);
    if (numSecs == 0 || numSecs > 64 || secHdrAddr < baseAddr) return;
    uint32_t secOff = secHdrAddr - baseAddr;
    if ((uint64_t)secOff + (uint64_t)numSecs * 0x38ULL > sz) return;
    std::vector<uint8_t> shBuf(numSecs * 0x38);
    if (!disc->ReadBytes((uint64_t)sec * SECTOR_SIZE + secOff, &shBuf.front(), shBuf.size())) return;
    for (uint32_t i = 0; i < numSecs; ++i) {
        const uint8_t *sh = &shBuf[i * 0x38];
        uint32_t rawAddr = Swap32(*(uint32_t *)&sh[0x0C]);
        uint32_t rawSize = Swap32(*(uint32_t *)&sh[0x10]);
        uint32_t nameAddr = Swap32(*(uint32_t *)&sh[0x14]);
        if (nameAddr < baseAddr || (nameAddr - baseAddr) + 12 > sz) continue;
        std::vector<char> snameV(16, 0); char *sname = &snameV.front();
        if (!disc->ReadBytes((uint64_t)sec * SECTOR_SIZE + (nameAddr - baseAddr), sname, 10)) continue;
        if (strcmp(sname, "$$XTIMAGE") != 0 && strcmp(sname, "$$XSIMAGE") != 0) continue;
        if (rawSize < 0x30 || rawSize > 65536 || (uint64_t)rawAddr + rawSize > sz) continue;
        std::vector<uint8_t> xpr(rawSize);
        if (!disc->ReadBytes((uint64_t)sec * SECTOR_SIZE + rawAddr, &xpr.front(), rawSize)) continue;
        uint32_t dataStart = 0;
        if (memcmp(&xpr.front(), "XPR0", 4) == 0) dataStart = Swap32(*(uint32_t *)(&xpr.front() + 8));
        else if (rawSize != 8192 && rawSize != 2048) continue;
        if (dataStart >= rawSize) continue;
        uint32_t dxtBytes = rawSize - dataStart;
        UINT dim = (dxtBytes >= 8192) ? 128 : ((dxtBytes >= 2048) ? 64 : 0);
        if (dim == 0) continue;

        UINT argbBytes = dim * dim * 4;
        void *physMem = XPhysicalAlloc(argbBytes, MAXULONG_PTR, 128, PAGE_READWRITE);
        if (!physMem) continue;
        uint32_t *dstPixels = (uint32_t *)physMem;
        const uint8_t *src = &xpr[dataStart];
        UINT bw = dim / 4, bh = dim / 4;
        for (UINT by = 0; by < bh; ++by) {
            for (UINT bx = 0; bx < bw; ++bx) {
                const uint8_t *blk = src + (by * bw + bx) * 8;
                uint16_t c0 = (uint16_t)blk[0] | ((uint16_t)blk[1] << 8);
                uint16_t c1 = (uint16_t)blk[2] | ((uint16_t)blk[3] << 8);
                uint32_t bits = (uint32_t)blk[4] | ((uint32_t)blk[5] << 8) | ((uint32_t)blk[6] << 16) | ((uint32_t)blk[7] << 24);
                std::vector<uint32_t> pal(4, 0);
                uint32_t r0 = ((c0 >> 11) & 0x1F) * 255 / 31;
                uint32_t g0 = ((c0 >> 5) & 0x3F) * 255 / 63;
                uint32_t b0 = (c0 & 0x1F) * 255 / 31;
                uint32_t r1 = ((c1 >> 11) & 0x1F) * 255 / 31;
                uint32_t g1 = ((c1 >> 5) & 0x3F) * 255 / 63;
                uint32_t b1 = (c1 & 0x1F) * 255 / 31;
                pal[0] = 0xFF000000U | (r0 << 16) | (g0 << 8) | b0;
                pal[1] = 0xFF000000U | (r1 << 16) | (g1 << 8) | b1;
                if (c0 > c1) {
                    pal[2] = 0xFF000000U | (((2 * r0 + r1) / 3) << 16) | (((2 * g0 + g1) / 3) << 8) | ((2 * b0 + b1) / 3);
                    pal[3] = 0xFF000000U | (((r0 + 2 * r1) / 3) << 16) | (((g0 + 2 * g1) / 3) << 8) | ((b0 + 2 * b1) / 3);
                } else {
                    pal[2] = 0xFF000000U | (((r0 + r1) / 2) << 16) | (((g0 + g1) / 2) << 8) | ((b0 + b1) / 2);
                    pal[3] = 0x00000000U;
                }
                for (UINT py = 0; py < 4; ++py) {
                    for (UINT px = 0; px < 4; ++px) {
                        uint32_t code = (bits >> ((py * 4 + px) * 2)) & 3U;
                        dstPixels[(by * 4 + py) * dim + (bx * 4 + px)] = pal[code];
                    }
                }
            }
        }

        IDirect3DTexture9 tex;
        XGSetTextureHeader(dim, dim, 1, 0, D3DFMT_LIN_A8R8G8B8, 0, 0, XGHEADER_CONTIGUOUS_MIP_OFFSET, 0, &tex, NULL, NULL);
        XGOffsetResourceAddress(&tex, physMem);
        IDirect3DSurface9 *pSurf = NULL;
        if (SUCCEEDED(tex.GetSurfaceLevel(0, &pSurf)) && pSurf) {
            ID3DXBuffer *pPng = NULL;
            if (SUCCEEDED(D3DXSaveSurfaceToFileInMemory(&pPng, D3DXIFF_PNG, pSurf, NULL, NULL)) && pPng) {
                DWORD pSz = pPng->GetBufferSize();
                if (pSz > 8 && pSz <= 16384) {
                    const uint8_t *pPtr = (const uint8_t *)pPng->GetBufferPointer();
                    outPng.assign(pPtr, pPtr + pSz);
                }
                pPng->Release();
            }
            pSurf->Release();
        }
        XPhysicalFree(physMem);
        if (!outPng.empty()) return;
    }
}

static bool ParseXbeMetadata(DiscStream *disc, uint32_t sec, uint32_t sz, GameMeta &meta) {
    if (sz < 0x200) return false;
    uint8_t hdr[0x200];
    if (!disc->ReadBytes((uint64_t)sec * SECTOR_SIZE, hdr, sizeof(hdr)) || memcmp(hdr, "XBEH", 4) != 0) return false;

    uint32_t baseAddr = Swap32(*(uint32_t *)&hdr[0x104]);
    uint32_t certAddr = Swap32(*(uint32_t *)&hdr[0x118]);
    if (certAddr < baseAddr) return false;
    uint8_t cert[0x100];
    if (!disc->ReadBytes((uint64_t)sec * SECTOR_SIZE + (certAddr - baseAddr), cert, sizeof(cert))) return false;

    meta.titleId = Swap32(*(uint32_t *)&cert[0x08]);
    for (int i = 0; i < 40; ++i) {
        uint16_t ch = Swap16(*(uint16_t *)&cert[0x0C + i * 2]);
        meta.titleName[i] = (wchar_t)ch;
        if (ch == 0) break;
    }
    memset(meta.mediaId, 0, 16);
    *(uint32_t *)meta.mediaId = Swap32(meta.titleId);
    ExtractXbeXtimagePng(disc, sec, sz, hdr, baseAddr, meta.pngIcon);
    meta.discNum = 1; meta.totalDiscs = 1; meta.platform = 1; meta.execType = 1;
    return meta.titleId != 0;
}

static void WriteBE32(uint8_t *dst, uint32_t val) {
    *(dst + 0) = (uint8_t)(val >> 24);
    *(dst + 1) = (uint8_t)(val >> 16);
    *(dst + 2) = (uint8_t)(val >> 8);
    *(dst + 3) = (uint8_t)val;
}
static void WriteBE64(uint8_t *dst, uint64_t val) {
    WriteBE32(dst, (uint32_t)(val >> 32));
    WriteBE32(dst + 4, (uint32_t)val);
}

static uint64_t ComputeUsedXisoBytes(DiscStream *disc, uint32_t rootSector, uint32_t rootSize, uint64_t maxBytes) {
    uint64_t maxEnd = ((uint64_t)rootSector * SECTOR_SIZE) + rootSize;
    std::vector<std::pair<uint32_t, uint32_t> > q;
    q.push_back(std::make_pair(rootSector, rootSize));
    for (size_t idx = 0; idx < q.size() && idx < 4096; ++idx) {
        std::vector<XisoDirEntry> ents;
        if (!ListXisoDirectory(disc, q[idx].first, q[idx].second, ents)) continue;
        for (size_t i = 0; i < ents.size(); ++i) {
            uint64_t endByte = ((uint64_t)ents[i].sector * SECTOR_SIZE) + ents[i].size;
            if (endByte > maxEnd) maxEnd = endByte;
            if (ents[i].isDir && ents[i].size > 0) {
                q.push_back(std::make_pair(ents[i].sector, ents[i].size));
            }
        }
    }
    maxEnd = (maxEnd + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);
    return (maxEnd > 0 && maxEnd < maxBytes) ? maxEnd : maxBytes;
}

static void EncodeBgra64ToPng(const uint8_t *bgra64, std::vector<uint8_t> &outPng) {
    if (!bgra64) return;
    void *physMem = XPhysicalAlloc(64 * 64 * 4, MAXULONG_PTR, 128, PAGE_READWRITE);
    if (!physMem) return;
    uint32_t *dst = (uint32_t *)physMem;
    for (int i = 0; i < 64 * 64; ++i) {
        uint32_t b = bgra64[i * 4 + 0], g = bgra64[i * 4 + 1], r = bgra64[i * 4 + 2], a = bgra64[i * 4 + 3];
        dst[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    IDirect3DTexture9 tex;
    XGSetTextureHeader(64, 64, 1, 0, D3DFMT_LIN_A8R8G8B8, 0, 0, XGHEADER_CONTIGUOUS_MIP_OFFSET, 0, &tex, NULL, NULL);
    XGOffsetResourceAddress(&tex, physMem);
    IDirect3DSurface9 *pSurf = NULL;
    if (SUCCEEDED(tex.GetSurfaceLevel(0, &pSurf)) && pSurf) {
        ID3DXBuffer *pPng = NULL;
        if (SUCCEEDED(D3DXSaveSurfaceToFileInMemory(&pPng, D3DXIFF_PNG, pSurf, NULL, NULL)) && pPng) {
            DWORD pSz = pPng->GetBufferSize();
            if (pSz > 16 && pSz <= 16384) {
                const uint8_t *pPtr = (const uint8_t *)pPng->GetBufferPointer();
                outPng.assign(pPtr, pPtr + pSz);
            }
            pPng->Release();
        }
        pSurf->Release();
    }
    XPhysicalFree(physMem);
}

static void DecodeDds64ToPng(DiscStream *disc, uint32_t ddsSec, uint32_t ddsSz, std::vector<uint8_t> &outPng) {
    if (ddsSz < 128 + 2048) return;
    uint8_t hdr[128];
    if (!disc->ReadBytes((uint64_t)ddsSec * SECTOR_SIZE, hdr, 128) || memcmp(hdr, "DDS ", 4) != 0) return;
    uint32_t h = Swap32(*(uint32_t *)&hdr[12]);
    uint32_t w = Swap32(*(uint32_t *)&hdr[16]);
    uint32_t pfFlags = Swap32(*(uint32_t *)&hdr[80]);
    uint32_t fourCC = *(uint32_t *)&hdr[84];
    uint32_t rgbBits = Swap32(*(uint32_t *)&hdr[88]);
    uint64_t off = 128;
    uint32_t cw = w, ch = h;
    while (cw >= 64 && ch >= 64) {
        uint32_t mipBytes = 0;
        if ((pfFlags & 0x40) && rgbBits == 32) mipBytes = cw * ch * 4;
        else if ((pfFlags & 0x04) && memcmp(&fourCC, "DXT1", 4) == 0) mipBytes = (cw / 4) * (ch / 4) * 8;
        else if ((pfFlags & 0x04) && (memcmp(&fourCC, "DXT3", 4) == 0 || memcmp(&fourCC, "DXT5", 4) == 0)) mipBytes = (cw / 4) * (ch / 4) * 16;
        else return;
        if (cw == 64 && ch == 64 && off + mipBytes <= ddsSz) {
            if ((pfFlags & 0x40) && rgbBits == 32) {
                std::vector<uint8_t> bgra(16384);
                if (disc->ReadBytes((uint64_t)ddsSec * SECTOR_SIZE + off, &bgra.front(), 16384))
                    EncodeBgra64ToPng(&bgra.front(), outPng);
            } else {
                bool isDxt1 = (memcmp(&fourCC, "DXT1", 4) == 0);
                uint32_t blkStride = isDxt1 ? 8 : 16;
                std::vector<uint8_t> dxt(mipBytes), bgra(16384, 0);
                if (disc->ReadBytes((uint64_t)ddsSec * SECTOR_SIZE + off, &dxt.front(), mipBytes)) {
                    for (uint32_t by = 0; by < 16; ++by) {
                        for (uint32_t bx = 0; bx < 16; ++bx) {
                            const uint8_t *blk = &dxt[(by * 16 + bx) * blkStride + (isDxt1 ? 0 : 8)];
                            uint16_t c0 = (uint16_t)blk[0] | ((uint16_t)blk[1] << 8);
                            uint16_t c1 = (uint16_t)blk[2] | ((uint16_t)blk[3] << 8);
                            uint32_t bits = (uint32_t)blk[4] | ((uint32_t)blk[5] << 8) | ((uint32_t)blk[6] << 16) | ((uint32_t)blk[7] << 24);
                            uint8_t pr[4], pg[4], pb[4];
                            pr[0] = (uint8_t)(((c0 >> 11) & 0x1F) * 255 / 31); pg[0] = (uint8_t)(((c0 >> 5) & 0x3F) * 255 / 63); pb[0] = (uint8_t)((c0 & 0x1F) * 255 / 31);
                            pr[1] = (uint8_t)(((c1 >> 11) & 0x1F) * 255 / 31); pg[1] = (uint8_t)(((c1 >> 5) & 0x3F) * 255 / 63); pb[1] = (uint8_t)((c1 & 0x1F) * 255 / 31);
                            pr[2] = (uint8_t)((2 * pr[0] + pr[1]) / 3); pg[2] = (uint8_t)((2 * pg[0] + pg[1]) / 3); pb[2] = (uint8_t)((2 * pb[0] + pb[1]) / 3);
                            pr[3] = (uint8_t)((pr[0] + 2 * pr[1]) / 3); pg[3] = (uint8_t)((pg[0] + 2 * pg[1]) / 3); pb[3] = (uint8_t)((pg[0] + 2 * pb[1]) / 3);
                            for (uint32_t py = 0; py < 4; ++py) {
                                for (uint32_t px = 0; px < 4; ++px) {
                                    uint32_t cd = (bits >> ((py * 4 + px) * 2)) & 3U;
                                    size_t di = ((by * 4 + py) * 64 + (bx * 4 + px)) * 4;
                                    bgra[di + 0] = pb[cd]; bgra[di + 1] = pg[cd]; bgra[di + 2] = pr[cd]; bgra[di + 3] = 0xFF;
                                }
                            }
                        }
                    }
                    EncodeBgra64ToPng(&bgra.front(), outPng);
                }
            }
            return;
        }
        off += mipBytes; cw /= 2; ch /= 2;
    }
}

static void ExtractIconFromDiscAssets(DiscStream *disc, uint32_t rootSector, uint32_t rootSize, std::vector<uint8_t> &nxeIconChunk, std::vector<uint8_t> &pngIcon) {
    if (!nxeIconChunk.empty() || !pngIcon.empty()) return;
    std::vector<std::pair<uint32_t, uint32_t> > dq;
    dq.push_back(std::make_pair(rootSector, rootSize));
    uint32_t ddsSec = 0, ddsSz = 0;
    for (size_t qi = 0; qi < dq.size() && qi < 128 && pngIcon.empty() && nxeIconChunk.empty(); ++qi) {
        std::vector<XisoDirEntry> ents; ListXisoDirectory(disc, dq[qi].first, dq[qi].second, ents);
        for (size_t i = 0; i < ents.size(); ++i) {
            if (ents[i].isDir) { dq.push_back(std::make_pair(ents[i].sector, ents[i].size)); continue; }
            if (_stricmp(ents[i].name.c_str(), "nxeart") == 0 && ents[i].size >= 0x8008) {
                std::vector<uint8_t> hdr(0x8008, 0);
                if (disc->ReadBytes((uint64_t)ents[i].sector * SECTOR_SIZE, &hdr.front(), 0x8008)) {
                    uint32_t sz1 = RdBE32(&hdr.front()), sz2 = RdBE32(&hdr.front() + 4);
                    if (sz1 > 8 && sz1 <= 16384 && sz2 <= 16384 && hdr.at(8) == 0x89 && hdr.at(9) == 0x50) { nxeIconChunk = hdr; return; }
                }
            } else if (_stricmp(ents[i].name.c_str(), "icon.dds") == 0 && ents[i].size > 64) {
                ddsSec = ents[i].sector; ddsSz = ents[i].size;
            }
        }
    }
    if (pngIcon.empty() && nxeIconChunk.empty() && ddsSec > 0 && ddsSz > 0)
        DecodeDds64ToPng(disc, ddsSec, ddsSz, pngIcon);
}
int ConvertToGodStandalone(const char *sourcePath, const char *contentRoot, const char *fallbackTitle, GodLogCallback logFn) {
    DiscStream *disc = NULL;
    DWORD attr = GetFileAttributesA(sourcePath);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        if (logFn) logFn("\x8D" "Error:\x9D" " Source path not found: %s\n", sourcePath);
        return 1;
    }

    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        if (logFn) logFn("Scanning extracted folder: %s...\n", sourcePath);
        disc = new VirtualFolderStream(sourcePath, logFn); if (logFn) { logFn("  Folder scan complete!\n"); fflush(stdout); }
    } else {
        SplitIsoStream *iso = new SplitIsoStream();
        if (!iso->Open(sourcePath)) {
            delete iso;
            if (logFn) logFn("\x8D" "Error:\x9D" " Invalid or unreadable XISO: %s\n", sourcePath);
            return 1;
        }
        disc = iso;
    }

    uint8_t sec32[2048];
    if (!disc->ReadBytes(32 * SECTOR_SIZE, sec32, sizeof(sec32)) || memcmp(sec32, "MICROSOFT*XBOX*MEDIA", 20) != 0) {
        delete disc;
        if (logFn) logFn("\x8D" "Error:\x9D" " Missing XISO volume descriptor\n");
        return 1;
    }

    uint32_t rootSector = Swap32(*(uint32_t *)&sec32[20]);
    uint32_t rootSize = Swap32(*(uint32_t *)&sec32[24]);

    std::string cleanRoot = contentRoot;
    while (!cleanRoot.empty() && (cleanRoot[cleanRoot.size() - 1] == '\\' || cleanRoot[cleanRoot.size() - 1] == '/'))
        cleanRoot.resize(cleanRoot.size() - 1);

    if (logFn) logFn("  Reading XISO root directory...\n");
    std::vector<XisoDirEntry> rootEntries;
    ListXisoDirectory(disc, rootSector, rootSize, rootEntries);

    const XisoDirEntry *contentEntry = NULL;
    const XisoDirEntry *execEntry = NULL;
    bool isXbe = false;

    for (size_t i = 0; i < rootEntries.size(); ++i) {
        if (rootEntries[i].isDir && _stricmp(rootEntries[i].name.c_str(), "Content") == 0) contentEntry = &rootEntries[i];
        else if (!rootEntries[i].isDir && _stricmp(rootEntries[i].name.c_str(), "default.xex") == 0) { execEntry = &rootEntries[i]; isXbe = false; }
        else if (!rootEntries[i].isDir && _stricmp(rootEntries[i].name.c_str(), "default.xbe") == 0) { execEntry = &rootEntries[i]; isXbe = true; }
    }

    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        std::string hostContent0 = std::string(sourcePath);
        if (!hostContent0.empty() && hostContent0[hostContent0.size() - 1] != '\\') hostContent0 += "\\";
        hostContent0 += "Content\\0000000000000000";
        DWORD cAttr = GetFileAttributesA(hostContent0.c_str());
        if (cAttr != INVALID_FILE_ATTRIBUTES && (cAttr & FILE_ATTRIBUTE_DIRECTORY)) {
            if (logFn) logFn("Detected folder Content\\0000000000000000! Installing to %s...\n", cleanRoot.c_str());
            VirtualFolderStream *cStream = new VirtualFolderStream(hostContent0, NULL);
            if (cStream) {
                std::vector<uint8_t> s32v(2048, 0);
                uint8_t *s32 = &s32v.front();
                if (cStream->ReadBytes(32 * SECTOR_SIZE, s32, 2048) && memcmp(s32, "MICROSOFT*XBOX*MEDIA", 20) == 0) {
                    ExtractXisoTree(cStream, Swap32(*(uint32_t *)(s32 + 20)), Swap32(*(uint32_t *)(s32 + 24)), cleanRoot, logFn);
                }
                delete cStream;
            }
        }
    }
    if (contentEntry) {
        std::vector<XisoDirEntry> sub;
        ListXisoDirectory(disc, contentEntry->sector, contentEntry->size, sub);
        for (size_t i = 0; i < sub.size(); ++i) {
            if (sub[i].isDir && strcmp(sub[i].name.c_str(), "0000000000000000") == 0) {
                if (logFn) logFn("Detected DLC/Install Content folder! Extracting packages...\n");
                if (!ExtractXisoTree(disc, sub[i].sector, sub[i].size, cleanRoot, logFn)) {
                    delete disc;
                    return 1;
                }
                std::vector<XisoDirEntry> pkgCheck;
                ListXisoDirectory(disc, sub[i].sector, sub[i].size, pkgCheck);
                if (!pkgCheck.empty() && SumNonContentBytes(disc, rootSector, rootSize, true) < 150ULL * 1024ULL * 1024ULL) {
                    if (logFn) logFn("Install/DLC disc complete (no standalone GOD required).\n");
                    delete disc;
                    return 0;
                }
            }
        }
    }

    XisoDirEntry fallbackXbe;
    if (!execEntry) {
        std::vector<std::pair<uint32_t, uint32_t> > dirQueue;
        dirQueue.push_back(std::make_pair(rootSector, rootSize));
        uint32_t bestXbeSz = 0;
        for (size_t qIdx = 0; qIdx < dirQueue.size() && qIdx < 64; ++qIdx) {
            std::vector<XisoDirEntry> curList;
            ListXisoDirectory(disc, dirQueue[qIdx].first, dirQueue[qIdx].second, curList);
            for (size_t i = 0; i < curList.size(); ++i) {
                if (curList[i].isDir) {
                    dirQueue.push_back(std::make_pair(curList[i].sector, curList[i].size));
                } else {
                    size_t nlen = curList[i].name.size();
                    if (nlen > 4 && _strnicmp(curList[i].name.c_str() + nlen - 4, ".xbe", 4) == 0) {
                        if (!execEntry || _stricmp(curList[i].name.c_str(), "default.xbe") == 0 || curList[i].size > bestXbeSz) {
                            fallbackXbe = curList[i];
                            execEntry = &fallbackXbe;
                            isXbe = true;
                            bestXbeSz = curList[i].size;
                        }
                    }
                }
            }
        }
    }

    std::vector<uint8_t> nxeIconChunk;
    for (size_t i = 0; i < rootEntries.size(); ++i) {
        if (!rootEntries[i].isDir && _stricmp(rootEntries[i].name.c_str(), "nxeart") == 0 && rootEntries[i].size >= 0x971A) {
            nxeIconChunk.resize(0x971A - 0x1712);
            if (!disc->ReadBytes((uint64_t)rootEntries[i].sector * SECTOR_SIZE + 0x1712, &nxeIconChunk.front(), nxeIconChunk.size())) {
                nxeIconChunk.clear();
            }
            break;
        }
    }
    if (logFn) logFn("  Parsing executable metadata...\n");
    GameMeta meta;
    memset(&meta, 0, sizeof(meta));
    if (!execEntry || !(isXbe ? ParseXbeMetadata(disc, execEntry->sector, execEntry->size, meta) : ParseXexMetadata(disc, execEntry->sector, execEntry->size, meta))) {
        if (!execEntry) {
            delete disc;
            if (logFn) logFn("\x8D" "Error:\x9D" " Neither default.xex nor default.xbe found\n");
            return 1;
        }
    ExtractIconFromDiscAssets(disc, rootSector, rootSize, nxeIconChunk, meta.pngIcon);
        meta.titleId = 0x584807D1;
        meta.discNum = 1;
        meta.totalDiscs = 1;
        if (logFn) logFn("Note: Using fallback executable metadata\n");
    }

    std::vector<wchar_t> cleanWV(128, 0);
    wchar_t *cleanW = &cleanWV.front();
    int cwIdx = 0;
    for (int i = 0; i < 127 && meta.titleName[i]; ++i) {
        wchar_t wc = meta.titleName[i];
        if (wc == 0x2122 || wc == 0x00AE || wc == 0x00A9) continue;
        cleanW[cwIdx++] = (wc >= 32 && wc < 0xD800) ? wc : L' ';
    }
    while (cwIdx > 0 && cleanW[cwIdx - 1] == L' ') cleanW[--cwIdx] = 0;
    if (cwIdx == 0 && fallbackTitle) {
        std::string fb = fallbackTitle;
        size_t cut = fb.find_first_of("([.");
        if (cut != std::string::npos) fb = fb.substr(0, cut);
        while (!fb.empty() && (fb[fb.size() - 1] == ' ' || fb[fb.size() - 1] == ',')) fb.resize(fb.size() - 1);
        if (fb.size() > 5 && _stricmp(fb.c_str() + fb.size() - 5, ", The") == 0) {
            fb = "The " + fb.substr(0, fb.size() - 5);
        } else if (fb.size() > 4 && _stricmp(fb.c_str() + fb.size() - 4, " The") == 0) {
            fb = "The " + fb.substr(0, fb.size() - 4);
        }
        for (size_t i = 0; i < 127 && i < fb.size(); ++i) cleanW[cwIdx++] = (wchar_t)(unsigned char)fb[i];
    }
    if (cwIdx == 0) wcscpy(cleanW, L"Xbox Game");
    memcpy(meta.titleName, cleanW, 128 * sizeof(wchar_t));

    std::vector<char> tidHexV(16, 0), midHexV(40, 0), dispTitleV(128, 0);
    char *tidHex = &tidHexV.front(), *midHex = &midHexV.front(), *dispTitle = &dispTitleV.front();
    for (int ti = 0; ti < 127 && meta.titleName[ti]; ++ti) dispTitle[ti] = (meta.titleName[ti] >= 32 && meta.titleName[ti] < 127) ? (char)meta.titleName[ti] : ' ';
    if (!dispTitle) strncpy(dispTitle, "Xbox Game", 127);
    _snprintf(tidHex, 16, "%08X", meta.titleId);
    for (int mi = 0; mi < 16; ++mi) _snprintf(midHex + mi * 2, 3, "%02X", meta.mediaId[mi]);

    std::vector<char> godDirV(512, 0), dataDirV(512, 0), hdrPathV(512, 0);
    char *godDir = &godDirV.front(), *dataDir = &dataDirV.front(), *hdrPath = &hdrPathV.front();
    _snprintf(godDir, 512, "%s\\%s\\%s", cleanRoot.c_str(), tidHex, isXbe ? "00005000" : "00007000");
    _snprintf(dataDir, 512, "%s\\%s.data", godDir, midHex);
    _snprintf(hdrPath, 512, "%s\\%s", godDir, midHex);
    if (logFn) logFn("  Creating output folder: %s...\n", dataDir);
    ForceCreateDirectoryTree(dataDir);

    uint64_t totalBytes = (attr & FILE_ATTRIBUTE_DIRECTORY) ? disc->TotalBytes() : ComputeUsedXisoBytes(disc, rootSector, rootSize, disc->TotalBytes());
    uint64_t totalBlocks = (totalBytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint32_t partCount = (uint32_t)((totalBlocks + DATA_BLOCKS_PER_PART - 1) / DATA_BLOCKS_PER_PART);
    if (partCount == 0) partCount = 1;

    uint8_t *blockBuf = (uint8_t *)malloc((size_t)(BLOCKS_PER_SUBTABLE * BLOCK_SIZE));
    uint8_t *masterTbl = (uint8_t *)malloc((size_t)BLOCK_SIZE);
    uint8_t *subTbls = (uint8_t *)malloc((size_t)(SUBTABLES_PER_PART * BLOCK_SIZE));
    std::vector<uint8_t> allMhts((size_t)partCount * (size_t)BLOCK_SIZE, 0);
    if (!blockBuf || !masterTbl || !subTbls) {
        if (blockBuf) free(blockBuf);
        if (masterTbl) free(masterTbl);
        if (subTbls) free(subTbls);
        delete disc;
        return 1;
    }

    std::vector<uint8_t> topHashV(20, 0);
    uint8_t *topHash = &topHashV.front();
    uint64_t totalContainerBytes = 0;

    if (logFn) logFn("Title: %s [%s] | Parts: %u\n", dispTitle, tidHex, partCount);
    for (uint32_t part = 0; part < partCount; ++part) {
        if (logFn) logFn("  Writing Data%04u (%u of %u)...\n", part, part + 1, partCount);
        std::vector<char> partPathV(512, 0); char *partPath = &partPathV.front();
        _snprintf(partPath, 512, "%s\\Data%04u", dataDir, part);
        HANDLE hPart = CreateFileA(partPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hPart == INVALID_HANDLE_VALUE) { free(blockBuf); free(masterTbl); free(subTbls); delete disc; return 1; }

        memset(masterTbl, 0, (size_t)BLOCK_SIZE);
        memset(subTbls, 0, (size_t)(SUBTABLES_PER_PART * BLOCK_SIZE));

        uint64_t partStart = (uint64_t)part * DATA_BLOCKS_PER_PART;
        uint64_t blocksHere = std::min<uint64_t>(DATA_BLOCKS_PER_PART, totalBlocks - partStart);
        uint32_t subsHere = (uint32_t)((blocksHere + BLOCKS_PER_SUBTABLE - 1) / BLOCKS_PER_SUBTABLE);
        uint64_t hdrBlocks = 1ULL + subsHere; DWORD wHdr = 0; WriteFile(hPart, masterTbl, (DWORD)BLOCK_SIZE, &wHdr, NULL);
        DWORD lastTick = GetTickCount();
        for (uint32_t subIdx = 0; subIdx < subsHere; ++subIdx) {
            uint64_t subStart = (uint64_t)subIdx * BLOCKS_PER_SUBTABLE;
            uint32_t chunkBlocks = (uint32_t)std::min<uint64_t>(BLOCKS_PER_SUBTABLE, blocksHere - subStart);
            size_t chunkBytes = (size_t)chunkBlocks * (size_t)BLOCK_SIZE;
            uint64_t gBlock = partStart + subStart;
            DWORD now = GetTickCount();
            if (logFn && (now - lastTick >= 750 || subIdx + 1 == subsHere)) {
                lastTick = now; uint64_t done = std::min<uint64_t>((gBlock + chunkBlocks) * BLOCK_SIZE, totalBytes);
                unsigned int pct = totalBytes > 0 ? (unsigned int)((done * 100ULL) / totalBytes) : 100;
                logFn("GOD [%s | %s | %s]: %u%% (%.1f / %.1f MB) [Part %u/%u]\n", dispTitle, tidHex, fallbackTitle ? fallbackTitle : sourcePath, pct, done / (1024.0 * 1024.0), totalBytes / (1024.0 * 1024.0), part + 1, partCount);
            }
            DWORD wSub = 0, written = 0;
            if (!disc->ReadBytes(gBlock * BLOCK_SIZE, blockBuf, chunkBytes)) { CloseHandle(hPart); free(blockBuf); free(masterTbl); free(subTbls); delete disc; return 1; }
            uint8_t *subBase = subTbls + ((size_t)subIdx * (size_t)BLOCK_SIZE);
            for (uint32_t eb = 0; eb < chunkBlocks; ++eb) ComputeSha1(blockBuf + ((size_t)eb * (size_t)BLOCK_SIZE), (size_t)BLOCK_SIZE, subBase + ((size_t)eb * 20));
            if (!WriteFile(hPart, subBase, (DWORD)BLOCK_SIZE, &wSub, NULL) || wSub != (DWORD)BLOCK_SIZE || !WriteFile(hPart, blockBuf, (DWORD)chunkBytes, &written, NULL) || written != (DWORD)chunkBytes) { CloseHandle(hPart); free(blockBuf); free(masterTbl); free(subTbls); delete disc; return 1; }
        }
        for (uint32_t s = 0; s < subsHere; ++s) ComputeSha1(subTbls + ((size_t)s * (size_t)BLOCK_SIZE), (size_t)BLOCK_SIZE, masterTbl + ((size_t)s * 20));
        memcpy(&allMhts[(size_t)part * (size_t)BLOCK_SIZE], masterTbl, (size_t)BLOCK_SIZE);
        LARGE_INTEGER li; li.QuadPart = 0; SetFilePointerEx(hPart, li, NULL, FILE_BEGIN); DWORD w1 = 0; WriteFile(hPart, masterTbl, (DWORD)BLOCK_SIZE, &w1, NULL);
        CloseHandle(hPart);
        totalContainerBytes += (hdrBlocks + blocksHere) * BLOCK_SIZE;
    }

    std::vector<uint8_t> nextMhtSha1V(20, 0);
    uint8_t *nextMhtSha1 = &nextMhtSha1V.front();
    for (int p = (int)partCount - 1; p >= 0; --p) {
        uint8_t *mhtPtr = &allMhts[(size_t)p * (size_t)BLOCK_SIZE];
        if (p + 1 < (int)partCount) {
            memcpy(mhtPtr + 203 * 20, nextMhtSha1, 20);
            std::vector<char> partPathV(512, 0); char *partPath = &partPathV.front();
            _snprintf(partPath, 512, "%s\\Data%04d", dataDir, p);
            HANDLE hUpd = CreateFileA(partPath, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hUpd != INVALID_HANDLE_VALUE) {
                DWORD wUpd = 0;
                WriteFile(hUpd, mhtPtr, (DWORD)BLOCK_SIZE, &wUpd, NULL);
                CloseHandle(hUpd);
            }
        }
        ComputeSha1(mhtPtr, (size_t)BLOCK_SIZE, nextMhtSha1);
    }
    memcpy(topHash, nextMhtSha1, 20);

    free(blockBuf); free(masterTbl); free(subTbls); delete disc;

    uint8_t *live = (uint8_t *)calloc(1, 0xB000);
    if (!live) return 1;
    memcpy(live, "LIVE", 4); for (int bOff = 0x04; bOff < 0x104; bOff += 32) memcpy(live + bOff, "ByromWasHereUnlockingYourGODGame", 32); live[0x237] = 0x01; live[0x23B] = 0x01; memset(live + 0x23C, 0xFF, 8); live[0x247] = 0x01;
    memset(live + 0x22C, 0xFF, 8);

    WriteBE32(live + 0x340, 0x0000AD0E);
    WriteBE32(live + 0x344, isXbe ? 0x00005000 : 0x00007000);
    WriteBE32(live + 0x348, 0x00000002);
    WriteBE64(live + 0x34C, 0ULL);
    uint32_t mId32 = *(uint32_t *)meta.mediaId; WriteBE32(live + 0x354, isXbe ? 0U : (mId32 ? mId32 : meta.titleId));
    WriteBE32(live + 0x360, meta.titleId);
    *(live + 0x364) = 0x00;
    *(live + 0x365) = 0x00;
    *(live + 0x366) = meta.discNum > 0 ? meta.discNum : 1;
    *(live + 0x367) = meta.totalDiscs > 0 ? meta.totalDiscs : 1;

    *(live + 0x379) = 0x24;
    *(live + 0x37A) = 0x05;
    *(live + 0x37B) = 0x05;
    *(live + 0x37C) = 0x11;
    memcpy(live + 0x37D, topHash, 20);
    *(live + 0x391) = 0x00;
    *(live + 0x392) = (uint8_t)((totalBlocks >> 16) & 0xFF);
    *(live + 0x393) = (uint8_t)((totalBlocks >> 8) & 0xFF);
    *(live + 0x394) = (uint8_t)(totalBlocks & 0xFF);

    WriteBE32(live + 0x39D, partCount);
    WriteBE64(live + 0x3A1, totalContainerBytes);
    WriteBE32(live + 0x3A9, 0x00000001);

    for (int i = 0; i < 128 && meta.titleName[i]; ++i) {
        uint16_t be = (uint16_t)meta.titleName[i];
        memcpy(live + 0x411 + i * 2, &be, 2);
        memcpy(live + 0x1691 + i * 2, &be, 2);
    }

    if (meta.titleName[0] != 0) {
        memset(live + 0x411, 0, 0x100);
        memset(live + 0x1691, 0, 0x80);
        for (int k = 0; k < 63 && meta.titleName[k]; ++k) {
            uint16_t ch = (uint16_t)meta.titleName[k];
            live[0x411 + k * 2] = (uint8_t)(ch >> 8); live[0x411 + k * 2 + 1] = (uint8_t)(ch & 0xFF);
            live[0x1691 + k * 2] = (uint8_t)(ch >> 8); live[0x1691 + k * 2 + 1] = (uint8_t)(ch & 0xFF);
        }
        if (logFn) logFn("  Updated placeholder title name in LIVE header!\n");
    }
    if (!nxeIconChunk.empty()) {
        memcpy(live + 0x1712, &nxeIconChunk.front(), nxeIconChunk.size());
    } else if (!meta.pngIcon.empty() && meta.pngIcon.size() <= 16384) {
        uint32_t pLen = (uint32_t)meta.pngIcon.size();
        WriteBE32(live + 0x1712, pLen);
        WriteBE32(live + 0x1716, pLen);
        memcpy(live + 0x171A, &meta.pngIcon.front(), pLen);
        memcpy(live + 0x571A, &meta.pngIcon.front(), pLen);
    }

    uint8_t hdrHash[20];
    ComputeSha1(live + 0x344, 0xB000 - 0x344, hdrHash);
    memcpy(live + 0x32C, hdrHash, 20);

    HANDLE hHdr = CreateFileA(hdrPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hHdr == INVALID_HANDLE_VALUE) { free(live); return 1; }
    DWORD hw = 0;
    WriteFile(hHdr, live, 0xB000, &hw, NULL);
    CloseHandle(hHdr);
    free(live);

    if (logFn) logFn("GOD Ready: %s\n", hdrPath);
    return 0;
}

class GodDiscStream : public DiscStream {
    std::string dataDir; uint64_t totalDataBytes; uint32_t cachedPart; HANDLE cachedHandle;
public:
    GodDiscStream() : totalDataBytes(0), cachedPart(0xFFFFFFFF), cachedHandle(INVALID_HANDLE_VALUE) {}
    ~GodDiscStream() { if (cachedHandle != INVALID_HANDLE_VALUE) CloseHandle(cachedHandle); }
    bool Open(const char *hdrPath) {
        HANDLE h = CreateFileA(hdrPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER fsz; if (!GetFileSizeEx(h, &fsz) || fsz.QuadPart != 0xB000) { CloseHandle(h); return false; }
        uint8_t hdr[0x3B0]; DWORD rb = 0; if (!ReadFile(h, hdr, sizeof(hdr), &rb, NULL) || rb != sizeof(hdr)) { CloseHandle(h); return false; } CloseHandle(h);
        if (memcmp(hdr, "LIVE", 4) != 0 && memcmp(hdr, "CON ", 4) != 0 && memcmp(hdr, "PIRS", 4) != 0) return false;
        dataDir = std::string(hdrPath) + ".data"; uint32_t blocks = 0; for (uint32_t p = 0; p < 64; ++p) { std::vector<char> pb(512, 0); _snprintf(&pb.front(), 512, "%s\\Data%04u", dataDir.c_str(), p); if (GetFileAttributesA(&pb.front()) == INVALID_FILE_ATTRIBUTES) break; blocks += (uint32_t)DATA_BLOCKS_PER_PART; }
        if (blocks == 0) return false;
        totalDataBytes = (uint64_t)blocks * BLOCK_SIZE;
        return GetFileAttributesA(dataDir.c_str()) != INVALID_FILE_ATTRIBUTES;
    }
    uint64_t TotalBytes() const { return totalDataBytes; }
    bool ReadBytes(uint64_t offset, void *buf, size_t len) {
        memset(buf, 0, len); if (offset >= totalDataBytes) return true;
        uint8_t *dst = (uint8_t *)buf; size_t rem = len;
        if (offset + rem > totalDataBytes) rem = (size_t)(totalDataBytes - offset);
        while (rem > 0) {
            uint64_t dataBlock = offset / BLOCK_SIZE; size_t blockOff = (size_t)(offset % BLOCK_SIZE);
            uint32_t partIdx = (uint32_t)(dataBlock / DATA_BLOCKS_PER_PART); uint64_t blockInPart = dataBlock % DATA_BLOCKS_PER_PART;
            uint64_t leadHashes = (partIdx == 0) ? 2ULL : 1ULL; uint64_t physOff = (leadHashes + (blockInPart / BLOCKS_PER_SUBTABLE) + blockInPart) * BLOCK_SIZE + blockOff;
            if (cachedPart != partIdx) {
                if (cachedHandle != INVALID_HANDLE_VALUE) { CloseHandle(cachedHandle); cachedHandle = INVALID_HANDLE_VALUE; }
                std::vector<char> pBuf(512, 0); char *pPath = &pBuf.front(); _snprintf(pPath, 512, "%s\\Data%04u", dataDir.c_str(), partIdx);
                cachedHandle = CreateFileA(pPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                if (cachedHandle == INVALID_HANDLE_VALUE) return false; cachedPart = partIdx;
            }
            size_t maxContig = (size_t)((BLOCKS_PER_SUBTABLE - (blockInPart % BLOCKS_PER_SUBTABLE)) * BLOCK_SIZE - blockOff);
            size_t chunk = rem < maxContig ? rem : maxContig;
            LARGE_INTEGER li; li.QuadPart = (LONGLONG)physOff; SetFilePointerEx(cachedHandle, li, NULL, FILE_BEGIN);
            DWORD rb = 0; if (!ReadFile(cachedHandle, dst, (DWORD)chunk, &rb, NULL) || rb != (DWORD)chunk) return false;
            dst += chunk; offset += chunk; rem -= chunk;
        }
        return true;
    }
};

static std::string SanitizeFolderName(const char *raw) {
    std::string out; if (!raw) return "Xbox_Game";
    for (size_t i = 0; raw[i] && out.size() < 42; ++i) {
        char c = raw[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_') out += c;
    }
    while (!out.empty() && out[out.size() - 1] == ' ') out.resize(out.size() - 1);
    return out.empty() ? "Xbox_Game" : out;
}

int ConvertUniversal(const char *sourcePath, const char *driveRoot, int outMode, const char *gamesDirName, const char *xgamesDirName, const char *isosDirName, const char *fallbackTitle, GodLogCallback logFn) {
    DWORD attr = GetFileAttributesA(sourcePath); if (attr == INVALID_FILE_ATTRIBUTES) return 1;
    bool srcIsDir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    size_t slen = strlen(sourcePath);
    bool looksIso = !srcIsDir && ((slen > 4 && _stricmp(sourcePath + slen - 4, ".iso") == 0) || (slen > 8 && _stricmp(sourcePath + slen - 8, ".iso.001") == 0));
    bool srcIsGod = !srcIsDir && !looksIso;

    DiscStream *disc = NULL;
    if (_strnicmp(sourcePath, "Cdrom0:", 7) == 0) { RawCdromStream *cd = new RawCdromStream(); if (!cd->Open(logFn)) { delete cd; return 1; } disc = cd; srcIsDir = false; }
    else if (srcIsDir) { if (logFn) logFn("Scanning folder: %s...\n", sourcePath); disc = new VirtualFolderStream(sourcePath, logFn); }
    else if (srcIsGod) { GodDiscStream *gs = new GodDiscStream(); if (!gs->Open(sourcePath)) { delete gs; return 1; } disc = gs; }
    else { SplitIsoStream *iso = new SplitIsoStream(); if (!iso->Open(sourcePath)) { delete iso; return 1; } disc = iso; }

    std::vector<uint8_t> sec32v(2048, 0); uint8_t *s32 = &sec32v.front(); if (!disc->ReadBytes(32 * SECTOR_SIZE, s32, 2048) || memcmp(s32, "MICROSOFT*XBOX*MEDIA", 20) != 0) { delete disc; return 1; } uint32_t rootSector = Swap32(*(uint32_t *)(s32 + 20)), rootSize = Swap32(*(uint32_t *)(s32 + 24));
    std::vector<XisoDirEntry> rootEntries; ListXisoDirectory(disc, rootSector, rootSize, rootEntries);
    bool isXbe = false; const XisoDirEntry *execEntry = NULL;
    for (size_t i = 0; i < rootEntries.size(); ++i) {
        if (!rootEntries[i].isDir && _stricmp(rootEntries[i].name.c_str(), "default.xex") == 0) { execEntry = &rootEntries[i]; isXbe = false; }
        else if (!rootEntries[i].isDir && _stricmp(rootEntries[i].name.c_str(), "default.xbe") == 0) { execEntry = &rootEntries[i]; isXbe = true; }
    }
    GameMeta meta; memset(&meta, 0, sizeof(meta));
    if (execEntry) { if (isXbe) ParseXbeMetadata(disc, execEntry->sector, execEntry->size, meta); else ParseXexMetadata(disc, execEntry->sector, execEntry->size, meta); }
    char uTidHex[16]; _snprintf(uTidHex, sizeof(uTidHex), "%08X", meta.titleId);
    std::vector<char> aBuf(128, 0); char *asciiTitle = &aBuf.front(); for (int i = 0; i < 127 && meta.titleName[i]; ++i) asciiTitle[i] = (char)(meta.titleName[i] & 0xFF); std::string cleanTitle = SanitizeFolderName(asciiTitle[0] ? asciiTitle : fallbackTitle);
    std::string drv = driveRoot; if (!drv.empty() && drv[drv.size() - 1] != '\\') drv += "\\";

    bool hasDeltaPatchXex = false;
    if (!isXbe) {
        for (size_t i = 0; i < rootEntries.size(); ++i) {
            size_t nlen = rootEntries[i].name.size();
            if (!rootEntries[i].isDir && nlen > 4 && _stricmp(rootEntries[i].name.c_str() + nlen - 4, ".xex") == 0 && rootEntries[i].size >= 0x20) {
                uint8_t xh[0x20] = {0};
                if (disc->ReadBytes((uint64_t)rootEntries[i].sector * SECTOR_SIZE, xh, 0x20) && memcmp(xh, "XEX2", 4) == 0) {
                    uint32_t mFlags = RdBE32(xh + 4);
                    if (mFlags & 0x00000008U) { hasDeltaPatchXex = true; break; }
                }
            }
        }
    }

    int effectiveMode = outMode;
    if (hasDeltaPatchXex && effectiveMode == OUT_MODE_GOD) {
        if (logFn) logFn("Delta-patch XEX detected -> routing to Extracted Folder (%s%s\\%s) for compatibility.\n", drv.c_str(), gamesDirName, cleanTitle.c_str());
        effectiveMode = OUT_MODE_FOLDER;
    }
    if (effectiveMode == OUT_MODE_GOD) { delete disc; std::string cRoot = drv + "Content\\0000000000000000"; return ConvertToGodStandalone(sourcePath, cRoot.c_str(), fallbackTitle, logFn); }
    if (effectiveMode == OUT_MODE_FOLDER) {
        if (srcIsDir) { delete disc; if (logFn) logFn("Already an extracted folder (%s)\n", sourcePath); return 0; }
        std::string targetFolder = drv + (isXbe ? xgamesDirName : gamesDirName) + "\\" + cleanTitle;
        if (logFn) logFn("=== Title: %s [%s] ===\nExtracting to: %s...\n", cleanTitle.c_str(), uTidHex, targetFolder.c_str());
        bool ok = ExtractXisoTree(disc, rootSector, rootSize, targetFolder, logFn); delete disc; return ok ? 0 : 1;
    }
    std::string isoDir = drv + isosDirName; ForceCreateDirectoryTree(isoDir.c_str());
    uint64_t totalIsoBytes = (srcIsDir || srcIsGod) ? disc->TotalBytes() : ComputeUsedXisoBytes(disc, rootSector, rootSize, disc->TotalBytes());
    bool splitParts = (_strnicmp(drv.c_str(), "Usb", 3) == 0) && (totalIsoBytes > 4000ULL * 1024ULL * 1024ULL);
    uint8_t *buf = (uint8_t *)malloc(512 * 1024); if (!buf) { delete disc; return 1; }
    uint64_t off = 0, partWritten = 0; uint32_t partIdx = 1; HANDLE hOut = INVALID_HANDLE_VALUE; DWORD lastTick = GetTickCount();
    while (off < totalIsoBytes) {
        if (hOut == INVALID_HANDLE_VALUE || (splitParts && partWritten >= 4000ULL * 1024ULL * 1024ULL)) {
            if (hOut != INVALID_HANDLE_VALUE) { CloseHandle(hOut); partIdx++; partWritten = 0; }
            std::vector<char> oBuf(512, 0); char *outP = &oBuf.front(); if (splitParts) _snprintf(outP, 512, "%s\\%s.iso.%03u", isoDir.c_str(), cleanTitle.c_str(), partIdx); else _snprintf(outP, sizeof(outP), "%s\\%s.iso", isoDir.c_str(), cleanTitle.c_str());
            hOut = CreateFileA(outP, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); if (hOut == INVALID_HANDLE_VALUE) { free(buf); delete disc; return 1; }
        }
        size_t chunk = (totalIsoBytes - off) > (512 * 1024) ? (512 * 1024) : (size_t)(totalIsoBytes - off);
        DWORD written = 0; if (!disc->ReadBytes(off, buf, chunk) || !WriteFile(hOut, buf, (DWORD)chunk, &written, NULL) || written != (DWORD)chunk) { CloseHandle(hOut); free(buf); delete disc; return 1; }
        off += chunk; partWritten += chunk;
        if (logFn && (GetTickCount() - lastTick >= 750 || off == totalIsoBytes)) { lastTick = GetTickCount(); logFn("XISO [%s | %s]: %u%% (%.1f / %.1f MB)\n", cleanTitle.c_str(), uTidHex, (unsigned int)((off * 100ULL) / totalIsoBytes), off / (1024.0 * 1024.0), totalIsoBytes / (1024.0 * 1024.0)); }
    }
    if (hOut != INVALID_HANDLE_VALUE) CloseHandle(hOut); free(buf); delete disc; return 0;
}


int PatchGodStorageIcon(const char *hdrPath, GodLogCallback logFn) {
    HANDLE h = CreateFileA(hdrPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { if (logFn) logFn("  Error: Cannot open %s\n", hdrPath); return 1; }
    LARGE_INTEGER fsz;
    if (!GetFileSizeEx(h, &fsz) || fsz.QuadPart != 0xB000) { CloseHandle(h); return 1; }
    std::vector<uint8_t> liveV(0xB000, 0);
    uint8_t *live = &liveV.front();
    DWORD rb = 0;
    if (!ReadFile(h, live, 0xB000, &rb, NULL) || rb != 0xB000) { CloseHandle(h); return 1; }
    CloseHandle(h);
    if (memcmp(live, "LIVE", 4) != 0 && memcmp(live, "CON ", 4) != 0 && memcmp(live, "PIRS", 4) != 0) return 1;

    GodDiscStream *disc = new GodDiscStream();
    if (!disc->Open(hdrPath)) { delete disc; if (logFn) logFn("  Error: Cannot open .data for %s\n", hdrPath); return 1; }
    std::vector<uint8_t> s32V(2048, 0);
    uint8_t *s32 = &s32V.front();
    if (!disc->ReadBytes(32 * SECTOR_SIZE, s32, 2048) || memcmp(s32, "MICROSOFT*XBOX*MEDIA", 20) != 0) { delete disc; if (logFn) logFn("  Error: Missing XISO header in .data\n"); return 1; }
    uint32_t rootSector = Swap32(*(uint32_t *)(s32 + 20)), rootSize = Swap32(*(uint32_t *)(s32 + 24));
    std::vector<XisoDirEntry> rootEntries;
    ListXisoDirectory(disc, rootSector, rootSize, rootEntries);

    bool isXbe = false;
    const XisoDirEntry *execEntry = NULL;
    const XisoDirEntry *nxeEntry = NULL;
    for (size_t i = 0; i < rootEntries.size(); ++i) {
        if (!rootEntries[i].isDir && _stricmp(rootEntries[i].name.c_str(), "default.xex") == 0) { execEntry = &rootEntries[i]; isXbe = false; }
        else if (!rootEntries[i].isDir && _stricmp(rootEntries[i].name.c_str(), "default.xbe") == 0) { execEntry = &rootEntries[i]; isXbe = true; }
        else if (!rootEntries[i].isDir && _stricmp(rootEntries[i].name.c_str(), "nxeart") == 0) { nxeEntry = &rootEntries[i]; }
    }

    GameMeta meta;
    meta.titleId = 0; meta.platform = 0; meta.execType = 0; meta.discNum = 1; meta.totalDiscs = 1;
    memset(meta.mediaId, 0, sizeof(meta.mediaId));
    memset(meta.titleName, 0, sizeof(meta.titleName));
    if (execEntry) {
        if (isXbe) ParseXbeMetadata(disc, execEntry->sector, execEntry->size, meta);
        else ParseXexMetadata(disc, execEntry->sector, execEntry->size, meta);
    }

    std::vector<uint8_t> nxeIconChunk;
    ExtractIconFromDiscAssets(disc, rootSector, rootSize, nxeIconChunk, meta.pngIcon);
    if (nxeEntry && nxeEntry->size >= 0x8008) {
        std::vector<uint8_t> nxeBuf(0x8008, 0);
        uint8_t *np = &nxeBuf.front();
        if (disc->ReadBytes((uint64_t)nxeEntry->sector * SECTOR_SIZE, np, 0x8008)) {
            uint32_t sz1 = RdBE32(np), sz2 = RdBE32(np + 4);
            if (sz1 > 8 && sz1 <= 16384 && sz2 <= 16384 && *(np + 8) == 0x89 && *(np + 9) == 0x50) nxeIconChunk = nxeBuf;
            if (meta.titleName == 0 && nxeEntry->size >= 0x8208) {
                std::vector<uint8_t> nxeTail(0x200, 0);
                if (disc->ReadBytes((uint64_t)nxeEntry->sector * SECTOR_SIZE + 0x8008, &nxeTail.front(), 0x200)) {
                    for (int k = 0; k < 127; ++k) {
                        uint16_t ch = ((uint16_t)nxeTail[k * 2] << 8) | (uint16_t)nxeTail[k * 2 + 1];
                        if (ch == 0) break;
                        meta.titleName[k] = (wchar_t)ch;
                    }
                }
            }
        }
    }
    std::string liveTitle;
    for (int k = 0; k < 63; ++k) {
        uint16_t c = RdBE16(live + 0x411 + k * 2);
        if (!c) break;
        liveTitle += (c >= 32 && c < 127) ? (char)c : '?';
    }
    bool hasRealTitle = (meta.titleName[0] != 0) || (!liveTitle.empty() && _strnicmp(liveTitle.c_str(), "Xbox_Game", 9) != 0 && _stricmp(liveTitle.c_str(), "Unknown Game") != 0);
    bool hasRealIcon = (!nxeIconChunk.empty() || !meta.pngIcon.empty());
    if (!hasRealTitle || !hasRealIcon) {
        if (logFn) logFn("  [NEEDS SAVE DATA] Launch game once to create save data, then re-run Patch!\n");
        std::string gName;
        if (meta.titleName[0] != 0) { for (int k = 0; k < 127 && meta.titleName[k]; ++k) gName += (char)(meta.titleName[k] & 0x7F); }
        if (gName.empty()) gName = liveTitle;
        g_needSaveTitles.push_back(!gName.empty() ? gName : hdrPath);
        return 2;
    }
    if (logFn) logFn("  Patched NXE Storage title & icon cleanly in <1s!\n");
    return 0;
}
































std::vector<std::string> g_storeNeedSaveTitles;
IDirect3DDevice9 *g_pSharedD3DDevice = NULL;
extern "C" void dprintf(const char *fmt, ...);
int convertPathToGod(const char *s, const char *c, const char *f) { if (!s || !c) return 1; std::string d = c; size_t col = d.find(':'); if (col != std::string::npos) d = d.substr(0, col + 1) + "\\"; int rc = ConvertUniversal(s, d.c_str(), OUT_MODE_GOD, "GAMES", "XGAMES", "ISOS", f, dprintf); if (rc == 2) g_storeNeedSaveTitles.push_back(f && *f ? f : s); return (rc == 0 || rc == 2) ? 0 : 1; }

