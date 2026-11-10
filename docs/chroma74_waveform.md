# Chroma74 factory waveform scripts (format 03 03)

Decoded from the SPI flash of two Chroma74 tags with a UC8159 controller and a 640×384 panel (NVRAM record `0x14`):

| Tag | Panel | Name | Build |
|---|---|---|---|
| JL10420082B | BWR | `D20R20031DPP0BE_V3 UC59 74E41f` | `chroma74r` |
| JM10525366C | BWY | `D19Y10090WY06G_UC8159 74E42e` | `chroma74y` |

Both are the script format of the Chroma60 ([chroma60_waveform.md](chroma60_waveform.md)), with the same header, record types and LUT layout. They differ from it, and from each other, in the scripts, the number of temperature blocks, the LUTs, and an **XON LUT** (R29h) loaded in every block. Unlike the Chroma60's JD and JJ tags, the red and yellow panels have entirely different waveforms.

`Chroma_Tag_FW/tools/decode_waveform.py dump.bin [block] [--steps]` prints everything below. Both payloads pass their checksum and end where the header says: the next byte is erased.

Firmware addresses refer to the stock CC1110 image of JL10420082B, version 3.8.0.0 (from a disassembly of that image). Its script interpreter is at 0x205B with its jump table at 0x20F9, at the same entry address as the Chroma60's. The JM10525366C image hasn't been analysed beyond the pixel table (see [Pixel codes](#pixel-codes)).

Register meanings come from the UltraChip UC8159 datasheet (version c_B, revision 0.5).

**Confidence:** ✅ confirmed (by the data, the stock firmware or the datasheet), ⚠️ strong inference, ❓ unknown.

## Layout

| Address | Size | Contents | |
|---|---|---|---|
| 0x2000 | 4 | Magic `ce fa ef be` | ✅ |
| 0x2004 | 2 | Format `03 03` | ✅ |
| 0x2006 | | Name, NUL terminated. Both names are longer than the Chroma60's and run into 0x2021–0x2023, so those bytes aren't always zero. | ✅ |
| 0x2025 | 2 | Payload length | ✅ |
| 0x2027 | 1 | `0x26`: payload at 0x202A | ✅ |
| 0x2028 | 1 | Payload checksum | ✅ |
| 0x202A | 16 | Section offsets from 0x202A (u32 LE): init, temperature table, refresh, VCOM measure | ✅ |
| 0x203A | 1 | `0x32`: power-on delay × 100 ms | ✅ |
| 0x203B | 1 | JL `01`, JM `02`: probably the colour code, red and yellow. On the Chroma29 and Chroma42 its only use is the stock `0x1A` (Get displaySize) reply. See [Colour code](chroma29_waveform.md#colour-code-0x203e-and-0x203b). | ⚠️ |

| | JL10420082B | JM10525366C |
|---|---|---|
| Payload | 0x202A–0x42FA, 8913 bytes, checksum `0xF7` | 0x202A–0x42F5, 8908 bytes, checksum `0x50` |
| Init script | 0x2048, 80 bytes | 0x2048, 75 bytes |
| Refresh script | 0x2098 | 0x2093 |
| VCOM-measure script | 0x20C0 | 0x20BB |
| Temperature table | 0x20E7, 7 entries and the end marker | 0x20E2, 7 entries and the end marker |
| Blocks | 7 × 1236 bytes from 0x212F | 7 × 1236 bytes from 0x212A |
| First erased byte | 0x42FB | 0x42F6 |

## Scripts

### Init

```
09 arg=1                panel power on
06                      reset pulse
06                      reset pulse
01 R06h 0e cd 26        BTST
01 R00h ef 80           PSR
01 R03h 00              PFS
01 R82h 28 (JM: 2f)     VDCS
04 R23h 260 zeros       LUT, code 001
04 R24h 260 zeros       LUT, code 010
04 R26h 260 zeros       LUT, code 101
04 R27h 260 zeros       LUT, code 110
04 R28h 260 zeros       LUT, code 111
01 R60h 22              TCON
01 RE3h 36              PWS (JL only)
01 R41h 80              TSE
01 R50h 77              CDI
01 R65h 00              DAM
00
```

This is the Chroma60's init script with three differences:
- R29h isn't zeroed, because every block loads an XON LUT.
- R82h is a fixed value, the same one the refresh script sends (see [VCOM](#vcom)).
- JL sends **PWS** (RE3h) `36`: VCOM and source power saving, VCOM_W = 3 and SD_W = 6 line periods ✅.

The other values are the Chroma60's, described in its doc.

### Refresh

```
07                      VDCS, TRES, image
01 R82h 28 (JM: 2f)     VDCS
01 R04h                 PON
03                      wait for BUSY
01 R12h                 DRF
05                      sleep until BUSY
03                      wait for BUSY
01 R02h                 POF
03                      wait for BUSY
01 R07h a5              deep sleep
00
```

Compared with the Chroma60's, R82h is sent again after the image, and there's an extra BUSY wait after DRF.

### VCOM measure

The Chroma60's script exactly: type `07`, PWR `07 01 04 3c`, PON, AMV `11`, read R81h into NVRAM `0x23`, POF ✅.

### VCOM

| | JL10420082B | JM10525366C |
|---|---|---|
| NVRAM `0x23` (measured) | `2e`, −2.30 V | `2a`, −2.10 V |
| Script R82h | `28`, −2.00 V | `2f`, −2.35 V |

On the Chroma60, type `07` sends the NVRAM VCOM. Here the refresh script then sends its own R82h before PON, so the stock refresh runs with the **script's fixed value**, not the measured one ⚠️. That holds if type `07` sends R82h as the Chroma60's does; the Chroma74 analysis only says it sends R82h when a value is stored.

## Temperature table and blocks

The table has the Chroma60's format ✅: 9-byte entries of an int8 threshold, a u32 offset from 0x202A and 4 zero bytes, ending with a −100 marker. Each block has these records, then `00 00 00`:

```
01 R01h <4 bytes>    PWR
01 R30h <1 byte>     PLL
01 R20h <220 bytes>  VCOM LUT
01 R22h <260 bytes>  LUT, code 011: white
01 R21h <260 bytes>  LUT, code 000: black
01 R25h <260 bytes>  LUT, code 100: red/yellow
01 R29h <200 bytes>  XON LUT
```

**PWR** is `07 VG xx 3c` ✅: byte 1 as on the Chroma60; byte 2 is VG_LVL[1:0] (only two bits are defined); byte 3 is VSHC_LVL, the red/yellow voltage VSH_LV, 3.0 V + 0.2 V per step; byte 4 is VSL_LV = −15 V, which no LUT uses.

**PLL** rates are from the datasheet ✅: `3a` 100 Hz, `3c` 50 Hz, `22` (M = 4, N = 2) 59 Hz. **Frames** are from the LUTs ✅; **time** is frames ÷ rate ⚠️.

### JL10420082B (BWR)

| # | ≥ °C | Address | VGH/VGL | VSH_LV | PLL | Rate | Frames | Time |
|---|---|---|---|---:|---|---|---:|---:|
| 0 | 25 | 0x212F | ±17 V (`07`) | 5.6 V | `3a` | 100 Hz | 1229 | 12 s |
| 1 | 20 | 0x2603 | ±17 V | 5.8 V | `3c` | 50 Hz | 805 | 16 s |
| 2 | 15 | 0x2AD7 | ±17 V | 6.2 V | `3c` | 50 Hz | 903 | 18 s |
| 3 | 10 | 0x2FAB | ±17 V | 6.0 V | `3c` | 50 Hz | 1149 | 23 s |
| 4 | 6 | 0x347F | ±17 V | 5.6 V | `3c` | 50 Hz | 1378 | 28 s |
| 5 | 3 | 0x3953 | ±17 V | 5.6 V | `3c` | 50 Hz | 1518 | 30 s |
| 6 | −5 | 0x3E27 | ±17 V | 5.6 V | `3c` | 50 Hz | 1518 | 30 s |

No two blocks have the same LUTs.

### JM10525366C (BWY)

| # | ≥ °C | Address | VGH/VGL | VSH_LV | PLL | Rate | Frames | Time |
|---|---|---|---|---:|---|---|---:|---:|
| 0 | 25 | 0x212A | ±19 V (`01`) | 3.4 V | `22` | 59 Hz | 3062 | 52 s |
| 1 | 20 | 0x25FE | ±19 V | 3.4 V | `22` | 59 Hz | 3062 | 52 s |
| 2 | 16 | 0x2AD2 | ±19 V | 3.6 V | `22` | 59 Hz | 3922 | 66 s |
| 3 | 12 | 0x2FA6 | ±19 V | 4.0 V | `3c` | 50 Hz | 3904 | 78 s |
| 4 | 8 | 0x347A | ±19 V | 4.4 V | `3c` | 50 Hz | 4562 | 91 s |
| 5 | 3 | 0x394E | ±19 V | 4.4 V | `3c` | 50 Hz | 5003 | 100 s |
| 6 | −5 | 0x3E22 | ±19 V | 4.6 V | `3c` | 50 Hz | 5594 | 112 s |

Blocks 0 and 1 are identical. The yellow panel refreshes 3–4 times slower than the red one.

## LUT format

VCOM and colour LUTs are the Chroma60's: 20 rows of 11 bytes (VCOM) or 13 bytes (colour), each a repeat count, 2 or 4 bytes of 8 phase levels, and 8 frame counts ✅. Up to 13 rows are used.

The **XON LUT** (R29h) is 20 rows of 10 bytes: a repeat count, one XON byte (one bit per phase, MSB first) and 8 frame counts ✅. Per the datasheet, an XON bit of **0 turns all gates on** and 1 is normal gate scanning ✅. Its repeats and frame counts copy the VCOM LUT's.

- **JL** sets every used phase to normal scanning (`f0`, with 4 phases used). It loads an XON LUT but never turns XON on.
- **JM** clears the first two phase bits of row 1 (`3f`). Every JM block starts with two phases (59/59 or 50/50 frames) of all gates on, while every LUT holds 0 V and VCOM holds VCM_DC. After that, scanning is normal. This step may clear the panel's charge before the image waveform starts ⚠️.

Rows with no frames are ignored here. In the stock data, their XON bytes are `ff`, `f0` or `00`.

## Pixel codes

| Code | Register | Colour | |
|---|---|---|---|
| `000` | R21h | Black | ✅ |
| `011` | R22h | White | ✅ |
| `100` | R25h | Red/yellow | ✅ |

The stock firmware turns two 1-bit planes into 4-bit pixels through a 16-byte table: at 0x07E3 in the JL image and 0x07D9 in the JM image, both `33 30 03 00 34 33 04 03 43 40 33 30 44 43 34 33` ✅. That's the Chroma60's coding: plane 1 means "not black" and plane 2 "not red/yellow".

## The waveform at 20 °C

`0` = 0 V or VCM_DC, `+` = VSH, `−` = VSL, `L` = VSH_LV. XON: `A` = all gates on, `.` = normal.

### JL10420082B, block 1 (20–24 °C), 805 frames at 50 Hz

| Step | Repeat | Frames | VCOM | White | Black | Red | XON |
|---|---:|---|---|---|---|---|---|
| 1 | 1 | 86/12/0/1 | `0 0 0 0` | `+ + 0 0` | `− 0 0 0` | `− − + −` | `. . . .` |
| 2 | 15 | 2/0/2 | `0 0 0` | `− 0 +` | `− 0 +` | `− 0 +` | `. . .` |
| 3 | 4 | 20/1/20/1 | `0 0 0 0` | `+ 0 − 0` | `+ 0 − 0` | `+ 0 − 0` | `. . . .` |
| 4 | 4 | 8/10/5/8 | `0 0 0 0` | `0 − − −` | `+ 0 0 +` | `0 0 − +` | `. . . .` |
| 5 | 3 | 4/4/40/2 | `0 0 0 0` | `0 0 0 −` | `0 0 0 +` | `0 − L L` | `. . . .` |
| 6 | 4 | 4/3/40/4 | `0 0 0 0` | `0 0 0 0` | `0 0 0 +` | `0 − L 0` | `. . . .` |

The likely purpose of each step ⚠️:
- **Step 1** drives the old image out, differently for each colour.
- **Steps 2–3** shake the particles. All three LUTs are identical.
- **Step 4** separates black (`+`) from white (`−`).
- **Steps 5–6** develop red with VSH_LV while topping up black and white.

### JM10525366C, blocks 0 and 1 (≥20 °C), 3062 frames at 59 Hz

| Step | Repeat | Frames | VCOM | White | Black | Yellow | XON |
|---|---:|---|---|---|---|---|---|
| 1 | 1 | 59/59 | `0 0` | `0 0` | `0 0` | `0 0` | `A A` |
| 2 | 5 | 5/10/6/9 | `0 0 0 0` | `+ + 0 0` | `0 0 − −` | `0 + 0 −` | `. . . .` |
| 3 | 1 | 50/50 | `0 0` | `+ −` | `+ −` | `+ −` | `. .` |
| 4 | 2 | 50/50/50/50 | `0 0 0 0` | `+ + 0 0` | `+ + − −` | `+ + − −` | `. . . .` |
| 5 | 200 | 3/3 | `0 0` | `+ −` | `+ −` | `+ −` | `. .` |
| 6 | 1 | 50/50 | `0 0` | `− −` | `− −` | `− −` | `. .` |
| 7 | 40 | 4/1/3 | `0 0 0` | `− 0 0` | `0 + +` | `0 + 0` | `. . .` |
| 8 | 6 | 40/10/5 | `0 0 0` | `0 0 0` | `0 0 0` | `L 0 −` | `. . .` |
| 9 | 3 | 40/20/4 | `0 0 0` | `0 0 0` | `0 0 0` | `L 0 −` | `. . .` |
| 10 | 2 | 40/20/1 | `0 0 0` | `0 0 0` | `0 0 0` | `L 0 −` | `. . .` |
| 11 | 5 | 3/3 | `0 0` | `− 0` | `0 +` | `0 0` | `. .` |

The likely purpose of each step ⚠️:
- **Step 1** holds the whole panel at 0 V with all gates on.
- **Steps 2–6** drive the old image out and then shake for a long time: step 5 alone is 1200 frames, 20 s.
- **Step 7** separates black from white; yellow pixels get a short `+`.
- **Steps 8–10** develop yellow (`L`, then a short `−`), while black and white rest at 0 V.
- **Step 11** tops up black and white.

This is the block the OEPL `chroma74y` build uses.

## How it changes with cold

The VCOM LUT stays at VCM_DC in every JL block. In JM, only block 4 (8 °C) moves it, to `+` in the last phase of step 10, along with black and white. Neither board has the Chroma60's antiphase VCOM swing.

**JL10420082B**:

| Blocks | ≥ °C | Shape |
|---|---|---|
| 0 | 25 | 6 steps at 100 Hz: one 87/56/5 pre-drive, 30 × 4/4 shaking, 5 × 26/1/26/1, separation, two red steps |
| 1–2 | 20, 15 | As above, at 50 Hz |
| 3 | 10 | 7 steps: a four-phase pre-drive (51/60/83/112), 14 × 2/2 and 4 × 22/22 shaking |
| 4–6 | 6, 3, −5 | 7 steps: the shaking starts with `+` instead of `−` (10 × 2/0/2), then 5–8 × 20–24/1/20–24/1, 9 × separation, red steps of up to 42 frames per phase |

**JM10525366C**:

| Blocks | ≥ °C | Shape |
|---|---|---|
| 0–1 | 25, 20 | As above, 11 steps |
| 2–3 | 16, 12 | 13 steps: the 200 × 3/3 shaking becomes 100 × 4/4/4/4 plus two single 4/4 steps, with longer separation and yellow steps |
| 4 | 8 | As 2–3 with 5-frame shaking phases |
| 5–6 | 3, −5 | 12 steps: 10 repeats of the pre-drive, 6–8-frame shaking phases, and no final top-up |

## Factory NVRAM (same record format as the other Chroma boards)

| Type | JL10420082B (sector 1) | JM10525366C (sector 0) |
|---|---|---|
| `0x01` | `JL 10 42 00 82` | none |
| `0x2a` | none | `JM 10 52 53 66 'C'`: the serial with its suffix letter |
| `0x05` | `b0` + `09210298-01-PP0BEN` | `d5` + `59T1128245102-Y06G` |
| `0x14` | `80 02 80 01 05 00`: 640 × 384, controller type 5 | same |
| `0x09` / `0x12` | `02d8` / `0a5a` | `02e6` / `0a5a` |
| `0x23` | `2e`: VCOM −2.30 V | `2a`: VCOM −2.10 V |

OEPL already reads the serial from `0x2a` before `0x01`.

## Compared with the OEPL driver

`Chroma_Tag_FW/board/chroma74/screen.c` compiles in one block per build. The stored LUT prefixes match the stock blocks byte for byte; the zero padding differs only in XON rows that have no frames.

| | Stock | OEPL `chroma74y` | OEPL `chroma74r` |
|---|---|---|---|
| Waveform | The block for the current temperature | JM blocks 0/1 (≥20 °C) | JL block 0 (≥25 °C) |
| PLL | Per block | `22`, as block 0 | `3a`, as block 0 |
| PWR | Per block | `07 01 02 3c`, as block 0 | **`07 01 04 3c`**: VGH/VGL ±19 V and VSH_LV 3.8 V, against block 0's ±17 V and 5.6 V |
| VCOM | The script's fixed R82h (⚠️, see [VCOM](#vcom)) | NVRAM `0x23` | NVRAM `0x23` |
| PWS | JL sends `36` | not sent | not sent |

On a red tag between 20 and 25 °C, the stock firmware would use block 1 at 50 Hz, a different waveform, where OEPL uses block 0 at 100 Hz.

## What the stock firmware checks

The JL and JM images each accept only format `03 03`. The JL image:
- checks the magic, then reads the 0x44-byte header (0x275F) and checks the version (0x2805);
- walks the temperature table with no count limit, until a threshold below −99 (0x26B9);
- uses the block offsets without checking them;
- never checks the payload checksum before use.

Its radio download handler (`0x32`, 0x4027) checks the version too. Details for all boards: [stock_firmware.md](stock_firmware.md#what-the-stock-firmware-checks).

## Open questions

1. Confirmation of the colour code at 0x203B (`01`, `02`), and whether the Chroma74 firmware also only reports it.
2. Whether type `07` in the Chroma74 firmware sends the NVRAM VCOM, and so which VCOM the stock refresh really uses.
3. The purpose of JM's all-gates-on first step.
4. Whether the OEPL `chroma74r` power setting (±19 V, 3.8 V) is deliberate. Block 0 asks for ±17 V and 5.6 V.
5. Real refresh times. The figures above are calculated.
6. The JM stock image: only its pixel table has been checked.
