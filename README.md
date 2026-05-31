# X Store (formerly Free60 Store)

### I am sorry
This project was intentended to be a proof of concept xbox 360 game downloader. I never expected so many people to see the post I put up online about it. I am sorry that my confusing, afterthought of a name caused issues, I never wanted it to do so. I respect the work done by the free60 project, though probably never will fully understand how much personal time and effor went into it. In short, I never wanted to make anybody mad, I just wanted to create a cool piece of homebrew. 

Not affiliated with the [free60 project](https://free60.org)

## What is it?

The Xbox 360 game store is a piece of homebrew for the Xbox 360 that allows games to be directly downloaded onto the Xbox 360.

## Requirements

* A JTAG/RGH hard modded console OR a console running BadUpdate/ABadAvatar
* The Xbox 360 must be connected to the internet

## **Not** Required

* Stealth Server
* Xbox Live
* Local web server

## How to use

1. Download X-store.zip from the releases
2. Extract the .xex and settings.txt files from the ZIP file, and transfer them to your console
3. Set the output path for games in settings.txt. (for example `hdd:\games` or `usb0:\Content\0000000000000000`) This will be where games will be extracted to
4. Launch it using your favourite file manager or dashboard
5. Search for your favourite game
6. Navigate the menu with ↑ and ↓
7. Select a game by pressing A
8. Select a version of the game
9. Press A to begin the download

Currently, this program supports downloading directly from Vimms Lair. While the download speeds can be slow, it contains a variety of games. 

## Updating
If you are on version 0.2.3 or later, you can update to the latest version by selecting `Update X Store` when you first start the program. If you are on an earlier version, download the latest version from the releases page, and replace X-Store.xex on your console with the latest version this github repo. 

## Compiling from the source

[See Compiling](COMPILING.md)

## How the Code Works

[Read the Code Breakdown](how_it_works.md)

## FAQ

* Will this work with BadUpdate/ABadAvatar? Yes, it should work on any homebrew capable Xbox 360 console
* Why is it taking so long? The Xbox 360 is (by modern standards) an old console. Attempting to download large games directly on the console over encrypted HTTPS is not a simple feat
* Does this require a stealth server? No, this program only requires an internet connection, no Xbox Live or Stealth server required
* Does it run in the background? No, you must keep the program open whilst downloading, decompressing and extracting
* Does this work with Aurora/FSD? Yes, this program will work with any dashboard that can launch homebrew apps.
* Why is the user interface so bad? I have very little experience with user interface design, and I have been more focused on getting the backend working. If you are skilled enough to make a better UI, I would very much appreciate it. 

## Credits

Many thanks to all of the open source projects that made this possible, including:

* [XboxTLS](https://github.com/JakobRangel/XboxTLS) by JakobRangel, for encrypted HTTPS communication
* [Simple 360 Nand Flasher](https://github.com/Swizzy/XDK_Projects) by Swizzy, to use as a base project to work from, as well as providing a simple, easy to use interface to output console style text to the screen
* [extract-xiso](https://github.com/XboxDev/extract-xiso) by XboxDev, modified to extract ISO files directly on the Xbox itself
* [LZMA SDK](https://www.7-zip.org/sdk.html), to decompress LZMA and LZMA 2 compressed 7z files
* [Vimm's Lair](https://vimm.net/), without which this entire project would not have been possible

## Disclaimers

* Firstly, while this program should not harm your Xbox 360 in any way, it is still in a very early alpha phase. As with all homebrew, run it at your own risk
* This program is in a very early alpha phase, and should be treated as a proof-of-concept. Bugs should be expected
* Depending on your local law, it may or may not be legal for you to download games with this tool. I take ABSOLUTELY NO RESPONSIBILITY for your actions
* AI. Yes, I used it. While this project is **not** vibecoded, there are many portions of this project where I made use of AI to **assist** in writing parts of the code
* This project has no intention of being affiliated with the free60 project. If you do not approve of this project's name, please let me know

## TODO

* Clean up the very messy code
* Optimise 7z decompression, potentially implimenting multithreading
* Eliminate memory leaks
* Impliment a better UI using xUI
* Add support to download from ROMSFUN using flaresolver proxy - unlikely
* Add support to download from the Internet Archive
* Potentially make use of the [triangle](https://github.com/JakobRangel/Triangle) frontend when it is made open source
* Test to see if DOS 8.3 file name limitations affect FATX

Pull Requests are welcome
