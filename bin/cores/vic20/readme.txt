This is the MiST port of the vic20 hardware simulation by MikeJ: http://fpgaarcade.com/?q=node/19
You can read more about it here: http://ws0.org/tag/vic20/
The source can be found here: https://github.com/wsoltys/mist-cores/tree/master/vic20

Features:
- pal vic20/vc20 with 16K and 3K ram expansion
- .prg and .a0 upload from sd card (.a0 needs to be renamed to .prg)
- scanline support
- joystick support
- the double scan can be turned off

Limitations:
The prg upload expects the start address in the first two bytes as usual. the rom upload
via osd expects cartridge roms with the auto start sequence.
A few glitches are visible. I don't know if its due to my changes or were already in the original
core.