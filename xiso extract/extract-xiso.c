/*
	extract-xiso.c

	An xdvdfs .iso (xbox iso) file extraction and creation tool by in <in@fishtank.com>
		written March 10, 2003


	View this file with your tab stops set to 4 spaces or it it will look wacky.


	Regarding licensing:

	I think the GPL sucks!  (it stands for Generosity Poor License)

	My open-source code is really *FREE* so you can do whatever you want with it,
	as long as 1) you don't claim that you wrote my code and 2) you retain a notice
	that some parts of the code are copyright in@fishtank.com and 3) you understand
	there are no warranties.  I only guarantee that it will take up disk space!

	If you want to help out with this project it would be welcome, just email me at
	in@fishtank.com.

	This code is copyright in@fishtank.com and is licensed under a slightly modified
	version of the Berkeley Software License, which follows:

	/*
	 * Copyright (c) 2003 in <in@fishtank.com>
	 * All rights reserved.
	 *
	 * Redistribution and use in source and binary forms, with or without
	 * modification, are permitted provided that the following conditions
	 * are met:
	 *
	 * 1. Redistributions of source code must retain the above copyright
	 *    notice, this list of conditions and the following disclaimer.
	 *
	 * 2. Redistributions in binary form must reproduce the above copyright
	 *    notice, this list of conditions and the following disclaimer in the
	 *    documentation and/or other materials provided with the distribution.
	 *
	 * 3. All advertising materials mentioning features or use of this software
	 *    must display the following acknowledgement:
	 *
	 *    This product includes software developed by in <in@fishtank.com>.
	 *
	 * 4. Neither the name "in" nor the email address "in@fishtank.com"
	 *    may be used to endorse or promote products derived from this software
	 *    without specific prior written permission.
	 *
	 * THIS SOFTWARE IS PROVIDED `AS IS' AND ANY EXPRESS OR IMPLIED WARRANTIES
	 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
	 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
	 * AUTHOR OR ANY CONTRIBUTOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
	 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
	 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
	 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
	 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
	 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
	 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
	 *\


	Last modified:

		03.29.03 in:	Fixed a path display bug, changed the tree descent algorithm
						and added ftp to xbox support (rev to v1.2)

		04.04.03 in:	Added a counter for total number of files in xiso (rev to
						v1.2.1)  THIS VERSION NOT FOR RELEASE!

		04.18.03 in:	Added xoff_t typecasts for __u32 * __u32 manipulations.
						This fixed a bug with very large iso's where the directory
						table was at the end of the iso--duh! (rev to v1.3)

		04.19.03 in:	A user pointed out that the program is increasing its
						memory usage over large iso's.  I've tracked this to the buffer
						allocation in extract_file() during traverse_xiso()
						recursions.  As a fix I've moved the copy buffer to a static
						variable.  Not as encapsulated as I'd like but hey, this is
						C after all.

						Also added support for FreeBSD (on Intel x86) (rev to v1.4)

		04.21.03 in:	It looks like whomever is making xiso creation tools out there
						has never heard of a binary tree and is sticking *every*
						directory entry off the right subnode (at least on all the iso's
						I've found so far).  This causes extremely deep recursion for
						iso's with lots of files (and also causes these iso's, when
						burned to DVD, to behave as a linked list for file lookups, thus
						providing *worst case* lookup performance at all times).

						Not only do I find this annoying and extremely bad programming,
						I've noticed that it is causing sporadic stack overflows with
						my (formerly) otherwise good tree traversal code.

						In order to combat such bad implementations, I've re-implemented
						the traverse_xiso() routine to get rid of any non-directory
						recursion.  Additionally, I've made a few extra tweaks to
						conserve even more memory.  I can see now that I'm going to need
						to write xiso creation as well and do it right. (rev to v1.5 beta)
						NOT FOR RELEASE

		04.22.03 in:	Making some major changes...

						Working on the optimization algorithm, still needs some tweaks
						apparently.  DO NOT RELEASE THIS SOURCE BUILD!

						NOTE:  I'm building this as 1.5 beta and sending the source to
						Emil only, this source is not yet for release.

		04.28.03 in:	I've finally decided that optimizing in-place just *isn't* going
						to happen.  The xbox is *really* picky about how its b-trees
						are laid out.  I've noticed that it will read the directory if
						I lay the entries out in prefix order.  Seems kind of weird to
						me that it would *have* to be that way but whatever.  So, I guess
						I'll write xiso creation and then piggyback a rewrite type op
						on top of it.  Not quite as nice since it means you need extra
						disk space but such is life.

		05.01.03 in:	Well it looks like I got the creation code working tonight, what
						a pain in the ass *that* was.  I've been working on it in my free
						time (which is almost non-existent) for a week now, bleh.  Also
						decided to implement rewriting xisos and I think I'll add build
						xiso from ftp-server, just to be *really* lazy.  I guess this
						means I'll have to read the stat code in the ftp tree.  Hmmm,
						probably need to dig around in there anyway...  A user reported
						that newer builds of evox are barfing with ftp upload so I guess
						I'll go debug that.

						Also cleaned up the code quite a bit tonight just for posterity.
						I'd just like to point out that I *know* I'm being really lazy with
						all these big-ass functions and no header files and such.  The fact
						is I just can't seem to bring myself to care woohaahaa!

						(rev to 2.0 beta)  NOT FOR RELEASE until I get the other goodies
						written ;)

		05.03.03 in:	Added support for create xiso from ftp server.  Still need to debug
						evox to see what the problem is-- looks like something to do for
						tomorrow!

		05.06.03 in:	Finally got back to this little project ;0 -- the ftp bug was that
						FtpWriteBlock() in the libftp kit was timing out on occasion and returning
						less than a complete buffer.  So I fixed that and some other little
						bugs here and there, plus I changed the handling of the create mode
						so that you can now specify an iso name.  Hopefully that's a bit more
						intuitive.

		05.10.03 in:	Fixed a lot of stuff by now, I think it's solid for 2.0 release.
						(rev to 2.0, release)

		05.13.03 in:	Oops, fixed a bug in main() passing an (essentially) nil pointer to
						create_xiso that was causing a core dump and cleaned up the avl_fetch()
						and avl_insert() routines.  (rev to 2.1, release)

		05.14.03 in:	Added media check fix, fixed a bug in the ftp library where FtpStat was
						failing on filenames with spaces in them.

		06.16.03 in:	Based on code from zeek, I added support for win32, minus ftp
						functionality.  Neither he nor I have the time to port the ftp library
						to windows right now, but at least the creation code will work.  Big thanks
						to zeek for taking the time to wade through the source and figure out
						what needed to be tweaked for the windows build.

		06.20.03 in:	Well I just couldn't release the windows build without ftp support (can
						you say OCD <g> ;-), anyway I sat down today and ported the ftp library
						to win32.  That was a major pain let me tell you as I don't have a decent
						PC to run windows on (all my decent PC's run linux) and I've never really
						programmed anything on Windows.  Who'd have known that I couldn't just use
						fdcustomOpen() to convert a socket descriptor to a FILE *!  Anyway, whining aside
						I think it all works the way it's supposed to.  I'm sure once I throw it on
						the PC community I'll have plenty of bug reports, but such is life.  I also
						fixed a few other minor glitches here and there that gcc never complained
						about but that vc++ didn't like.

		07.15.03 in:	Fixed a bug first reported by Metal Maniac (thanks) where the path string was
						being generated improperly during xiso creation on windows.  Special thanks to
						Hydra for submitting code that mostly fixed the problem, I needed to make a few
						more tweaks but nothing much.  Hopefully this will solve the problem.  Also,
						thanks to Huge for doing a Win32 GUI around extract-xiso 2.2!  Rev to 2.3, release.

		07.16.03 in:	Changed some of the help text, looks like I introduced a copy-paste
						bug somewhere along the line.  Oops.

		07.28.03 in:	Added support for progress updating to create_xiso, now just pass in
						a pointer to a progress_callback routine (see typedef below).  Also added
						support on darwin for burning the iso to cd/dvd.  For some reason right now
						everything works fine if I try to burn an image to a DVD, but if I try to
						insert a cd it dies.  I have no idea as of yet what's wrong.  I am strongly
						considering *not* opensourcing my cd-burning stuff once I get it working
						as I can think of a few commercial uses for it.  Have to mull that one
						over a bit more.  This version only for release to UI developers.

		12.02.03 in:	Fixed a few bugs in the ftp subsystem and increased the read-write buffer size
						to 2Mb.  That should help ftp performance quite a bit.

		10.29.04 in:	Well, it's been a looooong time since I've worked on this little program...
						I've always been irritated by the fact that extract-xiso would never create an
						iso that could be auto-detected by CD/DVD burning software.  To burn iso's I've
						always had to go in and select a manual sector size of 2048 bytes, etc.  What
						a pain!  As a result, I've been trying to get my hands on the Yellow Book for
						ages.  I never did manage that as I didn't want to pay for it but I did some
						research the other day and came across the ECMA-119 specification.  It lays
						out the exact volume format that I needed to use.  Hooray!  Now xiso's are
						autodetected and burned properly by burning software...

						If you try to follow what I've done and find the write_volume_descriptors()
						method cryptic, just go download the ecma-119 specification from the ecma
						website.  Read about primary volume descriptors and it'll make sense.

						Bleh! This code is ugly ;-)

		10.25.05 in:	Added in  patch from Nordman.  Thanks.
						Added in security patch from Chris Bainbridge.  Thanks.
						Fixed a few minor bugs.

		01.18.10 aiyyo:	XBox 360 iso extraction supported.

		10.04.10 aiyyo:	Added new command line switch (-s skip $SystemUpdate folder).
						Display file progress in percent.
						Try to create destination directory.

		10.11.10 aiyyo:	Fix -l bug (empty list).

		05.02.11 aiyyo:	Remove security patch.

		09.30.11 somski: Added XGD3 support

		01.11.14 twillecomme: Intégration of aiyyo's and somski's work.
						Minor warn fixes.

	enjoy!

	in
*/

