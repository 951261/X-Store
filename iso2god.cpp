#include <xtl.h>
#include "inc/bearssl_hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <vector>
#include <string>
#include <algorithm>

#include "OutputConsole.h"
#include "file-stuff.h"
#include "win32/dirent.h"
#include "settings.h"
#include "iso2god.h"

#define SECTOR_SIZE 2048ULL
#define BLOCK_SIZE 4096ULL
#define BLOCKS_PER_SUBTABLE 204ULL
#define SUBTABLES_PER_PART 204ULL
#define DATA_BLOCKS_PER_PART (BLOCKS_PER_SUBTABLE * SUBTABLES_PER_PART)

static inline uint16_t Swap16(uint16_t v) { return _byteswap_ushort(v); }
static inline uint32_t Swap32(uint32_t v) { return _byteswap_ulong(v); }
static inline uint64_t Swap64(uint64_t v) { return _byteswap_uint64(v); }

struct GameMeta {
    uint32_t titleId;
    uint8_t mediaId[16];
    uint8_t discNum;
    uint8_t totalDiscs;
    uint8_t platform;
    uint8_t execType;
    wchar_t titleName[128];
    bool isOriginalXbox;
};

class DiscStream {
public:
    virtual ~DiscStream() {}
    virtual uint64_t TotalBytes() const = 0;
    virtual bool ReadBytes(uint64_t offset, void *buf, size_t len) = 0;
};

struct IsoPart {
    int fd;
    uint64_t startOffset;
    uint64_t length;
};

class IsoFileStream : public DiscStream {
    std::vector<IsoPart> parts;
    uint64_t baseOffset;
    uint64_t sizeBytes;
public:
    IsoFileStream(const std::vector<IsoPart> &p, uint64_t base, uint64_t totalSz)
        : parts(p), baseOffset(base), sizeBytes(totalSz > base ? totalSz - base : 0) {}
    ~IsoFileStream() {
        for (size_t i = 0; i < parts.size(); ++i) {
            if (parts[i].fd != -1) customClose(parts[i].fd);
        }
    }
    void SetBaseOffset(uint64_t base) { uint64_t raw = baseOffset + sizeBytes; baseOffset = base; sizeBytes = raw > base ? raw - base : 0; }
    uint64_t TotalBytes() const { return sizeBytes; }
    bool ReadBytes(uint64_t offset, void *buf, size_t len) {
        memset(buf, 0, len);
        if (offset >= sizeBytes) return true;
        size_t toRead = len;
        if (offset + toRead > sizeBytes) toRead = (size_t)(sizeBytes - offset);
        uint64_t absPos = baseOffset + offset;
        uint8_t *dst = (uint8_t *)buf;
        size_t rem = toRead;

        for (size_t i = 0; i < parts.size() && rem > 0; ++i) {
            uint64_t pEnd = parts[i].startOffset + parts[i].length;
            if (absPos >= pEnd) continue;
            uint64_t localOff = absPos - parts[i].startOffset;
            uint64_t avail = pEnd - absPos;
            size_t chunk = (rem < avail) ? rem : (size_t)avail;
            if (customLseek(parts[i].fd, (long long)localOff, SEEK_SET) < 0) return false;
            long long r = customRead(parts[i].fd, dst, (long long)chunk);
            if (r != (long long)chunk) return false;
            dst += chunk;
            absPos += chunk;
            rem -= chunk;
        }
        return rem == 0;
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
    std::string la = a.name, lb = b.name;
    for (size_t i = 0; i < la.size(); ++i) la[i] = (char)toupper((unsigned char)la[i]);
    for (size_t i = 0; i < lb.size(); ++i) lb[i] = (char)toupper((unsigned char)lb[i]);
    return la < lb;
}

static void ScanDirRecursive(const std::string &dirPath, VEntry &node) {
    DIR *d = opendir(dirPath.c_str());
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        std::string childPath = dirPath;
        if (!childPath.empty() && childPath[childPath.size() - 1] != '\\' && childPath[childPath.size() - 1] != '/')
            childPath += "\\";
        childPath += ent->d_name;

        VEntry child;
        child.name = ent->d_name;
        child.hostPath = childPath;
        child.size = 0;
        child.startSector = 0;

        DIR *sub = opendir(childPath.c_str());
        if (sub) {
            closedir(sub);
            child.isDir = true;
            ScanDirRecursive(childPath, child);
        } else {
            child.isDir = false;
            FILE *fp = fopen(childPath.c_str(), "rb");
            if (fp) {
                _fseeki64(fp, 0, SEEK_END);
                __int64 sz = _ftelli64(fp);
                if (sz > 0) child.size = (uint32_t)sz;
                fclose(fp);
            }
        }
        node.children.push_back(child);
    }
    closedir(d);
    std::sort(node.children.begin(), node.children.end(), VEntryLess);
}

