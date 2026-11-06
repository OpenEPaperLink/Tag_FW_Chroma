// Chroma21 (JG, BWY, 212x104) settings for the spidump screen, see dumpscreen.h
#define DS_DRIVER         uc8154

// physical: 104 sources per line, 212 lines (gates)
#define DS_SOURCES        104
#define DS_GATES          212

// logical (OEPL) coordinates of physical pixel px of line py, as in
// board/chroma21/drawing.c: physical X = logical y, physical Y = 211 - logical x
#define DS_WIDTH          DS_GATES
#define DS_HEIGHT         DS_SOURCES
#define DS_LX(py,px)      (DS_GATES - 1 - (py))
#define DS_LY(py,px)      (px)

// the waveform block for this temperature, the screen doesn't measure it
#define DS_WF_TEMP        20
