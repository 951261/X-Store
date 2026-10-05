#ifndef XSTORE_DECOMPRESS_ZIP_H
#define XSTORE_DECOMPRESS_ZIP_H

// Extract a ZIP stored as consecutive raw .001, .002, ... chunks.
// Pass absolute device paths, e.g. "game:\\file.zip.001", "hdd:\\content".
// The destination and its subdirectories are created as needed.
// Files up to 0xF0000000 bytes use normal output; larger files become .001,
// .002, ... outputs (at most 999 parts). Existing output files are replaced.
// When isXBLA is true, each archive file/folder name is truncated to 40 bytes
// (including extensions). Split filenames reserve 4 bytes for the .001 suffix.
// Names that truncate to the same path follow the existing overwrite behavior.
// Unsafe entry names and resulting paths exceeding MAX_PATH are rejected.
// Returns 0 on success, nonzero on failure; errors are logged with dprintf.
// On failure, files already extracted or partially written remain on disk.
int decompressZipFile(const char *inputFile, const char *outputPath, bool isXBLA);

#endif