#include "extract-xiso.h"

#if defined(__LINUX__)
#define _LARGEFILE64_SOURCE
#endif
#if defined(__GNUC__)
#define _GNU_SOURCE
#endif

#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>

#include "file-stuff.h"
#include <OutputConsole.h>

#if defined(__FREEBSD__) || defined(__OPENBSD__)
#include <machine/limits.h>
#endif

// Allow some functions to work
// #undef _XBSTRICT

// #if defined( _WIN32 )
#include <direct.h>
#include "win32/dirent.h"
// #else
#if 0
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#endif

#if defined(_MSC_VER)
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#if defined(__DARWIN__)
#define exiso_target "macos-x"

#define PATH_CHAR '/'
#define PATH_CHAR_STR "/"

#define FORCE_ASCII 1
#define READFLAGS O_RDONLY
#define WRITEFLAGS O_WRONLY | O_CREAT | O_TRUNC
#define READWRITEFLAGS O_RDWR

typedef off_t xoff_t;
#elif defined(__FREEBSD__)
#define exiso_target "freebsd"

#define PATH_CHAR '/'
#define PATH_CHAR_STR "/"

#define FORCE_ASCII 1
#define READFLAGS O_RDONLY
#define WRITEFLAGS O_WRONLY | O_CREAT | O_TRUNC
#define READWRITEFLAGS O_RDWR

typedef off_t xoff_t;
#elif defined(__LINUX__)
#define exiso_target "linux"

#define PATH_CHAR '/'
#define PATH_CHAR_STR "/"

#define FORCE_ASCII 0
#define READFLAGS O_RDONLY | O_LARGEFILE
#define WRITEFLAGS O_WRONLY | O_CREAT | O_TRUNC | O_LARGEFILE
#define READWRITEFLAGS O_RDWR | O_LARGEFILE

#define lseek lseek64
#define stat stat64

typedef off64_t xoff_t;
#elif defined(__OPENBSD__)
#define exiso_target "openbsd"
#elif defined(_WIN32)
#define exiso_target "win32"

#define PATH_CHAR '\\'
#define PATH_CHAR_STR "\\"

#define FORCE_ASCII 0
#define READFLAGS O_RDONLY | O_BINARY
#define WRITEFLAGS O_WRONLY | O_CREAT | O_TRUNC | O_BINARY
#define READWRITEFLAGS O_RDWR | O_BINARY

#define S_ISDIR(x) ((x) & _S_IFDIR)
#define S_ISREG(x) ((x) & _S_IFREG)

#include "win32/getopt.h"
#if defined(_MSC_VER)
#include "win32/asprintf.h"
#endif
// #define lseek _lseeki64
#define mkdir(a, b) customMkdir(a)

typedef __int32 int32_t;
typedef __int64 xoff_t;
#else
#error unknown target, cannot compile!
#endif

#if defined(_XBOX) || defined(XBOX)
#ifndef USE_BIG_ENDIAN
#define USE_BIG_ENDIAN 1
#endif
#endif

static uint16_t swap16_value(uint16_t value)
{
	return (uint16_t)((value << 8) | (value >> 8));
}

static uint32_t swap32_value(uint32_t value)
{
	return ((value & 0x000000ffu) << 24) |
		   ((value & 0x0000ff00u) << 8) |
		   ((value & 0x00ff0000u) >> 8) |
		   ((value & 0xff000000u) >> 24);
}

#define swap16(n) ((n) = swap16_value((uint16_t)(n)))
#define swap32(n) ((n) = swap32_value((uint32_t)(n)))

#ifdef USE_BIG_ENDIAN
#define big16(n)
#define big32(n)
#define little16(n) swap16(n)
#define little32(n) swap32(n)
#else
#define big16(n) swap16(n)
#define big32(n) swap32(n)
#define little16(n)
#define little32(n)
#endif

#define DEBUG_VERIFY_XISO 0
#define DEBUG_OPTIMIZE_XISO 0
#define DEBUG_TRAVERSE_XISO_DIR 0

#ifndef DEBUG
#define DEBUG 0
#endif

// #include <stdbool.h>

#ifndef nil
#define nil 0
#endif

#define exiso_version "2.7.1 (01.11.14)"
#define VERSION_LENGTH 16

#define banner "extract-xiso v" exiso_version " for " exiso_target " - written by in <in@fishtank.com>\n"

