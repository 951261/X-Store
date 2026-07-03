Here is a complete, start to finish guide on how to setup X Store
### Prerequisites 
**Required:**
- A soft modded (BadUpdate, ABadAvatar, etc) or a hard modded (RGH, JTAG, etc) Xbox 360 console.
- An internet connection to your console (ethernet recommened)

**Not required**
- Xbox Live
- Stealth Server

## Setup Process
1) [Download the latest release](https://github.com/951261/X-Store/releases/latest) of the X Store ZIP file.
2) Extract the zip file to somewhere on your PC or phone, such as your Desktop, Documents or Downloads folder.
3) Open the settings.txt file with a text editor such as Notepad
4) In the settings.txt file, there will be some file paths you must set.

    a)    Firstly, you must set the path that Original Xbox games should be extracted to. If you have an Xbox 360 with an internal HDD, then you should set this to something like `original-xbox-path: Hdd:\Games\Original Xbox`. If you want to download the games onto a USB, then set the path to something such as `original-xbox-path: Usb0:\Games\Original Xbox`
   
    b)    Next, you must set the path for Xbox 360 Games. If you have a hard drive, set it to something like `xbox-360-path: Hdd:\Games\Xbox 360`. If you want the games to be installed to a USB (or external USB hard drive), then set the path to something similar to `xbox-360-path: Usb0:\Games\Xbox 360`
   
    c)    The last path you must set is for Xbox Live Arcade Games. If you have an Xbox 360 with an internal HDD, then you should set this to something like `xbla-path: Hdd:\Content\0000000000000000`. If you want to download the games onto a USB, then set the path to something such as `xbla-path: Usb0:\Content\0000000000000000`. For DLC and Title Update downloads to work correctly, you must ensure that you set this path to one of the `Content\0000000000000000` folders, such as the one on your HDD, or the one on a USB drive. 

   All of these paths can be set to wherever you like, so long as they are valid (i.e. `Hdd1:\Path\to\games` is invalid, as it should be `Hdd`, not `Hdd1`). I would recommend to set these paths to wherever you already have your games downloaded/backed up to. If this is your first time getting Xbox 360 games without a physical disk, then you can set the paths to wherever you like (e.g. the examples I gave earlier). 

5) Once you have saved and closed the settings.txt file, you must transfer `settings.txt` and `X-Store.xex` to your console. I reccomend copying them to the same folder that you have your other apps in. (e.g. you could copy both files to `Hdd1:\Apps\X-Store\` or `Usb0:\Apps\X-Store\`.) Both files MUST be in the same folder, else X-Store.xex will not know where to extract your games. Additionally, there MUST NOT be any other `.xex` files in the same folder as `X-Store.xex`

   To transfer `X-Store.xex` and `settings.txt` to your console, there are several methods you can choose from. The easist way is to simply copy the files to a FAT32 formatted USB drive (such as the one you already use for BadUpdate/ABadAvatar) using your PC or phone. You can then either leave the files on the USB, or you can use a file manager (such as Aurora or XEX Menu) to copy them to your console's internal HDD. Another option is to use FTP ([as seen in this guide](https://xbox360rgh.com/rgh-tutorial/rgh-transferring-games-files-via-ftp/)) to copy the files to your console's internal HDD or a USB. Another option is to use Xbox 360 Neighbourhood, however, that is not recommended, as I find it often crashes and/or fails to transfer files.

6) Once the files have all been transfered onto your console, you are almost ready to launch X Store. If you are using Aurora dashboard, you should ensure that the paths you set in settings.txt are also set in Aurora Settings -> Content -> Path. You should also set the scan depth to at least 4. If you do not add the paths in Aurora Settings, then your downloaded games will not automatically appear in Aurora after downloading them. Additionally, you should ensure Aurora can find X-Store.xex so that you can easily launch it from the main Aurora home screen. 

7) You are now ready to launch X Store! Launch it by either using a file manager (XEX Menu, Aurora, FSD, etc) to launch X-Store.xex directly, or launch it directly from the home screen of Aurora, Freestyle Dash or another dashboard (assuming you have correctly setup the paths in your dashboard's settings).

8) Upon launching X Store, you will be greeted with four options. The first option is to download Original Xbox games. The second option is to download Xbox 360 games. These will be downloaded in extracted XEX format, not GOD format. The third option is to download Xbox Live Arcade games, DLCs and Title Updates. These have the smallest download size, hence are usually the fastest to download. The 4th option is to Update X-Store to the latest version. There is currently a bug that is preventing updates from working, so if you select this 4th option, expect it to fail. If you want to exit X Store, simply press B at this menu.

9) After selecting one of the first three options, you will see a keyboard where you can search for a game. It is not case sensitive, but you must correctly spell the full name of the game (e.g. grand theft Auto v). Press the start button to search for the game/DLC/Title Update.

10) After the search results load, use the D-Pad to scroll the menu. When you find the option you want, select A to download it. For some multi-disk games, you may see a menu asking which version of the game you want to download (disk 1 or disk 2). Simply select the version you want to download (disk 1, disk 1 version 1.1, disk 2, etc).

11) For large games, the download, decompress and extract process can take a long time (sometimes over of 4-5 hours per game). If the download process fails, you need to manually try again, as X Store currently lacks automatic resume support. For other issues, check the [troubleshooting guide](https://github.com/951261/X-Store#troubleshooting). 
