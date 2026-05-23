/*
FILE : FAT32.cpp
PROJECT : xstore
PROGRAMMER : 951261
DESCRIPTION : This is a FAT32 driver. For some strange reason, creating 10-12 character file names can sometimes cause issues
*/

#include "FAT32.h"

#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Corona4G.h"

#ifndef OBJ_CASE_INSENSITIVE
#define OBJ_CASE_INSENSITIVE 0x40
#endif

#ifndef FILE_SYNCHRONOUS_IO_NONALERT
#define FILE_SYNCHRONOUS_IO_NONALERT 0x20
#endif

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS 0
#endif

#ifdef __cplusplus
extern "C" {
#endif
NTSTATUS NtReadFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
                    PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock,
                    PVOID Buffer, DWORD Length, PLARGE_INTEGER ByteOffset);
#ifdef __cplusplus
}
#endif

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef struct Fat32Info
{
    HANDLE device;
    u64 partitionOffset;
    u16 bytesPerSector;
    u8 sectorsPerCluster;
    u16 reservedSectors;
    u8 fatCount;
    u32 sectorsPerFat;
    u32 rootCluster;
    u32 firstDataSector;
    u32 totalSectors;
} Fat32Info;

typedef struct DirSlot
{
    u32 cluster;
    u32 offsetInCluster;
    u64 diskOffset;
} DirSlot;

static u16 le16(const u8 *p)
{
    return (u16)(p[0] | (p[1] << 8));
}

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void put_le16(u8 *p, u16 v)
{
    p[0] = (u8)(v & 0xff);
    p[1] = (u8)(v >> 8);
}

static void put_le32(u8 *p, u32 v)
{
    p[0] = (u8)(v & 0xff);
    p[1] = (u8)((v >> 8) & 0xff);
    p[2] = (u8)((v >> 16) & 0xff);
    p[3] = (u8)((v >> 24) & 0xff);
}

