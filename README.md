# OpenEPaperLink Firmware for Chroma Tags

This firmware is for the Chroma series of electronic shelf labels 
manufactured by Display Data.  

The firmware allows the tags to be used with the [OpenEPaperLink project](https://github.com/OpenEPaperLink/OpenEPaperLink).


## Firmware binaries

There is no need to build the tag firmware from source unless you want to 
make changes.

Currently the following tags are supported and support for the Chroma21 is planned:

| Tag | SN Format | BUILD | Binary filename | 
| :-: |:-:| :-:| :-:|
| Chroma29 | "JA0xxxxxxxB"<br>or<br>"JC0xxxxxxxB" | chroma29 | chroma29_full_\<version\>.bin |
| Chroma29 | "JA1xxxxxxxC" | chroma42_8176 | chroma42_8176_full_\<version\>.bin |
| Chroma42 | "JC0xxxxxxxB"<br>or<br>"JH0xxxxxxxB" | chroma42 | chroma42_full_\<version\>.bin |
| Chroma74 | "JM1xxxxxxxB" | chroma74y | chroma74y_full_\<version\>.bin |
| Chroma74 | "JL1xxxxxxxB" | chroma74r | chroma74r_full_\<version\>.bin |

Where \<version\> is the 4 digit firmware version in hex for example "chroma29_8151_full_0012.bin"

> [!CAUTION]
> Always flash using the **_full** version, the other files are for OTA updates.

Binaries are available here:  https://github.com/OpenEPaperLink/OpenEPaperLink/tree/master/binaries/Tag

Refer the table above for the correct filename. 
 
## Building from Source
 
**NB:** While it may be possible to use Windows for development I haven't 
tried it and don't recommend it. 

Executive summary;

1. Make sure that SDCC version 4.2.0 is in the path.
2. Change into the .../Tag_FW_Chroma/Chroma_Tag_FW/OEPL subdirectory.
3. Run make BUILD=\<tag type\>

Where \<tag type\> is from the above table.

The binary file will be created in .../Tag_FW_Chroma/Chroma_Tag_FW/builds/\<tag type\>/ subdirectory.

For example:

```
skip@Dell-5510:~/esl/Tag_FW_Chroma$ cd Chroma_Tag_FW/OEPL/
skip@Dell-5510:~/esl/Tag_FW_Chroma/Chroma_Tag_FW/OEPL$ make BUILD=chroma74y
sdcc -c main.c -DBUILD=chroma74y -DBWY -DATC1441_LUT -I/home/skip/esl/Tag_FW_Chroma/Chroma_Tag_FW/board/chroma74 -I/home/skip/esl/Tag_FW_Chroma/Chroma_Tag_FW/soc/cc111x -I/home/skip/esl/Tag_FW_Chroma/Chroma_Tag_FW/cpu/8051 -I/home/skip/esl/Tag_FW_Chroma/Chroma_Tag_FW/common -I. -I/home/skip/esl/Tag_FW_Chroma/Chroma_Tag_FW/../shared --code-size 0x8000 --xram-loc 0xf000 --xram-size 0xda2 --model-medium -I/home/skip/esl/Tag_FW_Chroma/Chroma_Tag_FW/cpu/8051 -mmcs51 --std-c2x --opt-code-size --peep-file /home/skip/esl/Tag_FW_Chroma/Chroma_Tag_FW/cpu/8051/peep.def --fomit-frame-pointer -DPROXY_BUILD -I. -MD -o /home/skip/esl/Tag_FW_Chroma/Chroma_Tag_FW/builds/chroma74y/main.rel
sdcc -c /home/skip/esl/Tag_FW_Chroma/Chroma_Tag_FW/common/eeprom.c -DBUILD=chroma74y -DBWY -DATC1441_LUT -I/home/skip/esl/Tag_FW_Chroma/Chroma_Tag_FW/board/chroma74 -I/home/skip/esl/Tag_FW_Chroma/Chroma_Tag_FW/soc/cc111x -I/home/skip/esl/Tag_FW_Chroma/Chroma_Tag_FW/cpu/8051 -I/home/skip/esl/Tag_FW_Chroma/Chroma_Tag_FW/common -I. -I/home/skip/esl/Tag_FW_Chroma/Chroma_Tag_FW/../shared --code-size 0x8000 --xram-loc 0xf000 --xram-size 0xda2 --model-medium -I/home/skip/esl/Tag_FW_Chroma/Chroma_Tag_FW/cpu/8051 -mmcs51 --std-c2x --opt-code-size --peep-file /home/skip/esl/Tag_FW_Chroma/Chroma_Tag_FW/cpu/8051/peep.def --fomit-frame-pointer -DPROXY_BUILD -I. -MD -o /home/skip/esl/Tag_FW_Chroma/Chroma_Tag_FW/builds/chroma74y/eeprom.rel

... many lines deleted ...

CSEG                                000001D6    000065DA =       26074. bytes (REL,CON,CODE)
CONST                               000067B0    00001811 =        6161. bytes (REL,CON,CODE)
XINIT                               00007FC1    00000024 =          36. bytes (REL,CON,CODE)
.bin size: 32741
skip@Dell-5510:~/esl/Tag_FW_Chroma/Chroma_Tag_FW/OEPL$ ls -l ../builds/chroma74y/*.bin
-rw-rw-r-- 1 skip skip 32741 Jan 10 13:40 ../builds/chroma74y/chroma74y_full_0012.bin
skip@Dell-5510:~/esl/Tag_FW_Chroma/Chroma_Tag_FW/OEPL$
```

## Installing SDCC

The code in this repo was developed and tested using SDCC 4.2.0 on Linux.  
Different SDCC versions can behave VERY differently, i.e. are **BROKEN**
with respect to this project.  

SDCC releases can be found here: https://sourceforge.net/projects/sdcc/files/

If you have success with a newer version of SDCC we would very much like to hear about it.  
**NOTE** Success means the compiled code not only builds but it **runs correctly**!

## Building SDCC from source

Requirements

- The usual development tools for C programs.  These are frequently provided by the "build-essential" package.
- The subversion source control tool.  Commonly provided by the "subversion" package.
- The makeinfo tool.  Commonly provided by the "texinfo" package.

The sdcc/setup.sh script can be **sourced** to setup build a local copy of SDCC.
It takes a **while** to build SDCC the first time, but it will only need to 
be built once.

```
skip@Dell-5510:~/esl/Tag_FW_Chroma$ . sdcc/setup_sdcc.sh
Building sdcc-4.2.0, please be patient this is going to take a while!
Checking out sdcc-4.2.0 source ...
Configuring ...
Compiling ...
Installing ...Added /home/skip/esl/Tag_FW_Chroma/sdcc/sdcc-4.2.0/bin to PATH
skip@Dell-5510:~/esl/Tag_FW_Chroma$ sdcc -v
SDCC : mcs51 4.2.0 #13081 (Linux)
published under GNU General Public License (GPL)
skip@Dell-5510:~/esl/Tag_FW_Chroma$
``` 
 
## Credits

Large parts of this repo are based on code written by, and wouldn't be possible without the hard work of:
- Dmitry dmitry.gr 
- Arron atc1441
- 'Corn' jjwbruijn
- Nic nlimper

Hats off to these legends!
 
## License

Dimitry didn't include a LICENSE file or copyright headers in the source code
but the web page containing the ZIP file I downloaded says:

"The license is simple: This code/data/waveforms are free for use in **hobby and 
other non-commercial products**." 

For commercial use, <a href="mailto:licensing@dmitry.gr">contact him</a>.

## Warranty

There is no warranty whatsoever.  Nothing.  Not implied or otherwise suggested.  
This code isn't fit for anything.  Please don't use this code to do nasty things.  