// #define exiso_log \
// 	if (!s_quiet) \
// 	printf

#define exiso_log \
	if (!s_quiet) \
	dprintf

#define flush()   \
	if (!s_quiet) \
	fflush(stdout)

#define mem_err()                                             \
	{                                                         \
		log_err(__FILE__, __LINE__, "out of memory error\n"); \
		err = 1;                                              \
	}
#define read_err()                                                        \
	{                                                                     \
		log_err(__FILE__, __LINE__, "read error: %s\n", strerror(errno)); \
		err = 1;                                                          \
	}
#define seek_err()                                                        \
	{                                                                     \
		log_err(__FILE__, __LINE__, "seek error: %s\n", strerror(errno)); \
		err = 1;                                                          \
	}
#define write_err()                                                        \
	{                                                                      \
		log_err(__FILE__, __LINE__, "write error: %s\n", strerror(errno)); \
		err = 1;                                                           \
	}
#define rread_err()                                                  \
	{                                                                \
		log_err(__FILE__, __LINE__, "unable to read remote file\n"); \
		err = 1;                                                     \
	}
#define rwrite_err()                                                     \
	{                                                                    \
		log_err(__FILE__, __LINE__, "unable to write to remote file\n"); \
		err = 1;                                                         \
	}
#define unknown_err()                                                         \
	{                                                                         \
		log_err(__FILE__, __LINE__, "an unrecoverable error has occurred\n"); \
		err = 1;                                                              \
	}
#define open_err(in_file)                                                               \
	{                                                                                   \
		log_err(__FILE__, __LINE__, "open error: %s %s\n", (in_file), strerror(errno)); \
		err = 1;                                                                        \
	}
#define chdir_err(in_dir)                                                                                 \
	{                                                                                                     \
		log_err(__FILE__, __LINE__, "unable to change to directory %s: %s\n", (in_dir), strerror(errno)); \
		err = 1;                                                                                          \
	}
#define mkdir_err(in_dir)                                                                              \
	{                                                                                                  \
		log_err(__FILE__, __LINE__, "unable to create directory %s: %s\n", (in_dir), strerror(errno)); \
		err = 1;                                                                                       \
	}
#define ropen_err(in_file)                                                         \
	{                                                                              \
		log_err(__FILE__, __LINE__, "unable to open remote file %s\n", (in_file)); \
		err = 1;                                                                   \
	}
#define rchdir_err(in_dir)                                                                  \
	{                                                                                       \
		log_err(__FILE__, __LINE__, "unable to change to remote directory %s\n", (in_dir)); \
		err = 1;                                                                            \
	}
#define rmkdir_err(in_dir)                                                               \
	{                                                                                    \
		log_err(__FILE__, __LINE__, "unable to create remote directory %s\n", (in_dir)); \
		err = 1;                                                                         \
	}
#define misc_err(in_format, a, b, c)                             \
	{                                                            \
		log_err(__FILE__, __LINE__, (in_format), (a), (b), (c)); \
		err = 1;                                                 \
	}

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#define GLOBAL_LSEEK_OFFSET 0x0FD90000ul
#define XGD3_LSEEK_OFFSET 0x02080000ul
#define XGD1_LSEEK_OFFSET 0x18300000ul

#define n_sectors(size) ((size) / XISO_SECTOR_SIZE + ((size) % XISO_SECTOR_SIZE ? 1 : 0))

#define XISO_HEADER_DATA "MICROSOFT*XBOX*MEDIA"
#define XISO_HEADER_DATA_LENGTH 20
#define XISO_HEADER_OFFSET 0x10000

#define XISO_FILE_MODULUS 0x10000

#define XISO_ROOT_DIRECTORY_SECTOR 0x108

#define XISO_OPTIMIZED_TAG_OFFSET 31337
#define XISO_OPTIMIZED_TAG "in!xiso!" exiso_version
#define XISO_OPTIMIZED_TAG_LENGTH (8 + VERSION_LENGTH)
#define XISO_OPTIMIZED_TAG_LENGTH_MIN 7

#define XISO_ATTRIBUTES_SIZE 1
#define XISO_FILENAME_LENGTH_SIZE 1
#define XISO_TABLE_OFFSET_SIZE 2
#define XISO_SECTOR_OFFSET_SIZE 4
#define XISO_DIRTABLE_SIZE 4
#define XISO_FILESIZE_SIZE 4
#define XISO_DWORD_SIZE 4
#define XISO_FILETIME_SIZE 8

#define XISO_SECTOR_SIZE 2048
#define XISO_UNUSED_SIZE 0x7c8

#define XISO_FILENAME_OFFSET 14
#define XISO_FILENAME_LENGTH_OFFSET (XISO_FILENAME_OFFSET - 1)
#define XISO_FILENAME_MAX_CHARS 255

#define XISO_ATTRIBUTE_RO 0x01
#define XISO_ATTRIBUTE_HID 0x02
#define XISO_ATTRIBUTE_SYS 0x04
#define XISO_ATTRIBUTE_DIR 0x10
#define XISO_ATTRIBUTE_ARC 0x20
#define XISO_ATTRIBUTE_NOR 0x80

#define XISO_PAD_BYTE 0xff
#define XISO_PAD_SHORT 0xffff

#define XISO_MEDIA_ENABLE "\xe8\xca\xfd\xff\xff\x85\xc0\x7d"
#define XISO_MEDIA_ENABLE_BYTE '\xeb'
#define XISO_MEDIA_ENABLE_LENGTH 8
#define XISO_MEDIA_ENABLE_BYTE_POS 7

#define EMPTY_SUBDIRECTORY ((dir_node_avl *)1)

#define READWRITE_BUFFER_SIZE 0x02000000 // Increased for more SPEED and POWER!!!

#define DEBUG_DUMP_DIRECTORY "/Volumes/c/xbox/iso/exiso"

#define GETOPT_STRING "c:d:Dhlmp:qQrsvx"

typedef enum avl_skew
{
	k_no_skew,
	k_left_skew,
	k_right_skew
} avl_skew;
typedef enum avl_result
{
	no_err,
	k_avl_error,
	k_avl_balanced
} avl_result;
typedef enum avl_traversal_method
{
	k_prefix,
	k_infix,
	k_postfix
} avl_traversal_method;

typedef enum bm_constants
{
	k_default_alphabet_size = 256
} bm_constants;

typedef enum modes
{
	k_generate_avl,
	k_extract,
	k_list,
	k_rewrite
} modes;
typedef enum errors
{
	err_end_of_sector = -5001,
	err_iso_rewritten = -5002,
	err_iso_no_files = -5003
} errors;

typedef void (*progress_callback)(xoff_t in_current_value, xoff_t in_final_value);
typedef int (*traversal_callback)(void *in_node, void *in_context, long in_depth);

typedef struct dir_node dir_node;
typedef struct create_list create_list;
typedef struct dir_node_avl dir_node_avl;

struct dir_node
{
	dir_node *left;
	dir_node *parent;
	dir_node_avl *avl_node;

	char *filename;

	uint16_t r_offset;
	uint8_t attributes;
	uint8_t filename_length;

	uint32_t file_size;
	uint32_t start_sector;
};

struct dir_node_avl
{
	uint32_t offset;
	xoff_t dir_start;