static int ascii_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int str_equal_no_case(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (ascii_lower((unsigned char)*a) != ascii_lower((unsigned char)*b))
            return 0;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static int starts_with_drive(const char *path, const char *drive)
{
    size_t n = strlen(drive);
    if (_strnicmp(path, drive, n) != 0)
        return 0;
    return path[n] == '\\' || path[n] == '/';
}

static int split_drive_path(const char *path, const char *drive, char *dir, size_t dirSize, char *name, size_t nameSize)
{
    const char *rel;
    const char *lastSlash;
    const char *p;
    size_t dirLen;
    size_t driveLen;

    if (!path || !drive || !starts_with_drive(path, drive))
        return -1;

    driveLen = strlen(drive);
    rel = path + driveLen;
    while (*rel == '\\' || *rel == '/')
        ++rel;

    lastSlash = NULL;
    for (p = rel; *p; ++p)
    {
        if (*p == '\\' || *p == '/')
            lastSlash = p;
    }

    if (lastSlash)
    {
        dirLen = (size_t)(lastSlash - rel);
        if (dirLen >= dirSize)
            return -1;
        memcpy(dir, rel, dirLen);
        dir[dirLen] = '\0';
        strncpy(name, lastSlash + 1, nameSize - 1);
    }
    else
    {
        dir[0] = '\0';
        strncpy(name, rel, nameSize - 1);
    }

    name[nameSize - 1] = '\0';
    return name[0] ? 0 : -1;
}

static int raw_open(const char *devicePath, HANDLE *out)
{
    STRING name;
    OBJECT_ATTRIBUTES attr;
    IO_STATUS_BLOCK ios;
    NTSTATUS status;

    *out = NULL;
    RtlInitAnsiString(&name, (PCHAR)devicePath);
    attr.RootDirectory = NULL;
    attr.ObjectName = &name;
    attr.Attributes = OBJ_CASE_INSENSITIVE;

    status = NtOpenFile(out,
                        GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                        &attr,
                        &ios,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT);

    return status < STATUS_SUCCESS ? -1 : 0;
}

static int raw_read(HANDLE h, u64 offset, void *buf, u32 len)
{
    IO_STATUS_BLOCK ios;
    LARGE_INTEGER pos;
    pos.QuadPart = offset;
    return NtReadFile(h, NULL, NULL, NULL, &ios, buf, len, &pos) < STATUS_SUCCESS ? -1 : 0;
}

static int raw_write(HANDLE h, u64 offset, const void *buf, u32 len)
{
    IO_STATUS_BLOCK ios;
    LARGE_INTEGER pos;
    NTSTATUS status;
    const u32 sectorSize = 512;

    if ((offset % sectorSize) != 0 || (len % sectorSize) != 0)
    {
        const u8 *src = (const u8 *)buf;
        u32 done = 0;

        while (done < len)
        {
            u8 sector[512];
            u64 sectorOffset = (offset + done) & ~(u64)(sectorSize - 1);
            u32 inSector = (u32)((offset + done) - sectorOffset);
            u32 chunk = sectorSize - inSector;

            if (chunk > len - done)
                chunk = len - done;

            if (raw_read(h, sectorOffset, sector, sectorSize) != 0)
                return -1;

            memcpy(sector + inSector, src + done, chunk);

            pos.QuadPart = sectorOffset;
            status = NtWriteFile(h, NULL, NULL, NULL, &ios, sector, sectorSize, &pos);
            if (status < STATUS_SUCCESS)
            {
                printf("Fat32Rename: raw sector write failed offset=%llu len=%u status=0x%08X\n",
                       sectorOffset, sectorSize, (unsigned int)status);
                return -1;
            }

            done += chunk;
        }

        return 0;
    }

    pos.QuadPart = offset;
    status = NtWriteFile(h, NULL, NULL, NULL, &ios, (PVOID)buf, len, &pos);
    if (status < STATUS_SUCCESS)
    {
        printf("Fat32Rename: raw_write failed offset=%llu len=%u status=0x%08X\n",
               offset, len, (unsigned int)status);
        return -1;
    }
    return 0;
}

static int read_sector(HANDLE h, u64 sector, u16 bytesPerSector, void *buf)
{
    return raw_read(h, sector * bytesPerSector, buf, bytesPerSector);
}

static int boot_sector_looks_fat32(const u8 *b)
{
    if (b[510] != 0x55 || b[511] != 0xaa)
        return 0;
    if (le16(b + 11) == 0 || b[13] == 0 || b[16] == 0)
        return 0;
    return le32(b + 36) != 0 && le32(b + 44) >= 2;
}

static int load_fat32_info(HANDLE h, Fat32Info *fs)
{
    u8 sector[512];
    u32 partLba = 0;
    u8 partType;

    memset(fs, 0, sizeof(*fs));
    fs->device = h;

    if (raw_read(h, 0, sector, sizeof(sector)) != 0)
        return -1;

    if (!boot_sector_looks_fat32(sector))
    {
        int i;
        int found = 0;
        for (i = 0; i < 4; ++i)
        {
            u8 *entry = sector + 0x1be + i * 16;
            partType = entry[4];
            if (partType == 0x0b || partType == 0x0c || partType == 0x0e)
            {
                partLba = le32(entry + 8);
                found = 1;
                break;
            }
        }
        if (!found)
            return -1;
        if (raw_read(h, (u64)partLba * 512, sector, sizeof(sector)) != 0)
            return -1;
        if (!boot_sector_looks_fat32(sector))
            return -1;
    }

    fs->partitionOffset = (u64)partLba * 512;
    fs->bytesPerSector = le16(sector + 11);
    fs->sectorsPerCluster = sector[13];
    fs->reservedSectors = le16(sector + 14);
    fs->fatCount = sector[16];
    fs->totalSectors = le16(sector + 19) ? le16(sector + 19) : le32(sector + 32);
    fs->sectorsPerFat = le32(sector + 36);
    fs->rootCluster = le32(sector + 44);
    fs->firstDataSector = fs->reservedSectors + fs->fatCount * fs->sectorsPerFat;

    if (fs->bytesPerSector == 0 || fs->sectorsPerCluster == 0 ||
        fs->sectorsPerFat == 0 || fs->rootCluster < 2)
        return -1;

    return 0;
}

static u32 cluster_size(const Fat32Info *fs)
{
    return (u32)fs->bytesPerSector * fs->sectorsPerCluster;
}

static u64 cluster_offset(const Fat32Info *fs, u32 cluster)
{
    u64 sector = fs->firstDataSector + (u64)(cluster - 2) * fs->sectorsPerCluster;
    return fs->partitionOffset + sector * fs->bytesPerSector;
}

static u64 fat_entry_offset(const Fat32Info *fs, u32 cluster)
{
    return fs->partitionOffset + (u64)fs->reservedSectors * fs->bytesPerSector + (u64)cluster * 4;
}

static int read_fat_entry(const Fat32Info *fs, u32 cluster, u32 *next)
{
    u8 b[4];
    if (raw_read(fs->device, fat_entry_offset(fs, cluster), b, sizeof(b)) != 0)
        return -1;
    *next = le32(b) & 0x0fffffff;
    return 0;
}

static int write_fat_entry_all(const Fat32Info *fs, u32 cluster, u32 value)
{
    u8 b[4];
    u8 old[4];
    int fat;
    u64 base = fs->partitionOffset + (u64)fs->reservedSectors * fs->bytesPerSector;

    if (raw_read(fs->device, fat_entry_offset(fs, cluster), old, sizeof(old)) != 0)
        return -1;
    value = (le32(old) & 0xf0000000) | (value & 0x0fffffff);
    put_le32(b, value);

    for (fat = 0; fat < fs->fatCount; ++fat)
    {
        u64 off = base + (u64)fat * fs->sectorsPerFat * fs->bytesPerSector + (u64)cluster * 4;
        if (raw_write(fs->device, off, b, sizeof(b)) != 0)
            return -1;
    }
    return 0;
}

static int is_eoc(u32 cluster)
{
    return cluster >= 0x0ffffff8;
}

static int read_cluster(const Fat32Info *fs, u32 cluster, u8 *buf)
{
    return raw_read(fs->device, cluster_offset(fs, cluster), buf, cluster_size(fs));
}

static int write_cluster(const Fat32Info *fs, u32 cluster, const u8 *buf)
{
    return raw_write(fs->device, cluster_offset(fs, cluster), buf, cluster_size(fs));
}

static int make_short_name(const char *name, u8 out[11])
{
    const char *dot = strrchr(name, '.');
    size_t baseLen;
    size_t extLen = 0;
    size_t i;

    memset(out, ' ', 11);
    if (!name || !*name)
        return -1;

    baseLen = dot ? (size_t)(dot - name) : strlen(name);
    if (baseLen == 0 || baseLen > 8)
        return -1;
    if (dot)
    {
        extLen = strlen(dot + 1);
        if (extLen > 3)
            return -1;
    }

    for (i = 0; i < baseLen; ++i)
    {
        unsigned char c = (unsigned char)name[i];
        if (c <= ' ' || c == '.' || c == '\\' || c == '/' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return -1;
        out[i] = (u8)((c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c);
    }
    for (i = 0; i < extLen; ++i)
    {
        unsigned char c = (unsigned char)dot[1 + i];
        if (c <= ' ' || c == '.' || c == '\\' || c == '/' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return -1;
        out[8 + i] = (u8)((c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c);
    }
    return 0;
}

static void short_name_to_text(const u8 e[32], char *out, size_t outSize)
{
    int i;
    int pos = 0;
    for (i = 0; i < 8 && e[i] != ' '; ++i)
        out[pos++] = (char)e[i];
    if (e[8] != ' ' && pos < (int)outSize - 1)
        out[pos++] = '.';
    for (i = 8; i < 11 && e[i] != ' ' && pos < (int)outSize - 1; ++i)
        out[pos++] = (char)e[i];
    out[pos] = '\0';
}

static u8 lfn_checksum(const u8 shortName[11])
{
    int i;
    u8 sum = 0;
    for (i = 0; i < 11; ++i)
        sum = (u8)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + shortName[i]);
    return sum;
}

static void append_lfn_char(char *lfn, size_t lfnSize, u16 ch)
{
    size_t n;
    if (ch == 0x0000 || ch == 0xffff)
        return;
    n = strlen(lfn);
    if (n + 1 < lfnSize)
    {
        lfn[n] = (ch < 0x80) ? (char)ch : '?';
        lfn[n + 1] = '\0';
    }
}

static void collect_lfn_chars(const u8 *entry, char *lfn, size_t lfnSize)
{
    int i;
    static const int offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    u8 seq = entry[0] & 0x1f;
    size_t base;

    if (seq == 0)
        return;

    base = (size_t)(seq - 1) * 13;
    if (base >= lfnSize)
        return;

    for (i = 0; i < 13; ++i)
    {
        u16 ch = le16(entry + offsets[i]);
        size_t pos = base + i;
        if (pos + 1 >= lfnSize)
            break;
        if (ch == 0x0000)
        {
            lfn[pos] = '\0';
            break;
        }
        if (ch != 0xffff)
        {
            lfn[pos] = (ch < 0x80) ? (char)ch : '?';
            if (lfn[pos + 1] == '\0')
                lfn[pos + 1] = '\0';
        }
    }
}

static u32 entry_first_cluster(const u8 *entry)
{
    return ((u32)le16(entry + 20) << 16) | le16(entry + 26);
}

static int entry_is_free(const u8 *entry)
{
    return entry[0] == 0x00 || entry[0] == 0xe5;
}

static int find_entry_in_dir(const Fat32Info *fs, u32 dirCluster, const char *name, u8 outEntry[32], DirSlot *slot)
{
    u8 shortName[11];
    u8 *buf;
    u32 c = dirCluster;
    u32 size = cluster_size(fs);
    int haveShort = make_short_name(name, shortName) == 0;

    buf = (u8 *)malloc(size);
    if (!buf)
        return -1;

    while (!is_eoc(c))
    {
        u32 next;
        u32 off;
        char lfn[260];
        memset(lfn, 0, sizeof(lfn));

        if (read_cluster(fs, c, buf) != 0)
            break;

        for (off = 0; off < size; off += 32)
        {
            u8 *e = buf + off;
            if (e[0] == 0x00)
            {
                free(buf);
                return -1;
            }
            if (e[0] == 0xe5)
            {
                memset(lfn, 0, sizeof(lfn));
                continue;
            }
            if ((e[11] & 0x3f) == 0x0f)
            {
                if (e[0] & 0x40)
                    memset(lfn, 0, sizeof(lfn));
                collect_lfn_chars(e, lfn, sizeof(lfn));
                continue;
            }
            if (!(e[11] & 0x08))
            {
                char shortText[16];
                short_name_to_text(e, shortText, sizeof(shortText));
                if ((lfn[0] && str_equal_no_case(lfn, name)) ||
                    str_equal_no_case(shortText, name) ||
                    (haveShort && memcmp(e, shortName, 11) == 0))
                {
                    memcpy(outEntry, e, 32);
                    if (slot)
                    {
                        slot->cluster = c;
                        slot->offsetInCluster = off;
                        slot->diskOffset = cluster_offset(fs, c) + off;
                    }
                    free(buf);
                    return 0;
                }
            }
            memset(lfn, 0, sizeof(lfn));
        }

        if (read_fat_entry(fs, c, &next) != 0)
            break;
        c = next;
    }

    free(buf);
    return -1;
}

static void dump_dir_entries(const Fat32Info *fs, u32 dirCluster)
{
    u8 *buf;
    u32 c = dirCluster;
    u32 size = cluster_size(fs);
    int printed = 0;

    buf = (u8 *)malloc(size);
    if (!buf)
        return;

    printf("Fat32Rename: raw directory listing follows\n");

    while (!is_eoc(c) && printed < 80)
    {
        u32 next;
        u32 off;
        char lfn[260];
        memset(lfn, 0, sizeof(lfn));

        if (read_cluster(fs, c, buf) != 0)
            break;

        for (off = 0; off < size && printed < 80; off += 32)
        {
            u8 *e = buf + off;
            if (e[0] == 0x00)
            {
                printf("  <end>\n");
                free(buf);
                return;
            }
            if (e[0] == 0xe5)
            {
                memset(lfn, 0, sizeof(lfn));
                continue;
            }
            if ((e[11] & 0x3f) == 0x0f)
            {
                if (e[0] & 0x40)
                    memset(lfn, 0, sizeof(lfn));
                collect_lfn_chars(e, lfn, sizeof(lfn));
                continue;
            }
            if (!(e[11] & 0x08))
            {
                char shortText[16];
                short_name_to_text(e, shortText, sizeof(shortText));
                printf("  short='%s' attr=0x%02X cluster=%u lfn='%s'\n",
                       shortText, e[11], entry_first_cluster(e), lfn);
                ++printed;
            }
            memset(lfn, 0, sizeof(lfn));
        }

        if (read_fat_entry(fs, c, &next) != 0)
            break;
        c = next;
    }

    free(buf);
}

static int find_free_run_in_dir(const Fat32Info *fs, u32 dirCluster, int entriesNeeded, DirSlot *slot)
{
    u8 *buf;
    u32 c = dirCluster;
    u32 size = cluster_size(fs);

    buf = (u8 *)malloc(size);
    if (!buf)
        return -1;

    while (!is_eoc(c))
    {
        u32 next;
        u32 off;
        int run = 0;
        u32 runStart = 0;

        if (read_cluster(fs, c, buf) != 0)
            break;

        for (off = 0; off < size; off += 32)
        {
            if (entry_is_free(buf + off))
            {
                if (run == 0)
                    runStart = off;
                ++run;
                if (run >= entriesNeeded)
                {
                    slot->cluster = c;
                    slot->offsetInCluster = runStart;
                    slot->diskOffset = cluster_offset(fs, c) + runStart;
                    free(buf);
                    return 0;
                }
            }
            else
            {
                run = 0;
            }
        }

        if (read_fat_entry(fs, c, &next) != 0)
            break;
        c = next;
    }

    free(buf);
    return -1;
}

static int find_free_cluster(const Fat32Info *fs, u32 *cluster)
{
    u32 maxClusters = (fs->totalSectors - fs->firstDataSector) / fs->sectorsPerCluster + 2;
    u32 c;
    for (c = 2; c < maxClusters; ++c)
    {
        u32 v;
        if (read_fat_entry(fs, c, &v) != 0)
            return -1;
        if (v == 0)
        {
            *cluster = c;
            return 0;
        }
    }
    return -1;
}

static int append_dir_cluster(const Fat32Info *fs, u32 dirCluster, DirSlot *slot)
{
    u32 c = dirCluster;
    u32 next;
    u32 freeCluster;
    u8 *zero;
    u32 size = cluster_size(fs);

    while (read_fat_entry(fs, c, &next) == 0 && !is_eoc(next))
        c = next;

    if (find_free_cluster(fs, &freeCluster) != 0)
        return -1;

    zero = (u8 *)calloc(1, size);
    if (!zero)
        return -1;

    if (write_cluster(fs, freeCluster, zero) != 0 ||
        write_fat_entry_all(fs, freeCluster, 0x0fffffff) != 0 ||
        write_fat_entry_all(fs, c, freeCluster) != 0)
    {
        free(zero);
        return -1;
    }

    free(zero);
    slot->cluster = freeCluster;
    slot->offsetInCluster = 0;
    slot->diskOffset = cluster_offset(fs, freeCluster);
    return 0;
}

static int walk_to_parent_dir(const Fat32Info *fs, const char *dirPath, u32 *cluster)
{
    char tmp[512];
    char *context = NULL;
    char *part;
    u32 current = fs->rootCluster;

    if (!dirPath || !*dirPath)
    {
        *cluster = current;
        return 0;
    }

    strncpy(tmp, dirPath, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (part = tmp; *part; ++part)
    {
        if (*part == '/')
            *part = '\\';
    }

    part = strtok_s(tmp, "\\", &context);
    while (part)
    {
        u8 entry[32];
        if (find_entry_in_dir(fs, current, part, entry, NULL) != 0)
            return -1;
        if (!(entry[11] & 0x10))
            return -1;
        current = entry_first_cluster(entry);
        if (current < 2)
            return -1;
        part = strtok_s(NULL, "\\", &context);
    }

    *cluster = current;
    return 0;
}

static void set_lfn_char(u8 *entry, int index, u16 ch)
{
    static const int offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    put_le16(entry + offsets[index], ch);
}

static void build_lfn_entry(u8 *entry, const char *longName, int seq, int total, u8 checksum)
{
    int i;
    int start = (seq - 1) * 13;
    int len = (int)strlen(longName);

    memset(entry, 0xff, 32);
    entry[0] = (u8)seq;
    if (seq == total)
        entry[0] |= 0x40;
    entry[11] = 0x0f;
    entry[12] = 0;
    entry[13] = checksum;
    put_le16(entry + 26, 0);

    for (i = 0; i < 13; ++i)
    {
        int pos = start + i;
        if (pos < len)
            set_lfn_char(entry, i, (u16)(unsigned char)longName[pos]);
        else if (pos == len)
            set_lfn_char(entry, i, 0x0000);
        else
            set_lfn_char(entry, i, 0xffff);
    }
}

static int write_rename_entries(const Fat32Info *fs, const DirSlot *target,
                                const u8 oldShortEntry[32], const char *newName)
{
    int len = (int)strlen(newName);
    int lfnCount = (len + 12) / 13;
    int totalEntries = lfnCount + 1;
    u8 *entries;
    u8 checksum = lfn_checksum(oldShortEntry);
    int i;

    if (len <= 0 || len > 255)
        return -1;

    entries = (u8 *)malloc(totalEntries * 32);
    if (!entries)
        return -1;

    for (i = 0; i < lfnCount; ++i)
    {
        int seq = lfnCount - i;
        build_lfn_entry(entries + i * 32, newName, seq, lfnCount, checksum);
    }

    memcpy(entries + lfnCount * 32, oldShortEntry, 32);

    for (i = 0; i < totalEntries; ++i)
    {
        if (raw_write(fs->device, target->diskOffset + (u64)i * 32, entries + i * 32, 32) != 0)
        {
            free(entries);
            return -1;
        }
    }

    free(entries);
    return 0;
}

static int remount_drive(const char *driveName, const char *devicePath)
{
    STRING linkName;
    STRING deviceName;
    char link[64];

    _snprintf(link, sizeof(link), "\\??\\%s", driveName);
    link[sizeof(link) - 1] = '\0';

    RtlInitAnsiString(&linkName, link);
    RtlInitAnsiString(&deviceName, (PCHAR)devicePath);
    ObDeleteSymbolicLink(&linkName);
    return ObCreateSymbolicLink(&linkName, &deviceName) < 0 ? -1 : 0;
}

int Fat32RenameOnDevice(const char *driveName, const char *devicePath, const char *oldPath, const char *newPath)
{
    char oldDir[512];
    char newDir[512];
    char oldName[260];
    char newName[260];
    STRING linkName;
    char link[64];
    HANDLE h = NULL;
    Fat32Info fs;
    u32 parentCluster;
    u8 oldEntry[32];
    u8 existingEntry[32];
    DirSlot oldSlot;
    DirSlot targetSlot;
    int lfnCount;
    int result = -1;

    if (!driveName || !devicePath || !oldPath || !newPath)
        return -1;

    if (split_drive_path(oldPath, driveName, oldDir, sizeof(oldDir), oldName, sizeof(oldName)) != 0 ||
        split_drive_path(newPath, driveName, newDir, sizeof(newDir), newName, sizeof(newName)) != 0)
    {
        printf("Fat32Rename: path split failed\n");
        return -1;
    }

    if (!str_equal_no_case(oldDir, newDir))
    {
        printf("Fat32Rename: old/new paths must be in the same directory\n");
        return -1;
    }

    _snprintf(link, sizeof(link), "\\??\\%s", driveName);
    link[sizeof(link) - 1] = '\0';

    RtlInitAnsiString(&linkName, link);
    ObDeleteSymbolicLink(&linkName);

    if (raw_open(devicePath, &h) != 0)
    {
        printf("Fat32Rename: raw_open failed for %s\n", devicePath);
        goto done;
    }

    if (load_fat32_info(h, &fs) != 0)
    {
        printf("Fat32Rename: FAT32 boot sector not found on %s\n", devicePath);
        goto done;
    }

    if (walk_to_parent_dir(&fs, oldDir, &parentCluster) != 0)
    {
        printf("Fat32Rename: parent directory not found: %s\n", oldDir);
        goto done;
    }

    if (find_entry_in_dir(&fs, parentCluster, oldName, oldEntry, &oldSlot) != 0)
    {
        printf("Fat32Rename: old entry not found: %s\n", oldName);
        dump_dir_entries(&fs, parentCluster);
        goto done;
    }

    if (find_entry_in_dir(&fs, parentCluster, newName, existingEntry, NULL) == 0)
    {
        printf("Fat32Rename: new name already exists: %s\n", newName);
        goto done;
    }

    lfnCount = ((int)strlen(newName) + 12) / 13;
    if (find_free_run_in_dir(&fs, parentCluster, lfnCount + 1, &targetSlot) != 0)
    {
        if (append_dir_cluster(&fs, parentCluster, &targetSlot) != 0)
        {
            printf("Fat32Rename: no free directory slots\n");
            goto done;
        }
    }

    if (write_rename_entries(&fs, &targetSlot, oldEntry, newName) != 0)
    {
        printf("Fat32Rename: failed to write new VFAT entries\n");
        goto done;
    }

    {
        u8 deleted = 0xe5;
        if (raw_write(fs.device, oldSlot.diskOffset, &deleted, 1) != 0)
        {
            printf("Fat32Rename: failed to delete old entry\n");
            goto done;
        }
    }

    printf("Fat32Rename: renamed %s to %s\n", oldName, newName);
    result = 0;

done:
    if (h)
        NtClose(h);
    remount_drive(driveName, devicePath);
    return result;
}

int Fat32RenameUsb1(const char *oldPath, const char *newPath)
{   
    if(strstr(oldPath, "Usb1:") != NULL) {
        return Fat32RenameOnDevice("Usb1:", "\\Device\\Mass1", oldPath, newPath);
    } else if (strstr(oldPath, "Usb0:") != NULL) {
        return Fat32RenameOnDevice("Usb0:", "\\Device\\Mass0", oldPath, newPath);
    } else {
        return EXIT_FAILURE; // MUST be a FAT32 formatted USB
    }
}
