#ifndef GOD_CONTENT_HELPER_H
#define GOD_CONTENT_HELPER_H

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

        if (nlen > 0) {
            XisoDirEntry ent;
            ent.name.assign((const char *)&table[off + 14], nlen);
            ent.sector = sec;
            ent.size = sz;
            ent.isDir = (attr & 0x10) != 0;
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

static bool ExtractXisoTreeRecursive(DiscStream *disc, uint32_t dirSector, uint32_t dirSize, const std::string &destDir) {
    std::vector<XisoDirEntry> entries;
    if (!ListXisoDirectory(disc, dirSector, dirSize, entries)) return false;
    customForceMkdir(destDir.c_str());

    const size_t BUF_SZ = 256 * 1024;
    uint8_t *buf = (uint8_t *)malloc(BUF_SZ);
    if (!buf) return false;

    for (size_t i = 0; i < entries.size(); ++i) {
        std::string childDst = destDir + "\\" + entries[i].name;
        if (entries[i].isDir) {
            if (!ExtractXisoTreeRecursive(disc, entries[i].sector, entries[i].size, childDst)) {
                free(buf);
                return false;
            }
        } else {
            dprintf("GOD/DLC: Installing %s (%.1f MB)...\n",
                entries[i].name.c_str(), entries[i].size / (1024.0 * 1024.0));
            FILE *fp = fopen(childDst.c_str(), "wb");
            if (!fp) {
                free(buf);
                dprintf("GOD/DLC Error: Cannot write %s\n", childDst.c_str());
                return false;
            }
            uint64_t rem = entries[i].size;
            uint64_t srcOff = (uint64_t)entries[i].sector * SECTOR_SIZE;
            while (rem > 0) {
                size_t chunk = rem > BUF_SZ ? BUF_SZ : (size_t)rem;
                if (!disc->ReadBytes(srcOff, buf, chunk) || fwrite(buf, 1, chunk, fp) != chunk) {
                    fclose(fp);
                    free(buf);
                    return false;
                }
                srcOff += chunk;
                rem -= chunk;
            }
            fclose(fp);
        }
    }
    free(buf);
    return true;
}

// Checks for Content\0000000000000000 on the disc.
// Sets outSkipGod = true if this is a dedicated Install/DLC/Bonus disc (< 150 MB outside Content).
static bool HandleEmbeddedContentFolder(DiscStream *disc, uint32_t rootSector, uint32_t rootSize, const std::string &contentRoot, bool &outSkipGod) {
    outSkipGod = false;
    std::vector<XisoDirEntry> rootEntries;
    if (!ListXisoDirectory(disc, rootSector, rootSize, rootEntries)) return true;

    const XisoDirEntry *contentDir = NULL;
    for (size_t i = 0; i < rootEntries.size(); ++i) {
        if (rootEntries[i].isDir && _stricmp(rootEntries[i].name.c_str(), "Content") == 0) {
            contentDir = &rootEntries[i];
            break;
        }
    }
    if (!contentDir) return true;

    std::vector<XisoDirEntry> subEntries;
    if (!ListXisoDirectory(disc, contentDir->sector, contentDir->size, subEntries)) return true;

    const XisoDirEntry *zeroProfileDir = NULL;
    for (size_t i = 0; i < subEntries.size(); ++i) {
        if (subEntries[i].isDir && strcmp(subEntries[i].name.c_str(), "0000000000000000") == 0) {
            zeroProfileDir = &subEntries[i];
            break;
        }
    }
    if (!zeroProfileDir) return true;

    dprintf("GOD/DLC: Detected Install/Bonus Disc Content folder!\n");
    if (!ExtractXisoTreeRecursive(disc, zeroProfileDir->sector, zeroProfileDir->size, contentRoot)) {
        dprintf("GOD/DLC Error: Failed extracting Content\\0000000000000000 packages\n");
        return false;
    }

    uint64_t gameDataBytes = SumNonContentBytes(disc, rootSector, rootSize, true);
    if (gameDataBytes < 150ULL * 1024ULL * 1024ULL) {
        dprintf("GOD/DLC: Disc is an Install/Add-On disc (no standalone GOD needed). Done!\n");
        outSkipGod = true;
    } else {
        dprintf("GOD/DLC: Hybrid disc detected; also packing playable game to GOD...\n");
    }
    return true;
}

#endif