	char *filename;
	uint32_t file_size;
	uint32_t start_sector;
	dir_node_avl *subdirectory;

	uint32_t old_start_sector;

	avl_skew skew;
	dir_node_avl *left;
	dir_node_avl *right;
};

struct create_list
{
	char *path;
	char *name;
	create_list *next;
};

typedef struct FILE_TIME
{
	uint32_t l;
	uint32_t h;
} FILE_TIME;

typedef struct wdsafp_context
{
	xoff_t dir_start;
	uint32_t *current_sector;
} wdsafp_context;

typedef struct write_tree_context
{
	int xiso;
	char *path;
	int from;
	progress_callback progress;
	xoff_t final_bytes;
} write_tree_context;

int log_err(const char *in_file, int in_line, const char *in_format, ...);
void avl_rotate_left(dir_node_avl **in_root);
void avl_rotate_right(dir_node_avl **in_root);
int avl_compare_key(char *in_lhs, char *in_rhs);
avl_result avl_left_grown(dir_node_avl **in_root);
avl_result avl_right_grown(dir_node_avl **in_root);
dir_node_avl *avl_fetch(dir_node_avl *in_root, char *in_filename);
avl_result avl_insert(dir_node_avl **in_root, dir_node_avl *in_node);
int avl_traverse_depth_first(dir_node_avl *in_root, traversal_callback in_callback, void *in_context, avl_traversal_method in_method, long in_depth);

void boyer_moore_done();
char *boyer_moore_search(char *in_text, long in_text_len);
int boyer_moore_init(char *in_pattern, long in_pat_len, long in_alphabet_size);

int free_dir_node_avl(void *in_dir_node_avl, void *, long);
int extract_file(int in_xiso, dir_node *in_file, modes in_mode, char *path);
int decode_xiso(char *in_xiso, char *in_path, modes in_mode, char **out_iso_path, bool in_ll_compat);
int verify_xiso(int in_xiso, uint32_t *out_root_dir_sector, uint32_t *out_root_dir_size, char *in_iso_name);
int traverse_xiso(int in_xiso, dir_node *in_dir_node, xoff_t in_dir_start, char *in_path, modes in_mode, dir_node_avl **in_root, bool in_ll_compat);
int create_xiso(char *in_root_directory, char *in_output_directory, dir_node_avl *in_root, int in_xiso, char **out_iso_path, char *in_name, progress_callback in_progress_callback);

FILE_TIME *alloc_filetime_now(void);
int generate_avl_tree_local(dir_node_avl **out_root, int *io_n);
int generate_avl_tree_remote(dir_node_avl **out_root, int *io_n);
int write_directory(dir_node_avl *in_avl, int in_xiso, int in_depth);
int write_file(dir_node_avl *in_avl, write_tree_context *in_context, int in_depth);
int write_tree(dir_node_avl *in_avl, write_tree_context *in_context, int in_depth);
int calculate_total_files_and_bytes(dir_node_avl *in_avl, void *in_context, int in_depth);
int calculate_directory_size(dir_node_avl *in_avl, uint32_t *out_size, long in_depth);
int calculate_directory_requirements(dir_node_avl *in_avl, void *in_context, int in_depth);
int calculate_directory_offsets(dir_node_avl *in_avl, uint32_t *io_context, int in_depth);
int write_dir_start_and_file_positions(dir_node_avl *in_avl, wdsafp_context *io_context, int in_depth);
int write_volume_descriptors(int in_xiso, uint32_t in_total_sectors);

#if DEBUG
void write_sector(int in_xiso, xoff_t in_start, char *in_name, char *in_extension);
#endif

static long s_pat_len;
static bool s_quiet = false;
static char *s_pattern = nil;
static long *s_gs_table = nil;
static long *s_bc_table = nil;
static xoff_t s_total_bytes = 0;
static int s_total_files = 0;
static char *s_copy_buffer = nil;
static bool s_real_quiet = false;
static bool s_media_enable = true;
static xoff_t s_total_bytes_all_isos = 0;
static int s_total_files_all_isos = 0;
static bool s_warned = 0;

static bool s_remove_systemupdate = false;
static char *s_systemupdate = "$SystemUpdate";

static xoff_t s_xbox_disc_lseek = 0;

#if 0 // #pragma mark - inserts a spacer in the function popup of certain text editors (i.e. mine ;-)
#pragma mark -
#endif

int extractIso(const char *isoPath, const char *pathToExtractTo)
{
	struct stat sb;
	create_list *create = nil, *p, *q, **r;
	int i, fd, opt_char, err = 0, isos = 0;
	bool extract = true, rewrite = false, free_user = false, free_pass = false, x_seen = false, _delete = false, optimized;
	char *cwd = nil, *path = nil, *buf = nil, *new_iso_path = nil, tag[XISO_OPTIMIZED_TAG_LENGTH * sizeof(long)];

	x_seen = true;

	if (!err)
	{

		exiso_log("%s", banner);

		if ((extract) && (s_copy_buffer = (char *)malloc(READWRITE_BUFFER_SIZE)) == nil)
			mem_err();
	}

	if (pathToExtractTo != NULL)
	{
		path = strdup(pathToExtractTo);

		if ((err = customForceMkdir(path)))
			mkdir_err(path);
	}

	++isos;
	exiso_log("\n");
	s_total_bytes = s_total_files = 0;

	if (!err)
	{
		optimized = false;

		if ((fd = customOpen(isoPath, READFLAGS, 0)) == -1)
			open_err(isoPath);
		if (!err && customLseek(fd, (xoff_t)XISO_OPTIMIZED_TAG_OFFSET, SEEK_SET) == -1)
			seek_err();
		if (!err && customRead(fd, tag, XISO_OPTIMIZED_TAG_LENGTH) != XISO_OPTIMIZED_TAG_LENGTH)
			read_err();

		if (fd != -1)
			customClose(fd);

		if (!err)
		{
			tag[XISO_OPTIMIZED_TAG_LENGTH] = 0;

			if (!strncmp(tag, XISO_OPTIMIZED_TAG, XISO_OPTIMIZED_TAG_LENGTH_MIN))
				optimized = true;

			{
				// the order of the mutually exclusive options here is important, the extract ? k_extract : k_list test *must* be the final comparison
				if (!err)
					err = decode_xiso((char *)isoPath, path, extract ? k_extract : k_list, nil, !optimized);
			}
		}
	}

	if (!err)
		exiso_log("\n%u files in %s total %lld bytes\n", s_total_files, rewrite ? new_iso_path : isoPath, (long long int)s_total_bytes);

	if (new_iso_path)
	{
		if (!err)
			exiso_log("\n%s successfully rewritten%s%s\n", isoPath, path ? " as " : ".", path ? new_iso_path : "");

		free(new_iso_path);
		new_iso_path = nil;
	}

	if (err == err_iso_no_files)
		err = 0;
	//}

	if (!err && isos > 1)
		exiso_log("\n%u files in %u xiso's total %lld bytes\n", s_total_files_all_isos, isos, (long long int)s_total_bytes_all_isos);
	if (s_warned)
		exiso_log("\nWARNING:  Warning(s) were issued during execution--review stderr!\n");

	boyer_moore_done();

	if (s_copy_buffer)
		free(s_copy_buffer);
	if (path)
		free(path);

	return err;
}