static void BuildDirTableBalanced(const std::vector<VEntry> &items, int lo, int hi, std::vector<uint8_t> &buf, std::vector<uint32_t> &sectorPatchOffsets) {
    if (lo > hi) return;
    int mid = (lo + hi) / 2;
    size_t entryPos = buf.size();
    uint8_t nameLen = (uint8_t)std::min<size_t>(items[mid].name.size(), 255);
    size_t recSize = (14 + nameLen + 3) & ~3U;

    if ((entryPos / SECTOR_SIZE) != ((entryPos + recSize - 1) / SECTOR_SIZE)) {
        size_t nextSec = ((entryPos / SECTOR_SIZE) + 1) * (size_t)SECTOR_SIZE;
        buf.resize(nextSec, 0xFF);
        entryPos = buf.size();
    }

    buf.resize(entryPos + recSize, 0xFF);
    uint8_t *p = &buf[entryPos];
    memset(p, 0, 14);
    p[12] = items[mid].isDir ? 0x10 : 0x00;
    p[13] = nameLen;
    memcpy(p + 14, items[mid].name.data(), nameLen);

    sectorPatchOffsets[mid] = (uint32_t)(entryPos + 4);
    *(uint32_t *)(p + 8) = Swap32(items[mid].size);

    if (lo <= mid - 1) {
        *(uint16_t *)(&buf[entryPos]) = Swap16((uint16_t)(buf.size() / 4));
        BuildDirTableBalanced(items, lo, mid - 1, buf, sectorPatchOffsets);
    }
    if (mid + 1 <= hi) {
        *(uint16_t *)(&buf[entryPos + 2]) = Swap16((uint16_t)(buf.size() / 4));
        BuildDirTableBalanced(items, mid + 1, hi, buf, sectorPatchOffsets);
    }
}

struct VExtent {
    uint64_t startByte;
    uint64_t length;
    int type;
    const VEntry *entry;
};

class VirtualXisoStream : public DiscStream {
    VEntry root;
    uint8_t headerSectors[33 * 2048];
    std::vector<VExtent> extents;
    uint64_t totalBytes;

