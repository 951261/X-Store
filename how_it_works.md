# How The Code Works

I wanted to share how this code works to make it easier for others to contribute to this project.

## Step-by-Step Breakdown

### Xbox TLS

The core of this project is XboxTLS, a simple, lightweight TLS 1.2 client for the Xbox 360 based on BearSSL. All downloads are done securely with encryption thanks to this project.

### Finding the Game

The code begins by opening a keyboard that allows the user to search for a game. The search string is then sent to Vimm's lair, and the results are downloaded. The user then selects a game to download. The download URL is sent to the main download loop to download the 7zip file.

### Downloading the Game

One of the main reasons very few have tried to created an Xbox 360 game downloader is FAT32/FATX file size limits. Both filesystems store file sizes in a 32 bit field, limiting the maximum file size to ~4 GB. To overcome this, this program splits large files into multiple smaller parts. Games are downloaded into `tmp.7z.XXX`, where XXX is a unique number for each part (tmp.7z.001, tmp.7z.002, etc...).

### Decompressing the Game

Once downloaded, the program moves on to the decompression loop. This code decompresses the downloaded .7z.XXX files, treating them as a single, larger .7z file. This decompression code is based on the LZMA SDK, and is currently compatible with LZMA, LZMA2 and STORE based 7z files. Instead of loading the files into RAM (which there is nowhere near enough of), the decompression is streamed directly from the input file(s) to the output files(s). This prevents consuming the limited RAM on the console. Any output files that are larger than ~4 GB are split into multiple smaller parts (.iso.001, .iso.002, etc...).

### Extracting the ISO

If downloading an Original Xbox or Xbox 360 game, the ISO file needs to be extracted. This is achieved using a heavily modified version of extract-xiso. To handle split .iso files, I created custom wrappers around `open`, `close`, `read`, and other file handling functions. A very peculiar issue I encountered was that under very specific circumstances, the Xbox 360 uses Microsoft DOS 3.8 file naming. When creating or renaming a file on a FAT32 formatted USB in the form `file.abc.def` where `file.abc` is 8 characters in length, the Xbox 360 will create a file called `file.def`. This issue is not unique `open`/`fopen`, it happens within the Kernel itself, with no documented flag to bypass it. Even when renaming a file in Aurora file manager, it is not possible to rename a file to `file.aaa.bbb`. To overcome this, a custom FAT32 driver was added with the ability to rename files by directly accessing the USB at `\Device\MassX`.

Once the ISO file has been extracted, the program exits, and the game should be ready to play.

#### Why not Convert the ISO to a GOD?

Firstly, I prefer the idea of having all of the files exposed, not in a compressed package. Secondly, and more importantly, it was much easier to find an easily portable ISO extract program than an ISO to GOD program. extract-xiso is completely written in C, and is designed to compile for Windows, making it easily portable to the Xbox 360. If you are a developer who prefers GOD games (as most people do) and has some free time, I would be happy to get GOD conversion working. 