int log_err(const char *in_file, int in_line, const char *in_format, ...)
{
	va_list ap;
	// va_list ap2;
	char *format;
	int ret;

#if DEBUG
	asprintf(&format, "%s:%u %s", in_file, in_line, in_format);
#else
	format = (char *)in_format;
#endif

	// if (s_real_quiet)
	// 	ret = 0;
	// else
	{
		va_start(ap, in_format);

		ret = vfprintf(stdout, format, ap);
		va_end(ap);

		va_start(ap, in_format);

		FILE *fp = NULL;
		errno_t err = fopen_s(&fp, LOG_FILE_PATH, "a+");
		if (fp)
		{
			ret = vfprintf(fp, format, ap);
			fclose(fp);
		}
		va_end(ap);
	}

#if DEBUG
	free(format);
#endif

	return ret;
}

#if 0
#pragma mark -
#endif

int verify_xiso(int in_xiso, uint32_t *out_root_dir_sector, uint32_t *out_root_dir_size, char *in_iso_name)
{
	int err = 0;
	char buffer[XISO_HEADER_DATA_LENGTH];

	if (customLseek(in_xiso, (xoff_t)XISO_HEADER_OFFSET, SEEK_SET) == -1)
		seek_err();
	if (!err && customRead(in_xiso, buffer, XISO_HEADER_DATA_LENGTH) != XISO_HEADER_DATA_LENGTH)
		read_err();
	if (!err && memcmp(buffer, XISO_HEADER_DATA, XISO_HEADER_DATA_LENGTH))
	{
		if (customLseek(in_xiso, (xoff_t)XISO_HEADER_OFFSET + GLOBAL_LSEEK_OFFSET, SEEK_SET) == -1)
			seek_err();
		if (!err && customRead(in_xiso, buffer, XISO_HEADER_DATA_LENGTH) != XISO_HEADER_DATA_LENGTH)
			read_err();
		if (!err && memcmp(buffer, XISO_HEADER_DATA, XISO_HEADER_DATA_LENGTH))
		{
			if (customLseek(in_xiso, (xoff_t)XISO_HEADER_OFFSET + XGD3_LSEEK_OFFSET, SEEK_SET) == -1)
				seek_err();
			if (!err && customRead(in_xiso, buffer, XISO_HEADER_DATA_LENGTH) != XISO_HEADER_DATA_LENGTH)
				read_err();
			if (!err && memcmp(buffer, XISO_HEADER_DATA, XISO_HEADER_DATA_LENGTH))
			{
				if (customLseek(in_xiso, (xoff_t)XISO_HEADER_OFFSET + XGD1_LSEEK_OFFSET, SEEK_SET) == -1)
					seek_err();
				if (!err && customRead(in_xiso, buffer, XISO_HEADER_DATA_LENGTH) != XISO_HEADER_DATA_LENGTH)
					read_err();
				if (!err && memcmp(buffer, XISO_HEADER_DATA, XISO_HEADER_DATA_LENGTH))
					misc_err("%s does not appear to be a valid xbox iso image\n", in_iso_name, 0, 0) else s_xbox_disc_lseek = XGD1_LSEEK_OFFSET;
			}
			else
				s_xbox_disc_lseek = XGD3_LSEEK_OFFSET;
		}
		else
			s_xbox_disc_lseek = GLOBAL_LSEEK_OFFSET;
	}
	else
		s_xbox_disc_lseek = 0;

	// read root directory information
	if (!err && customRead(in_xiso, out_root_dir_sector, XISO_SECTOR_OFFSET_SIZE) != XISO_SECTOR_OFFSET_SIZE)
		read_err();
	if (!err && customRead(in_xiso, out_root_dir_size, XISO_DIRTABLE_SIZE) != XISO_DIRTABLE_SIZE)
		read_err();

	little32(*out_root_dir_sector);
	little32(*out_root_dir_size);

	// seek to header tail and verify media tag
	if (!err && customLseek(in_xiso, (xoff_t)XISO_FILETIME_SIZE + XISO_UNUSED_SIZE, SEEK_CUR) == -1)
		seek_err();
	if (!err && customRead(in_xiso, buffer, XISO_HEADER_DATA_LENGTH) != XISO_HEADER_DATA_LENGTH)
		read_err();
	if (!err && memcmp(buffer, XISO_HEADER_DATA, XISO_HEADER_DATA_LENGTH))
		misc_err("%s appears to be corrupt\n", in_iso_name, 0, 0);

	// seek to root directory sector
	if (!err)
	{
		if (!*out_root_dir_sector && !*out_root_dir_size)
		{
			exiso_log("xbox image %s contains no files.\n", in_iso_name);
			err = err_iso_no_files;
		}
		else
		{
			if (customLseek(in_xiso, (xoff_t)*out_root_dir_sector * XISO_SECTOR_SIZE, SEEK_SET) == -1)
				seek_err();
		}
	}

	return err;
}

int decode_xiso(char *in_xiso, char *in_path, modes in_mode, char **out_iso_path, bool in_ll_compat)
{
	dir_node_avl *root = nil;
	bool repair = false;
	uint32_t root_dir_sect, root_dir_size;
	int xiso, err = 0, len, path_len = 0, add_slash = 0;
	char *buf, *cwd = nil, *name = nil, *short_name = nil, *iso_name, *folder = nil;

	if ((xiso = customOpen(in_xiso, READFLAGS, 0)) == -1)
		open_err(in_xiso);

	if (strstr(in_xiso, ".001") != NULL)
	{										 // if split ISO
		in_xiso[strlen(in_xiso) - 4] = '\0'; // remove .001 part of file name
	}

	if (!err)
	{
		len = (int)strlen(in_xiso);

		// if (in_mode == k_rewrite)
		// {
		// 	in_xiso[len -= 4] = 0;
		// 	repair = true;
		// }

		for (name = &in_xiso[len]; name >= in_xiso && *name != PATH_CHAR; --name)
			;
		++name;

		len = (int)strlen(name);

		// create a directory of the same name as the file we are working on, minus the ".iso" portion
		if (len > 4 && strcasecmp(&name[len - 4], ".iso") == 0)
		{
			name[len -= 4] = 0;
			if ((short_name = strdup(name)) == nil)
				mem_err();
			name[len] = '.';
		}
	}

	if (!err && !len)
		misc_err("invalid xiso image name: %s\n", in_xiso, 0, 0);

	if (!err && in_mode == k_extract)
	{
		if ((cwd = getcwd(nil, 0)) == nil)
			mem_err();

		if (!err && in_path)
		{
			if (!err && mkdir(in_path, 0755))
				;
			if (!err && chdir(in_path) == -1)
				chdir_err(in_path);
		}
		else if (!err)
		{
			char *folderEnd;

			if ((folder = strdup(in_xiso)) == nil)
				mem_err();
			if (!err && (folderEnd = strrchr(folder, PATH_CHAR)) != nil)
			{
				*(folderEnd + 1) = '\0';
				if (chdir(folder) == -1)
					chdir_err(folder);
			}
		}
	}

	if (!err)
		err = verify_xiso(xiso, &root_dir_sect, &root_dir_size, name);

	iso_name = short_name ? short_name : name;

	if (!err && in_mode != k_rewrite)
	{
		exiso_log("%s %s:\n\n", in_mode == k_extract ? "extracting" : "listing", name);

		if (in_mode == k_extract)
		{
			if (!in_path)
			{
				if ((err = mkdir(iso_name, 0755)))
					mkdir_err(iso_name);
				if (!err && (err = chdir(iso_name)))
					chdir_err(iso_name);
			}
		}
	}

	if (!err && root_dir_sect && root_dir_size)
	{
		if (in_path)
		{
			path_len = (int)strlen(in_path);
			if (in_path[path_len - 1] != PATH_CHAR)
				++add_slash;
		}

		if ((buf = (char *)malloc(path_len + add_slash + strlen(iso_name) + 2)) == nil)
			mem_err();

		if (!err)
		{
			sprintf(buf, "%s%s%s%c", in_path ? in_path : "", add_slash && (!in_path) ? PATH_CHAR_STR : "", in_mode != k_list && (!in_path) ? iso_name : "", PATH_CHAR);

			if (!err && customLseek(xiso, (xoff_t)root_dir_sect * XISO_SECTOR_SIZE + s_xbox_disc_lseek, SEEK_SET) == -1)
				seek_err();
			if (!err)
				err = traverse_xiso(xiso, nil, (xoff_t)root_dir_sect * XISO_SECTOR_SIZE + s_xbox_disc_lseek, buf, in_mode, nil, in_ll_compat);

			free(buf);
		}
	}

	if (err == err_iso_rewritten)
		err = 0;
	if (err)
		misc_err("failed to %s xbox iso image %s\n", in_mode == k_rewrite ? "rewrite" : in_mode == k_extract ? "extract"
																											 : "list",
				 name, 0);

	if (xiso != -1)
		customClose(xiso);

	if (short_name)
		free(short_name);
	if (folder)
		free(folder);
	if (cwd)
	{
		chdir(cwd);
		free(cwd);
	}

	if (repair)
		in_xiso[strlen(in_xiso)] = '.';

	return err;
}