    void LayoutNode(VEntry &node, uint32_t &curSector) {
        if (node.children.empty()) {
            node.dirTable.resize(2048, 0);
            node.startSector = curSector;
            node.size = 2048;
            VExtent ex = { (uint64_t)curSector * SECTOR_SIZE, 2048, 1, &node };
            extents.push_back(ex);
            curSector += 1;
            return;
        }
        std::vector<uint32_t> patchOffs(node.children.size(), 0);
        BuildDirTableBalanced(node.children, 0, (int)node.children.size() - 1, node.dirTable, patchOffs);
        size_t padded = (node.dirTable.size() + 2047) & ~2047U;
        node.dirTable.resize(padded, 0xFF);
        node.startSector = curSector;
        node.size = (uint32_t)padded;
        VExtent ex = { (uint64_t)curSector * SECTOR_SIZE, padded, 1, &node };
        extents.push_back(ex);
        curSector += (uint32_t)(padded / SECTOR_SIZE);

        for (size_t i = 0; i < node.children.size(); ++i) {
            if (node.children[i].isDir) {
                LayoutNode(node.children[i], curSector);
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
            *(uint32_t *)(&node.dirTable[patchOffs[i]]) = Swap32(node.children[i].startSector);
        }
    }

public:
    VirtualXisoStream(const std::string &folder) {
        memset(headerSectors, 0, sizeof(headerSectors));
        root.isDir = true;
        ScanDirRecursive(folder, root);
        uint32_t curSector = 33;
        LayoutNode(root, curSector);

        uint8_t *sec32 = headerSectors + (32 * 2048);
        memcpy(sec32, "MICROSOFT*XBOX*MEDIA", 20);
        *(uint32_t *)(sec32 + 20) = Swap32(root.startSector);
        *(uint32_t *)(sec32 + 24) = Swap32(root.size);
        memcpy(sec32 + 2028, "MICROSOFT*XBOX*MEDIA", 20);
        totalBytes = (uint64_t)curSector * SECTOR_SIZE;
    }

    uint64_t TotalBytes() const { return totalBytes; }

    bool ReadBytes(uint64_t offset, void *buf, size_t len) {
        memset(buf, 0, len);
        if (offset >= totalBytes) return true;
        uint8_t *dst = (uint8_t *)buf;
        uint64_t end = std::min<uint64_t>(offset + len, totalBytes);

        if (offset < sizeof(headerSectors)) {
            uint64_t hEnd = std::min<uint64_t>(end, sizeof(headerSectors));
            memcpy(dst, headerSectors + offset, (size_t)(hEnd - offset));
        }

        for (size_t i = 0; i < extents.size(); ++i) {
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
                FILE *fp = fopen(ex.entry->hostPath.c_str(), "rb");
                if (!fp) return false;
                _fseeki64(fp, (__int64)srcOff, SEEK_SET);
                fread(dst + dstOff, 1, chunk, fp);
                fclose(fp);
            }
        }
        return true;
    }
};

#include "god_content_helper.h"

static bool FindFileInXiso(DiscStream *disc, uint32_t dirSector, uint32_t dirSize, const char *targetName, uint32_t &outSector, uint32_t &outSize) {
    if (dirSize == 0 || dirSize > 16 * 1024 * 1024) return false;
    std::vector<uint8_t> table(dirSize);
    if (!disc->ReadBytes((uint64_t)dirSector * SECTOR_SIZE, &table[0], dirSize)) return false;

    std::vector<uint32_t> stack;
    stack.push_back(0);
    while (!stack.empty()) {
        uint32_t off = stack.back();
        stack.pop_back();
        if (off == 0xFFFF * 4 || off + 14 > dirSize) continue;

        uint16_t left = Swap16(*(uint16_t *)&table[off]);
        uint16_t right = Swap16(*(uint16_t *)&table[off + 2]);
        uint32_t sec = Swap32(*(uint32_t *)&table[off + 4]);
        uint32_t sz = Swap32(*(uint32_t *)&table[off + 8]);
        uint8_t attr = table[off + 12];
        uint8_t nlen = table[off + 13];
        if (off + 14 + nlen > dirSize) continue;

        if (left != 0 && left != 0xFFFF) stack.push_back((uint32_t)left * 4);
        if (right != 0 && right != 0xFFFF) stack.push_back((uint32_t)right * 4);

        if (!(attr & 0x10) && nlen == strlen(targetName) && _strnicmp((const char *)&table[off + 14], targetName, nlen) == 0) {
            outSector = sec;
            outSize = sz;
            return true;
        }
    }
    return false;
}

static bool FindAnyXbeRecursive(DiscStream *disc, uint32_t dirSector, uint32_t dirSize, int depth, bool &foundDefault, uint32_t &bestSize, uint32_t &outSector, uint32_t &outSize) {
    if (depth > 4) return foundDefault || bestSize > 0;
    std::vector<XisoDirEntry> entries;
    if (!ListXisoDirectory(disc, dirSector, dirSize, entries)) return foundDefault || bestSize > 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].isDir) {
            size_t len = entries[i].name.size();
            if (len > 4 && _strnicmp(entries[i].name.c_str() + len - 4, ".xbe", 4) == 0) {
                bool isDef = (_stricmp(entries[i].name.c_str(), "default.xbe") == 0);
                if (isDef && !foundDefault) {
                    foundDefault = true;
                    bestSize = entries[i].size;
                    outSector = entries[i].sector;
                    outSize = entries[i].size;
                } else if (!foundDefault && entries[i].size > bestSize) {
                    bestSize = entries[i].size;
                    outSector = entries[i].sector;
                    outSize = entries[i].size;
                }
            }
        }
    }
    if (!foundDefault) {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].isDir) {
                FindAnyXbeRecursive(disc, entries[i].sector, entries[i].size, depth + 1, foundDefault, bestSize, outSector, outSize);
            }
        }
    }
    return foundDefault || bestSize > 0;
}

