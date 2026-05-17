# Compiling

Sadly, there is extremely little information around creating native Xbox 360 programs. If you are interested in creating or modifying Xbox 360 programs, I would recommend taking a look at [ClementDreptin's ModdingResources](https://github.com/ClementDreptin/ModdingResources/). While it is mainly focussed on developing dashlaunch plugings for modding games, much of the information applies to creating homebrew as well.

## Requirements

* A computer running Windows 7 or higher
* Microsoft Visual Studio 2010
* The official Microsoft Xbox 360 XDK
* An Xbox capable of running homebrew (BadUpdate, JTAG or RGH) preferably with the XBDM plugin

1. Install Visual Studio 2010 and the official XDK
2. Clone this Git Repository
3. From within Visual Studio 2010, open *free60 store.sln*
4. In the solution explorer on the left, you should see several projects. Right click each one, and click Build, ensuring that XboxTLS is the **last** one built
5. Once everything has built successfully, navigate to the *Release* folder using file explorer. In this folder, you should find *XboxTLS.xex*. Transfer this to your console and launch it.
6. To view the standard output of the Xbox, you will need to use a program on your PC called Xbox Watson. It is quite buggy, but it is what Microsoft provided developers with. To prevent some of the bugs, the program can be run in Windows 7 compatability mode.

## Issues

* HELP! It won't compile!
  * If you get an error about failing to find `uploadXEX.bat`, right click on the XboxTLS solution, go to Properties > configuration properties > Build Events > Post-Build Events, and remove the command line field.
  * Try to determine whether the error message is an error in the code, or an error in the build system. If you have not edited the code, something is likely incorrectly configured.
* The program crashed when running it on the Xbox
  * Use Xbox Watson to determine which part of the code likely caused the error