int traverse_xiso(int in_xiso, dir_node *in_dir_node, xoff_t in_dir_start, char *in_path, modes in_mode, dir_node_avl **in_root, bool in_ll_compat)
{
	dir_node_avl *avl;
	char *path;
	xoff_t curpos;
	dir_node subdir;
	dir_node *dir, node;
	int err = 0, sector;
	uint16_t l_offset = 0, tmp;

	if (in_dir_node == nil)
		in_dir_node = &node;

	memset(dir = in_dir_node, 0, sizeof(dir_node));

read_entry:

	if (!err && customRead(in_xiso, &tmp, XISO_TABLE_OFFSET_SIZE) != XISO_TABLE_OFFSET_SIZE)
		read_err();

	if (!err)
	{
		if (tmp == XISO_PAD_SHORT)
		{
			if (l_offset == 0)
			{ // Directory is empty
				if (in_mode == k_generate_avl)
				{
					avl_insert(in_root, EMPTY_SUBDIRECTORY);
				}
				goto end_traverse;
			}

			l_offset = l_offset * XISO_DWORD_SIZE + (XISO_SECTOR_SIZE - (l_offset * XISO_DWORD_SIZE) % XISO_SECTOR_SIZE);
			err = customLseek(in_xiso, in_dir_start + (xoff_t)l_offset, SEEK_SET) == -1 ? 1 : 0;

			if (!err)
				goto read_entry; // me and my silly comments
		}
		else
		{
			l_offset = tmp;
		}
	}

	if (!err && customRead(in_xiso, &dir->r_offset, XISO_TABLE_OFFSET_SIZE) != XISO_TABLE_OFFSET_SIZE)
		read_err();
	if (!err && customRead(in_xiso, &dir->start_sector, XISO_SECTOR_OFFSET_SIZE) != XISO_SECTOR_OFFSET_SIZE)
		read_err();
	if (!err && customRead(in_xiso, &dir->file_size, XISO_FILESIZE_SIZE) != XISO_FILESIZE_SIZE)
		read_err();
	if (!err && customRead(in_xiso, &dir->attributes, XISO_ATTRIBUTES_SIZE) != XISO_ATTRIBUTES_SIZE)
		read_err();
	if (!err && customRead(in_xiso, &dir->filename_length, XISO_FILENAME_LENGTH_SIZE) != XISO_FILENAME_LENGTH_SIZE)
		read_err();

	if (!err)
	{
		little16(l_offset);
		little16(dir->r_offset);
		little32(dir->file_size);
		little32(dir->start_sector);

		if ((dir->filename = (char *)malloc(dir->filename_length + 1)) == nil)
			mem_err();
	}

	if (!err)
	{
		if (customRead(in_xiso, dir->filename, dir->filename_length) != dir->filename_length)
			read_err();
		if (!err)
		{
			dir->filename[dir->filename_length] = 0;

			// security patch (Chris Bainbridge), modified by in to support "...", etc. 02.14.06 (in)
			if (!strcmp(dir->filename, ".") || !strcmp(dir->filename, "..") || strchr(dir->filename, '/') || strchr(dir->filename, '\\'))
			{
				log_err(__FILE__, __LINE__, "filename '%s' contains invalid character(s), aborting.", dir->filename);
				exit(1);
			}
		}
	}

	if (!err && in_mode == k_generate_avl)
	{
		if ((avl = (dir_node_avl *)malloc(sizeof(dir_node_avl))) == nil)
			mem_err();
		if (!err)
		{
			memset(avl, 0, sizeof(dir_node_avl));

			if ((avl->filename = strdup(dir->filename)) == nil)
				mem_err();
		}
		if (!err)
		{
			dir->avl_node = avl;

			avl->file_size = dir->file_size;
			avl->old_start_sector = dir->start_sector;

			if (avl_insert(in_root, avl) == k_avl_error)
				misc_err("this iso appears to be corrupt\n", 0, 0, 0);
		}
	}

	if (!err && l_offset)
	{
		in_ll_compat = false;

		if ((dir->left = (dir_node *)malloc(sizeof(dir_node))) == nil)
			mem_err();
		if (!err)
		{
			memset(dir->left, 0, sizeof(dir_node));
			if (customLseek(in_xiso, in_dir_start + (xoff_t)l_offset * XISO_DWORD_SIZE, SEEK_SET) == -1)
				seek_err();
		}
		if (!err)
		{
			dir->left->parent = dir;
			dir = dir->left;

			goto read_entry;
		}
	}

left_processed:

	if (dir->left)
	{
		free(dir->left);
		dir->left = nil;
	}

	if (!err && (curpos = customLseek(in_xiso, 0, SEEK_CUR)) == -1)
		seek_err();

	if (!err)
	{
		if (dir->attributes & XISO_ATTRIBUTE_DIR)
		{
			if (in_path)
			{
				if ((path = (char *)malloc(strlen(in_path) + dir->filename_length + 2)) == nil)
					mem_err();

				if (!err)
				{
					sprintf(path, "%s%s%c", in_path, dir->filename, PATH_CHAR);
					if (customLseek(in_xiso, (xoff_t)dir->start_sector * XISO_SECTOR_SIZE + s_xbox_disc_lseek, SEEK_SET) == -1)
						seek_err();
				}
			}
			else
				path = nil;

			if (!err)
			{
				if (!s_remove_systemupdate || !strstr(dir->filename, s_systemupdate))
				{
					if (in_mode == k_extract)
					{
						if ((err = mkdir(dir->filename, 0755)))
							mkdir_err(dir->filename);
						if (!err && (err = chdir(dir->filename)))
							chdir_err(dir->filename);
					}
					if (!err && in_mode != k_generate_avl)
					{
						exiso_log("%s%s%s%s (0 bytes)%s", in_mode == k_extract ? "creating " : "", in_path, dir->filename, PATH_CHAR_STR, in_mode == k_extract ? " [OK]" : "");
						flush();
						exiso_log("\n");
					}
				}
			}

			if (!err)
			{
				memcpy(&subdir, dir, sizeof(dir_node));

				subdir.parent = nil;
				if (!err && dir->file_size > 0)
					err = traverse_xiso(in_xiso, &subdir, (xoff_t)dir->start_sector * XISO_SECTOR_SIZE + s_xbox_disc_lseek, path, in_mode, in_mode == k_generate_avl ? &dir->avl_node->subdirectory : nil, in_ll_compat);

				if (!s_remove_systemupdate || !strstr(dir->filename, s_systemupdate))
				{

					if (!err && in_mode == k_extract && (err = chdir("..")))
						chdir_err("..");
				}
			}

			if (path)
				free(path);
		}
		else if (in_mode != k_generate_avl)
		{
			if (!err)
			{
				if (!s_remove_systemupdate || !strstr(in_path, s_systemupdate))
				{

					if (in_mode == k_extract)
					{
						err = extract_file(in_xiso, dir, in_mode, in_path);
					}
					else
					{
						exiso_log("%s%s%s (%u bytes)%s\n", in_mode == k_extract ? "extracting " : "", in_path, dir->filename, dir->file_size, "");
						flush();
						exiso_log("\n");
					}

					++s_total_files;
					++s_total_files_all_isos;
					s_total_bytes += dir->file_size;
					s_total_bytes_all_isos += dir->file_size;
				}
			}
		}
	}

	if (!err && dir->r_offset)
	{
		// compatibility for iso's built as linked lists (bleh!)
		if (in_ll_compat && (xoff_t)dir->r_offset * XISO_DWORD_SIZE / XISO_SECTOR_SIZE > (sector = (int)((curpos - in_dir_start) / XISO_SECTOR_SIZE)))
			dir->r_offset = sector * (XISO_SECTOR_SIZE / XISO_DWORD_SIZE) + (XISO_SECTOR_SIZE / XISO_DWORD_SIZE);

		if (!err && customLseek(in_xiso, in_dir_start + (xoff_t)dir->r_offset * XISO_DWORD_SIZE, SEEK_SET) == -1)
			seek_err();
		if (!err)
		{
			if (dir->filename)
			{
				free(dir->filename);
				dir->filename = nil;
			}

			l_offset = dir->r_offset;

			goto read_entry;
		}
	}

end_traverse:

	if (dir->filename)
		free(dir->filename);

	if ((dir = dir->parent))
		goto left_processed;

	return err;
}