static bool FindAnyXbeInXiso(DiscStream *disc, uint32_t dirSector, uint32_t dirSize, uint32_t &outSector, uint32_t &outSize) {
    bool foundDefault = false;
    uint32_t bestSize = 0;
    return FindAnyXbeRecursive(disc, dirSector, dirSize, 0, foundDefault, bestSize, outSector, outSize);
}

static bool ParseXexMetadata(DiscStream *disc, uint32_t xexSector, uint32_t xexSize, GameMeta &meta) {
    if (xexSize < 0x100) return false;
    size_t hdrRead = std::min<uint32_t>(xexSize, 65536);
    std::vector<uint8_t> buf(hdrRead);
    if (!disc->ReadBytes((uint64_t)xexSector * SECTOR_SIZE, &buf[0], hdrRead)) return false;
    if (memcmp(&buf[0], "XEX2", 4) != 0) return false;

    uint32_t certOff = Swap32(*(uint32_t *)&buf[0x10]);
    uint32_t optCount = Swap32(*(uint32_t *)&buf[0x14]);

    if (certOff + 0x180 <= hdrRead) {
        memcpy(meta.mediaId, &buf[certOff + 0x154], 16);
    }

    for (uint32_t i = 0; i < optCount && 0x18 + i * 8 + 8 <= hdrRead; ++i) {
        uint32_t id = Swap32(*(uint32_t *)&buf[0x18 + i * 8]);
        uint32_t val = Swap32(*(uint32_t *)&buf[0x18 + i * 8 + 4]);
        if (id == 0x00040006 && val + 0x18 <= hdrRead) {
            memcpy(meta.mediaId, &buf[val + 0x00], 4);
            meta.titleId = Swap32(*(uint32_t *)&buf[val + 0x0C]);
            meta.platform = buf[val + 0x10];
            meta.execType = buf[val + 0x11];
            meta.discNum = buf[val + 0x12];
            meta.totalDiscs = buf[val + 0x13];
        } else if (id == 0x00040404 && val + 0x100 <= hdrRead) {
            for (int k = 0; k < 127; ++k) {
                uint16_t ch = Swap16(*(uint16_t *)&buf[val + k * 2]);
                meta.titleName[k] = (wchar_t)ch;
                if (ch == 0) break;
            }
        }
    }
    if (meta.discNum == 0) meta.discNum = 1;
    if (meta.totalDiscs == 0) meta.totalDiscs = 1;
    meta.isOriginalXbox = false;
    return meta.titleId != 0;
}

static bool ParseXbeMetadata(DiscStream *disc, uint32_t xbeSector, uint32_t xbeSize, GameMeta &meta) {
    if (xbeSize < 0x200) return false;
    uint8_t hdr[0x200];
    if (!disc->ReadBytes((uint64_t)xbeSector * SECTOR_SIZE, hdr, sizeof(hdr))) return false;
    if (memcmp(hdr, "XBEH", 4) != 0) return false;

    uint32_t baseAddr = Swap32(*(uint32_t *)&hdr[0x104]);
    uint32_t certAddr = Swap32(*(uint32_t *)&hdr[0x118]);
    if (certAddr < baseAddr) return false;
    uint32_t certOff = certAddr - baseAddr;

    uint8_t cert[0x100];
    if (!disc->ReadBytes((uint64_t)xbeSector * SECTOR_SIZE + certOff, cert, sizeof(cert))) return false;
    meta.titleId = Swap32(*(uint32_t *)&cert[0x08]);
    for (int i = 0; i < 40; ++i) {
        uint16_t ch = Swap16(*(uint16_t *)&cert[0x0C + i * 2]);
        meta.titleName[i] = (wchar_t)ch;
        if (ch == 0) break;
    }
    memset(meta.mediaId, 0, 16);
    *(uint32_t *)meta.mediaId = Swap32(meta.titleId);
    meta.discNum = 1;
    meta.totalDiscs = 1;
    meta.platform = 1;
    meta.execType = 1;
    meta.isOriginalXbox = true;
    return meta.titleId != 0;
}