#if 0
#pragma mark -
#endif

avl_result avl_insert(dir_node_avl **in_root, dir_node_avl *in_node)
{
	avl_result tmp;
	int result;

	if (*in_root == nil)
	{
		*in_root = in_node;
		return k_avl_balanced;
	}

	result = avl_compare_key(in_node->filename, (*in_root)->filename);

	if (result < 0)
		return (tmp = avl_insert(&(*in_root)->left, in_node)) == k_avl_balanced ? avl_left_grown(in_root) : tmp;
	if (result > 0)
		return (tmp = avl_insert(&(*in_root)->right, in_node)) == k_avl_balanced ? avl_right_grown(in_root) : tmp;

	return k_avl_error;
}

avl_result avl_left_grown(dir_node_avl **in_root)
{
	switch ((*in_root)->skew)
	{
	case k_left_skew:
	{
		if ((*in_root)->left->skew == k_left_skew)
		{
			(*in_root)->skew = (*in_root)->left->skew = k_no_skew;
			avl_rotate_right(in_root);
		}
		else
		{
			switch ((*in_root)->left->right->skew)
			{
			case k_left_skew:
			{
				(*in_root)->skew = k_right_skew;
				(*in_root)->left->skew = k_no_skew;
			}
			break;

			case k_right_skew:
			{
				(*in_root)->skew = k_no_skew;
				(*in_root)->left->skew = k_left_skew;
			}
			break;

			default:
			{
				(*in_root)->skew = k_no_skew;
				(*in_root)->left->skew = k_no_skew;
			}
			break;
			}
			(*in_root)->left->right->skew = k_no_skew;
			avl_rotate_left(&(*in_root)->left);
			avl_rotate_right(in_root);
		}
	}
		return no_err;

	case k_right_skew:
	{
		(*in_root)->skew = k_no_skew;
	}
		return no_err;

	default:
	{
		(*in_root)->skew = k_left_skew;
	}
		return k_avl_balanced;
	}
}

avl_result avl_right_grown(dir_node_avl **in_root)
{
	switch ((*in_root)->skew)
	{
	case k_left_skew:
	{
		(*in_root)->skew = k_no_skew;
	}
		return no_err;

	case k_right_skew:
	{
		if ((*in_root)->right->skew == k_right_skew)
		{
			(*in_root)->skew = (*in_root)->right->skew = k_no_skew;
			avl_rotate_left(in_root);
		}
		else
		{
			switch ((*in_root)->right->left->skew)
			{
			case k_left_skew:
			{
				(*in_root)->skew = k_no_skew;
				(*in_root)->right->skew = k_right_skew;
			}
			break;

			case k_right_skew:
			{
				(*in_root)->skew = k_left_skew;
				(*in_root)->right->skew = k_no_skew;
			}
			break;

			default:
			{
				(*in_root)->skew = k_no_skew;
				(*in_root)->right->skew = k_no_skew;
			}
			break;
			}
			(*in_root)->right->left->skew = k_no_skew;
			avl_rotate_right(&(*in_root)->right);
			avl_rotate_left(in_root);
		}
	}
		return no_err;

	default:
	{
		(*in_root)->skew = k_right_skew;
	}
		return k_avl_balanced;
	}
}

void avl_rotate_left(dir_node_avl **in_root)
{
	dir_node_avl *tmp = *in_root;

	*in_root = (*in_root)->right;
	tmp->right = (*in_root)->left;
	(*in_root)->left = tmp;
}

void avl_rotate_right(dir_node_avl **in_root)
{
	dir_node_avl *tmp = *in_root;

	*in_root = (*in_root)->left;
	tmp->left = (*in_root)->right;
	(*in_root)->right = tmp;
}

int avl_compare_key(char *in_lhs, char *in_rhs)
{
	char a, b;

	for (;;)
	{
		a = *in_lhs++;
		b = *in_rhs++;

		if (a >= 'a' && a <= 'z')
			a -= 32; // uppercase(a);
		if (b >= 'a' && b <= 'z')
			b -= 32; // uppercase(b);

		if (a)
		{
			if (b)
			{
				if (a < b)
					return -1;
				if (a > b)
					return 1;
			}
			else
				return 1;
		}
		else
			return b ? -1 : 0;
	}
}