static void ComputeSha1(const void *data, size_t len, uint8_t *outHash) {
    br_sha1_context ctx;
    br_sha1_init(&ctx);
    br_sha1_update(&ctx, data, len);
    br_sha1_out(&ctx, outHash);
}

static void WriteBigEndian32(uint8_t *dst, uint32_t val) { *(uint32_t *)dst = Swap32(val); }
static void WriteBigEndian64(uint8_t *dst, uint64_t val) { *(uint64_t *)dst = Swap64(val); }

int convertPathToGod(const char *sourcePath, const char *contentRoot, const char *fallbackTitleName) {
    if (!sourcePath || !*sourcePath || !contentRoot || !*contentRoot) {
        dprintf("GOD Error: Invalid source or destination path\n");
        return EXIT_FAILURE;
    }

    DiscStream *disc = NULL;
    DIR *dirCheck = opendir(sourcePath);
    if (dirCheck) {
        closedir(dirCheck);
        dprintf("GOD: Scanning extracted game folder %s...\n", sourcePath);
        disc = new VirtualXisoStream(sourcePath);
    } else {
        std::vector<IsoPart> parts;
        uint64_t totalLen = 0;
        std::string srcStr = sourcePath;
        size_t slen = srcStr.size();
        bool isSplit = (slen > 8 && _stricmp(srcStr.c_str() + slen - 8, ".iso.001") == 0);
        if (isSplit) {
            std::string prefix = srcStr.substr(0, slen - 3);
            for (int idx = 1; idx <= 32; ++idx) {
                char partPath[512];
                _snprintf(partPath, sizeof(partPath), "%s%03d", prefix.c_str(), idx);
                int pfd = customOpen(partPath, 0x8000, 0);
                if (pfd == -1) break;
                long long plen = customLseek(pfd, 0, SEEK_END);
                if (plen <= 0) { customClose(pfd); break; }
                IsoPart ip = { pfd, totalLen, (uint64_t)plen };
                parts.push_back(ip);
                totalLen += (uint64_t)plen;
            }
        } else {
            int pfd = customOpen(sourcePath, 0x8000, 0);
            if (pfd != -1) {
                long long plen = customLseek(pfd, 0, SEEK_END);
                if (plen > 0) {
                    IsoPart ip = { pfd, 0ULL, (uint64_t)plen };
                    parts.push_back(ip);
                    totalLen = (uint64_t)plen;
                } else {
                    customClose(pfd);
                }
            }
        }
        if (parts.empty() || totalLen == 0) {
            dprintf("GOD Error: Could not open %s\n", sourcePath);
            return EXIT_FAILURE;
        }

        IsoFileStream *isoStream = new IsoFileStream(parts, 0ULL, totalLen);
        const uint64_t candidateOffsets[4] = { 0ULL, 0x2080000ULL, 0xFD90000ULL, 0x18300000ULL };
        uint64_t detectedBase = 0;
        bool foundMagic = false;
        char magic[20] = {0};

        for (size_t i = 0; i < 4; ++i) {
            if (totalLen > candidateOffsets[i] + 32 * SECTOR_SIZE + 20) {
                if (isoStream->ReadBytes(candidateOffsets[i] + 32 * SECTOR_SIZE, magic, 20) && memcmp(magic, "MICROSOFT*XBOX*MEDIA", 20) == 0) {
                    detectedBase = candidateOffsets[i];
                    foundMagic = true;
                    break;
                }
            }
        }

        if (!foundMagic) {
            delete isoStream;
            dprintf("GOD Error: Not a valid XISO image\n");
            return EXIT_FAILURE;
        }

        isoStream->SetBaseOffset(detectedBase);
        disc = isoStream;
    }

    uint8_t sec32[2048];
    if (!disc->ReadBytes(32 * SECTOR_SIZE, sec32, sizeof(sec32)) || memcmp(sec32, "MICROSOFT*XBOX*MEDIA", 20) != 0) {
        delete disc;
        dprintf("GOD Error: Failed to read XISO volume descriptor\n");
        return EXIT_FAILURE;
    }

    uint32_t rootSector = Swap32(*(uint32_t *)&sec32[20]);
    uint32_t rootSize = Swap32(*(uint32_t *)&sec32[24]);

    std::string cleanContentRoot = contentRoot;
    while (!cleanContentRoot.empty() && (cleanContentRoot[cleanContentRoot.size() - 1] == '\\' || cleanContentRoot[cleanContentRoot.size() - 1] == '/'))
        cleanContentRoot.resize(cleanContentRoot.size() - 1);

    bool skipGodCreation = false;
    if (!HandleEmbeddedContentFolder(disc, rootSector, rootSize, cleanContentRoot, skipGodCreation)) {
        delete disc;
        return EXIT_FAILURE;
    }
    if (skipGodCreation) {
        delete disc;
        return EXIT_SUCCESS;
    }

    GameMeta meta;
    memset(&meta, 0, sizeof(meta));
    uint32_t execSec = 0, execSz = 0;

    if (FindFileInXiso(disc, rootSector, rootSize, "default.xex", execSec, execSz)) {
        if (!ParseXexMetadata(disc, execSec, execSz, meta)) {
            delete disc;
            dprintf("GOD Error: Failed to parse default.xex metadata\n");
            return EXIT_FAILURE;
        }
    } else if (FindAnyXbeInXiso(disc, rootSector, rootSize, execSec, execSz)) {
        if (!ParseXbeMetadata(disc, execSec, execSz, meta)) {
            delete disc;
            dprintf("GOD Error: Failed to parse XBE metadata\n");
            return EXIT_FAILURE;
        }
    } else {
        delete disc;
        dprintf("GOD Error: Neither default.xex nor any .xbe found (rootSec=%u, rootSz=%u)\n", rootSector, rootSize);
        return EXIT_FAILURE;
    }

    if (meta.titleName[0] == 0 && fallbackTitleName && *fallbackTitleName) {
        for (size_t i = 0; i < 127 && fallbackTitleName[i]; ++i) {
            meta.titleName[i] = (wchar_t)(unsigned char)fallbackTitleName[i];
        }
    }
    if (meta.titleName[0] == 0) wcscpy(meta.titleName, L"Xbox Game");

    std::string base = contentRoot;
    while (!base.empty() && (base[base.size() - 1] == '\\' || base[base.size() - 1] == '/'))
        base.resize(base.size() - 1);

    char titleIdHex[16];
    _snprintf(titleIdHex, sizeof(titleIdHex), "%08X", meta.titleId);

    char mediaIdHex[40];
    _snprintf(mediaIdHex, sizeof(mediaIdHex), "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
        meta.mediaId[0], meta.mediaId[1], meta.mediaId[2], meta.mediaId[3],
        meta.mediaId[4], meta.mediaId[5], meta.mediaId[6], meta.mediaId[7],
        meta.mediaId[8], meta.mediaId[9], meta.mediaId[10], meta.mediaId[11],
        meta.mediaId[12], meta.mediaId[13], meta.mediaId[14], meta.mediaId[15]);

    char godDir[MAX_TEXT_LENGTH];
    char dataDir[MAX_TEXT_LENGTH];
    char headerPath[MAX_TEXT_LENGTH];
    _snprintf(godDir, sizeof(godDir), "%s\\%s\\%s", base.c_str(), titleIdHex, meta.isOriginalXbox ? "00005000" : "00007000");
    _snprintf(dataDir, sizeof(dataDir), "%s\\%s.data", godDir, mediaIdHex);
    _snprintf(headerPath, sizeof(headerPath), "%s\\%s", godDir, mediaIdHex);

    customForceMkdir(dataDir);

    uint64_t totalDataBytes = disc->TotalBytes();
    uint64_t totalDataBlocks = (totalDataBytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint32_t partCount = (uint32_t)((totalDataBlocks + DATA_BLOCKS_PER_PART - 1) / DATA_BLOCKS_PER_PART);
    if (partCount == 0) partCount = 1;

    dprintf("GOD: Packing TitleID %s (%u parts, %.1f MB)...\n",
        titleIdHex, partCount, (double)totalDataBytes / (1024.0 * 1024.0));

    uint8_t *blockBuf = (uint8_t *)malloc((size_t)BLOCK_SIZE);
    uint8_t *masterHashTable = (uint8_t *)malloc((size_t)BLOCK_SIZE);
    uint8_t *subHashTables = (uint8_t *)malloc((size_t)(SUBTABLES_PER_PART * BLOCK_SIZE));
    if (!blockBuf || !masterHashTable || !subHashTables) {
        if (blockBuf) free(blockBuf);
        if (masterHashTable) free(masterHashTable);
        if (subHashTables) free(subHashTables);
        delete disc;
        dprintf("GOD Error: Out of memory allocating hash buffers\n");
        return EXIT_FAILURE;
    }

    uint8_t topLevelHash[20];
    memset(topLevelHash, 0, sizeof(topLevelHash));
    uint64_t totalContainerBytes = 0;

    for (uint32_t part = 0; part < partCount; ++part) {
        char partPath[MAX_TEXT_LENGTH];
        _snprintf(partPath, sizeof(partPath), "%s\\Data%04u", dataDir, part);

        FILE *outFp = fopen(partPath, "wb");
        if (!outFp) {
            dprintf("GOD Error: Cannot create %s\n", partPath);
            free(blockBuf); free(masterHashTable); free(subHashTables);
            delete disc;
            return EXIT_FAILURE;
        }

        memset(masterHashTable, 0, (size_t)BLOCK_SIZE);
        memset(subHashTables, 0, (size_t)(SUBTABLES_PER_PART * BLOCK_SIZE));

        uint64_t partStartBlock = (uint64_t)part * DATA_BLOCKS_PER_PART;
        uint64_t blocksInThisPart = std::min<uint64_t>(DATA_BLOCKS_PER_PART, totalDataBlocks - partStartBlock);
        uint32_t subTablesInPart = (uint32_t)((blocksInThisPart + BLOCKS_PER_SUBTABLE - 1) / BLOCKS_PER_SUBTABLE);

        uint64_t headerBlocks = 1ULL + subTablesInPart;
        _fseeki64(outFp, (__int64)(headerBlocks * BLOCK_SIZE), SEEK_SET);

        DWORD lastTick = GetTickCount();
        for (uint64_t b = 0; b < blocksInThisPart; ++b) {
            uint64_t globalBlock = partStartBlock + b;
            DWORD now = GetTickCount();
            if (now - lastTick >= 750 || b + 1 == blocksInThisPart) {
                lastTick = now;
                uint64_t doneBytes = (globalBlock + 1) * BLOCK_SIZE;
                if (doneBytes > totalDataBytes) doneBytes = totalDataBytes;
                unsigned int pct = totalDataBytes > 0 ? (unsigned int)((doneBytes * 100ULL) / totalDataBytes) : 100;
                dprintf("GOD: %u%% (%.1f / %.1f MB) [Part %u/%u]\n",
                    pct, doneBytes / (1024.0 * 1024.0), totalDataBytes / (1024.0 * 1024.0), part + 1, partCount);
            }
            if (!disc->ReadBytes(globalBlock * BLOCK_SIZE, blockBuf, (size_t)BLOCK_SIZE)) {
                fclose(outFp);
                free(blockBuf); free(masterHashTable); free(subHashTables);
                delete disc;
                dprintf("GOD Error: Read failed at block %llu\n", globalBlock);
                return EXIT_FAILURE;
            }

            if (fwrite(blockBuf, 1, (size_t)BLOCK_SIZE, outFp) != (size_t)BLOCK_SIZE) {
                fclose(outFp);
                free(blockBuf); free(masterHashTable); free(subHashTables);
                delete disc;
                dprintf("GOD Error: Write failed on %s\n", partPath);
                return EXIT_FAILURE;
            }

            uint32_t subIdx = (uint32_t)(b / BLOCKS_PER_SUBTABLE);
            uint32_t entryIdx = (uint32_t)(b % BLOCKS_PER_SUBTABLE);
            uint8_t *hashDst = subHashTables + ((size_t)subIdx * (size_t)BLOCK_SIZE) + ((size_t)entryIdx * 20);
            ComputeSha1(blockBuf, (size_t)BLOCK_SIZE, hashDst);
        }

        for (uint32_t s = 0; s < subTablesInPart; ++s) {
            uint8_t *subPtr = subHashTables + ((size_t)s * (size_t)BLOCK_SIZE);
            ComputeSha1(subPtr, (size_t)BLOCK_SIZE, masterHashTable + ((size_t)s * 20));
        }

        if (part == 0) ComputeSha1(masterHashTable, (size_t)BLOCK_SIZE, topLevelHash);

        _fseeki64(outFp, 0, SEEK_SET);
        fwrite(masterHashTable, 1, (size_t)BLOCK_SIZE, outFp);
        fwrite(subHashTables, 1, (size_t)(subTablesInPart * BLOCK_SIZE), outFp);
        fclose(outFp);

        totalContainerBytes += (headerBlocks + blocksInThisPart) * BLOCK_SIZE;
        dprintf("GOD: Wrote Data%04u (%u/%u)\n", part, part + 1, partCount);
    }

    free(blockBuf);
    free(masterHashTable);
    free(subHashTables);
    delete disc;

    const size_t LIVE_HEADER_SIZE = 0xB000;
    uint8_t *liveHdr = (uint8_t *)calloc(1, LIVE_HEADER_SIZE);
    if (!liveHdr) {
        dprintf("GOD Error: Out of memory allocating LIVE header\n");
        return EXIT_FAILURE;
    }

    memcpy(liveHdr + 0x000, "LIVE", 4);
    WriteBigEndian32(liveHdr + 0x344, meta.isOriginalXbox ? 0x00005000 : 0x00007000);
    WriteBigEndian32(liveHdr + 0x348, 0x00000001);
    WriteBigEndian64(liveHdr + 0x34C, totalContainerBytes);
    WriteBigEndian32(liveHdr + 0x354, meta.titleId);
    liveHdr[0x35C] = meta.platform;
    liveHdr[0x35D] = meta.execType;
    liveHdr[0x35E] = meta.discNum;
    liveHdr[0x35F] = meta.totalDiscs;
    WriteBigEndian32(liveHdr + 0x360, meta.titleId);

    memcpy(liveHdr + 0x37D, topLevelHash, 20);
    WriteBigEndian32(liveHdr + 0x391, 0x0000AD0E);
    liveHdr[0x395] = 0x01;
    WriteBigEndian32(liveHdr + 0x3A0, (uint32_t)(totalDataBlocks + partCount * 205));
    memcpy(liveHdr + 0x3D4, meta.mediaId, 16);

    for (int i = 0; i < 128 && meta.titleName[i] != 0; ++i) {
        uint16_t beChar = Swap16((uint16_t)meta.titleName[i]);
        memcpy(liveHdr + 0x411 + i * 2, &beChar, 2);
    }

    FILE *hdrFp = fopen(headerPath, "wb");
    if (!hdrFp) {
        free(liveHdr);
        dprintf("GOD Error: Cannot create LIVE header %s\n", headerPath);
        return EXIT_FAILURE;
    }
    fwrite(liveHdr, 1, LIVE_HEADER_SIZE, hdrFp);
    fclose(hdrFp);
    free(liveHdr);

    dprintf("GOD: Successfully created %s\n", headerPath);
    return EXIT_SUCCESS;
}