#if 0
#pragma mark -
#endif

int boyer_moore_init(char *in_pattern, long in_pat_len, long in_alphabet_size)
{
	long i, j, k, *backup, err = 0;

	s_pattern = in_pattern;
	s_pat_len = in_pat_len;

	if ((s_bc_table = (long *)malloc(in_alphabet_size * sizeof(long))) == nil)
		mem_err();

	if (!err)
	{
		for (i = 0; i < in_alphabet_size; ++i)
			s_bc_table[i] = in_pat_len;
		for (i = 0; i < in_pat_len - 1; ++i)
			s_bc_table[(uint8_t)in_pattern[i]] = in_pat_len - i - 1;

		if ((s_gs_table = (long *)malloc(2 * (in_pat_len + 1) * sizeof(long))) == nil)
			mem_err();
	}

	if (!err)
	{
		backup = s_gs_table + in_pat_len + 1;

		for (i = 1; i <= in_pat_len; ++i)
			s_gs_table[i] = 2 * in_pat_len - i;
		for (i = in_pat_len, j = in_pat_len + 1; i; --i, --j)
		{
			backup[i] = j;

			while (j <= in_pat_len && in_pattern[i - 1] != in_pattern[j - 1])
			{
				if (s_gs_table[j] > in_pat_len - i)
					s_gs_table[j] = in_pat_len - i;
				j = backup[j];
			}
		}
		for (i = 1; i <= j; ++i)
			if (s_gs_table[i] > in_pat_len + j - i)
				s_gs_table[i] = in_pat_len + j - i;

		k = backup[j];

		for (; j <= in_pat_len; k = backup[k])
		{
			for (; j <= k; ++j)
				if (s_gs_table[j] >= k - j + in_pat_len)
					s_gs_table[j] = k - j + in_pat_len;
		}
	}

	return err;
}

void boyer_moore_done()
{
	if (s_bc_table)
	{
		free(s_bc_table);
		s_bc_table = nil;
	}
	if (s_gs_table)
	{
		free(s_gs_table);
		s_gs_table = nil;
	}
}

#if 0
#pragma mark -
#endif

int extract_file(int in_xiso, dir_node *in_file, modes in_mode, char *path)
{
	int err = 0;
	bool warn = false;
	uint32_t i, size, read_size, totalsize = 0, totalpercent = 0;
	int out;

	if (s_remove_systemupdate && strstr(path, s_systemupdate))
	{
		if (!err && customLseek(in_xiso, (xoff_t)in_file->start_sector * XISO_SECTOR_SIZE + s_xbox_disc_lseek, SEEK_SET) == -1)
			seek_err();
	}
	else
	{
		if (in_mode == k_extract)
		{
			if ((out = customOpen(in_file->filename, WRITEFLAGS, 0644)) == -1)
				open_err(in_file->filename);
		}
		else
			err = 1;

		if (!err && customLseek(in_xiso, (xoff_t)in_file->start_sector * XISO_SECTOR_SIZE + s_xbox_disc_lseek, SEEK_SET) == -1)
			seek_err();

		if (!err)
		{
			if (in_file->file_size == 0)
			{
				exiso_log("%s%s%s (0 bytes) [100%%]%s\n", in_mode == k_extract ? "extracting " : "", path, in_file->filename, "");
			}
			else
			{
				i = 0;
				size = min(in_file->file_size, READWRITE_BUFFER_SIZE);
				do
				{
					if ((int)(read_size = customRead(in_xiso, s_copy_buffer, size)) < 0)
					{
						read_err();
						break;
					}
					if (in_mode == k_extract && read_size != 0)
					{
						if (write(out, s_copy_buffer, read_size) != (int)read_size)
						{
							write_err();
							break;
						}
					}
					totalsize += read_size;
					totalpercent = (totalsize * 100.0) / in_file->file_size;
					exiso_log("%s%s%s (%u bytes) [%u%%]%s\n", in_mode == k_extract ? "extracting " : "", path, in_file->filename, in_file->file_size, totalpercent, "");

					i += read_size;
					size = min(in_file->file_size - i, READWRITE_BUFFER_SIZE);
				} while (i < in_file->file_size && read_size > 0);
				if (!err && i < in_file->file_size)
				{
					exiso_log("\nWARNING: File %s is truncated. Reported size: %u bytes, read size: %u bytes!", in_file->filename, in_file->file_size, i);
					in_file->file_size = i;
				}
			}
			if (in_mode == k_extract)
				customClose(out);
		}
	}

	if (!err)
		exiso_log("\n");

	return err;
}

// Found the CD-ROM layout in ECMA-119.  Now burning software should correctly
// detect the format of the xiso and burn it correctly without the user having
// to specify sector sizes and so on.	in 10.29.04

#define ECMA_119_DATA_AREA_START 0x8000
#define ECMA_119_VOLUME_SPACE_SIZE (ECMA_119_DATA_AREA_START + 80)
#define ECMA_119_VOLUME_SET_SIZE (ECMA_119_DATA_AREA_START + 120)
#define ECMA_119_VOLUME_SET_IDENTIFIER (ECMA_119_DATA_AREA_START + 190)
#define ECMA_119_VOLUME_CREATION_DATE (ECMA_119_DATA_AREA_START + 813)

// write_volume_descriptors() assumes that the iso file block from offset
// 0x8000 to 0x8808 has been zeroed prior to entry.

#if DEBUG

void write_sector(int in_xiso, xoff_t in_start, char *in_name, char *in_extension)
{
	ssize_t wrote;
	xoff_t curpos;
	int fp = -1, err = 0;
	char *cwd, *sect = nil, buf[256];

	if ((cwd = getcwd(nil, 0)) == nil)
		mem_err();
	if (!err && chdir(DEBUG_DUMP_DIRECTORY) == -1)
		chdir_err(DEBUG_DUMP_DIRECTORY);

	sprintf(buf, "%llu.%s.%s", in_start, in_name, in_extension ? in_extension : "");

	if (!err && (fp = customOpen(buf, WRITEFLAGS, 0644)) == -1)
		open_err(buf);
	if (!err && (curpos = customLseek(in_xiso, 0, SEEK_CUR)) == -1)
		seek_err();
	if (!err && customLseek(in_xiso, in_start, SEEK_SET) == -1)
		seek_err();

	if (!err && (sect = (char *)malloc(XISO_SECTOR_SIZE)) == nil)
		mem_err();

	if (!err && customRead(in_xiso, sect, XISO_SECTOR_SIZE) != XISO_SECTOR_SIZE)
		read_err();
	if (!err && (wrote = write(fp, sect, XISO_SECTOR_SIZE)) != XISO_SECTOR_SIZE)
		write_err();

	if (!err && customLseek(in_xiso, curpos, SEEK_SET) == -1)
		seek_err();

	if (sect)
		free(sect);
	if (fp != -1)
		customClose(fp);

	if (cwd)
	{
		if (chdir(cwd) == -1)
			chdir_err(cwd);
		free(cwd);
	}
}

#endif
