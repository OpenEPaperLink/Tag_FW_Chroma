# Chroma42 factory waveforms (formats 03 02 and 03 03)

Decoded from the SPI flash of two Chroma42 tags (400×300, per NVRAM record `0x14`). They use different controllers and different formats.

The [OEPL wiki's Chroma42 page](https://github.com/OpenEPaperLink/OpenEPaperLink/wiki/4.2%E2%80%B3-Chroma-42) lists four variants:

| Serial | Colour | Board | Controller | OEPL build | Dumped here |
|---|---|---|---|---|---|
| JC0…B | BWR | edk288 issue 2 (2014) | Two UC8154s in cascade mode | `chroma42` | JC00002960B, format `03 02` |
| JH0…B | BWY | edk288 issue 2 | Two UC8154s in cascade mode | `chroma42` (not verified) | |
| JC1…B | BWR | edk288 issue 4 (2018) | UC8176 | `chroma42_8176` | stock MCU image only (JC10063658B) |
| JH1…B | BWY | edk288 issue 4 | UC8176 | `chroma42_8176` | JH10005098, format `03 03` |

- **JC00002960B** has the table format of the Chroma21 and Chroma29, with the UC8154's 15-byte LUTs. It's described in [JC0: table format](#jc0-table-format), which only covers what differs from [chroma29_waveform.md](chroma29_waveform.md). The waveform's name is `v3EPD P15C28.4 UC 42E4`.
- **JH10005098** has the script format of the Chroma60 ([chroma60_waveform.md](chroma60_waveform.md)) with UC8176 registers and LUTs. Its waveform is named `P16Y00.63 V3 UC8176 42E4`, and its panel serial ends in `-Y005`, which fits the wiki's BWY.

`Chroma_Tag_FW/tools/decode_waveform.py dump.bin [block] [--steps]` prints everything below for either dump. Both payloads pass their checksum and end where the header says: the next byte is erased.

Firmware addresses refer to the stock CC1110 image of JH10005098 (from a disassembly of that image). The stock image from JC10063658B (a JC1, BWR UC8176 tag) is byte-identical to it: both UC8176 variants run the same stock firmware, and only the waveform in SPI flash differs. The stock image of JC00002947B (a different JC0 tag from the dump) hasn't been analysed.

**Confidence:** ✅ confirmed (by the data, the stock firmware or a datasheet), ⚠️ strong inference, ❓ unknown. UC8176 register meanings come from the UltraChip UC8176 datasheet (version c_A, revision 0.1, preliminary). That revision has no pages for the LUT registers (R20h–R24h), so the LUT layout is still inferred from the data.

## JC0: table format

JC0 tags have two UC8154s in cascade mode, per the wiki. That's probably why the OEPL `chroma42` driver sets TRES to 200 × 300: each controller drives half of the 400-pixel width ⚠️. The waveform holds one set of LUTs, with nothing per controller.

The layout, header, temperature-table bytes, index map and LUT format are those of the Chroma29's `03 02` waveform, with the same sizes:

| | JC00002960B |
|---|---|
| Payload | 0x2035–0x23BF, 907 bytes, checksum `0xC3` |
| Temperature table | 7 × 10 bytes at 0x2047 |
| Index map | 7 × 12 bytes at 0x208D |
| LUT pool | 49 × 15 bytes at 0x20E1 |
| First erased byte | 0x23C0 |

### Parameters (0x2035)

| Address | Meaning | Value | |
|---|---|---|---|
| 0x2035 | VCM_DC (R82h) | `08` (−0.8 V) | ✅ |
| 0x2036 | TCON | `22` | ✅ |
| 0x2037 | CDI | `17` | ✅ |
| 0x2038 | BTST | `07 07 0f` | ✅ |
| 0x203B | PWR bytes 1–2 | `03 00` | ✅ |
| 0x203D | Delay before the EPD is powered on, × 100 ms | `32` (5 s) | ✅ |
| 0x203E | Probably the colour code, here `00`: not set, although the panel is BWR. Only reported to the host. See [Colour code](chroma29_waveform.md#colour-code-0x203e-and-0x203b). | `00` | ⚠️ |
| 0x203F | Delay before power off | 2000 ms | ✅ |

### Temperature table

Frame rates are from the UC8154 PLL table ✅, and times are calculated, as in the Chroma29 doc.

| # | ≥ °C | Row | PLL 1 | PLL 2 | VDPS_LV | Byte 6 | Frames B/W + red | Time |
|---|---|---|---|---|---:|---:|---|---:|
| 0 | 35 | 0 | `39` 137 Hz | `39` 137 Hz | 4.2 V | 11 ms | 452 + 446 | 7 s |
| 1 | 30 | 1 | `39` 137 Hz | `39` 137 Hz | 4.2 V | 11 ms | 509 + 606 | 8 s |
| 2 | 20 | 2 | `39` 137 Hz | `29` 98 Hz | 4.2 V | 16 ms | 685 + 316 | 8 s |
| 3 | 15 | 3 | `39` 137 Hz | `29` 98 Hz | 4.6 V | 16 ms | 1103 + 610 | 14 s |
| 4 | 10 | 4 | `29` 98 Hz | `29` 98 Hz | 5.0 V | 16 ms | 633 + 591 | 13 s |
| 5 | 3 | 5 | `29` 98 Hz | `29` 98 Hz | 6.4 V | 16 ms | 767 + 578 | 14 s |
| 6 | −5 | 6 | `2a` 50 Hz | `2a` 50 Hz | 7.8 V | 30 ms | 610 + 542 | 23 s |

One entry per threshold, so every refresh is a single pass. The VCOM LUTs stay at VCM_DC in every row.

**The index map has the same problem as the Chroma29's `03 02` map** ⚠️: rows 1, 3, 4, 5 and 6 point G2 at the white LUT, and each leaves one G2-shaped pool entry unreferenced (`pool[10]`, `[24]`, `[31]`, `[38]`, `[45]`).

### The waveform at 20 °C (row 2)

`0` = 0 V or VCM_DC, `+` = VSH, `−` = VSL; in the red stage, the red voltages.

B/W stage, 685 frames at 137 Hz:

| Step | Repeat | Frames | VCOM1 | Black (R21h) | White (R22h) | Red (R24h) |
|---|---:|---|---|---|---|---|
| 1 | 1 | 3/2 | `0 0` | `0 0` | `0 0` | `− −` |
| 2 | 15 | 2/4 | `0 0` | `0 −` | `+ 0` | `− −` |
| 3 | 25 | 10/10 | `0 0` | `− +` | `− +` | `− +` |
| 4 | 15 | 2/4 | `0 0` | `0 +` | `− 0` | `0 0` |

Red stage, 316 frames at 98 Hz:

| Step | Repeat | Frames | VCOM2 | R0 | R1 |
|---|---:|---|---|---|---|
| 1 | 1 | 10/10 | `0 0` | `+ +` | `0 0` |
| 2 | 8 | 2/20 | `0 0` | `− +` | `0 0` |
| 3 | 3 | 20/20 | `0 0` | `+ +` | `0 0` |

The same pattern as the Chroma29's: drive black and white the opposite way, shake, then set them; then a red-only stage.

### JC0 factory NVRAM

| Type | Value |
|---|---|
| `0x01` | `JC 00 00 29 60` |
| `0x05` | `b4` + `00E4U54O1117058645` |
| `0x14` | `90 01 2c 01 00 06`: 400 × 300 |
| `0x09` / `0x12` | `02ed` / `0a5a` |

No VCOM record (`0x23`); VCOM comes from 0x2035.

## JH1: script format (UC8176)

### Layout

The header is the Chroma60's, with different offsets:

| Address | Size | Contents | |
|---|---|---|---|
| 0x2000 | 4 | Magic `ce fa ef be` | ✅ |
| 0x2004 | 2 | Format `03 03`. The stock firmware (0x262F) accepts only 3.3 | ✅ |
| 0x2006 | 27 | Name | ✅ |
| 0x2025 | 2 | Payload length, `0x0B43` | ✅ |
| 0x2027 | 1 | `0x26`: payload at 0x202A | ✅ |
| 0x2028 | 1 | Checksum, `0x7D` | ✅ |
| 0x202A | 16 | Section offsets from 0x202A (u32 LE): init `0x1E` → 0x2048, temperature table `0x76` → 0x20A0, refresh `0x53` → 0x207D, VCOM measure `0x73` → 0x209D | ✅ |
| 0x203A | 1 | `0x32`: power-on delay × 100 ms | ✅ |
| 0x203B | 1 | `02`: probably the colour code, yellow. The stock firmware's only use is the last byte of its `0x1A` (Get displaySize) reply, through the accessor at 0x1EA7. See [Colour code](chroma29_waveform.md#colour-code-0x203e-and-0x203b). | ⚠️ |
| 0x2048 | 53 | Init script | ✅ |
| 0x207D | 32 | Refresh script | ✅ |
| 0x209D | 3 | VCOM-measure script: just an end record | ✅ |
| 0x20A0 | 14 × 9 | Temperature table | ✅ |
| 0x211E | 13 × 203 | Temperature blocks | ✅ |
| 0x2B6D | | End; erased flash follows | ✅ |

The stock firmware loads the 0x44 bytes from 0x2004 at boot, after checking the magic (0x2589).

### Script records

The interpreter (0x1F04) has the same record types as the Chroma60's: `00` end, `01` command and data, `02` delay, `03` wait for BUSY, `04` command and `arg` zeros, `05` sleep until BUSY, `06` reset pulse, `07` image, `08` read R81h into NVRAM `0x23`, `09` panel power ✅.

Type `07` differs from the Chroma60's: it sends TRES and the image but **no VDCS**, so VCOM is whatever the init script sets ✅. TRES sends HRES as two bytes because NVRAM `0x14` gives controller type 7 ✅.

After the image, type `07` reads R71h (0x22C3) and checks bit 3, which the datasheet names `data_flag`: "driver has already received all the one frame data" ✅. If it's clear, the image is sent again, up to 3 times. The datasheet says a host must send R11h (DSP) to end the transfer; the stock firmware never does, as on the Chroma60.

A refresh runs init, the temperature block and the refresh script (0x1EB3) ✅.

### Init (0x2048)

```
09 arg=1           panel power on
06                 reset pulse
06                 reset pulse
01 R03h 00         PFS
01 R06h 0e cd 26   BTST (same values as the Chroma60's)
01 R00h 2f         PSR
01 R41h 00         TSE
01 R50h 57         CDI
01 R60h 22         TCON
01 R82h 12         VDCS
04 R21h 42 zeros   LUT, unused in this mode
00
```

| Register | Value | Meaning | |
|---|---|---|---|
| R03h PFS | `00` | VDH and VDL switch off 1 frame after POF (the default) | ✅ |
| R06h BTST | `0e` | Phase A: 10 ms, strength 2 of 8, minimum off time 3.34 µs | ✅ |
| | `cd` | Phase B: 40 ms, strength 2, minimum off time 1.54 µs | ✅ |
| | `26` | Phase C: strength 5, minimum off time 3.34 µs | ✅ |
| R00h PSR | `2f` | RES = 00 (400×300); REG_EN = 1, LUTs from registers; BWR = 0, black/white/red pixels; UD = 1, scan up; SHL = 1, shift right; SHD_N = 1, booster on; RST_N = 1, no soft reset. The OEPL driver uses the same value. | ✅ |
| R41h TSE | `00` | Internal temperature sensor, offset +0. The stock firmware picks the block itself, using the CC1110's sensor. | ✅ |
| R50h CDI | `57` | VBD = 01: the border uses the white LUT. DDX = 01: a B/W bit of 1 selects the white LUT and 0 the black LUT, and a red bit of 1 selects the red LUT. Interval 10 Hsync (the default). | ✅ |
| R60h TCON | `22` | Source-to-gate and gate-to-source non-overlap both 12 periods of 660 ns (the default) | ✅ |
| R82h VDCS | `12` | **−1.00 V** (−0.10 V, then −0.05 V per step). The only VCOM setting: there's no NVRAM VCOM record and type `07` doesn't send one | ✅ |
| R21h | 42 zeros | The datasheet names it LUTWW in B/W mode and gives it no use in B/W/red mode | ✅ |

The BTST bytes are the Chroma60's, where the UC8159 datasheet calls strength `001` reserved. On the UC8176 it's a defined value.

### Refresh (0x207D)

```
07            TRES, image
01 R04h       PON
03            wait for BUSY
01 R12h       DRF
05            sleep until BUSY
01 R02h       POF
03            wait for BUSY
01 R07h a5    deep sleep
00
```

### Temperature table (0x20A0, 9-byte entries)

As on the Chroma60: an int8 threshold, a u32 offset from 0x202A and 4 zero bytes; the first entry at or below the temperature wins (0x24E3); below −99 ends the table ✅. The thresholds are 30, 20, 16, 12, 8, 7, 6, 5, 4, 3, 2, 1 and −5, the Chroma60's with 30 in place of 25. The stock firmware refuses to refresh below −5 °C, with a low battery (below 2200 mV) or a sensor reading below −30 °C (0x1CC7) ✅.

### Temperature blocks (0x211E, 13 × 203 bytes)

Every block has these records, then an end record:

```
01 R01h <5 bytes>    PWR
01 R30h <1 byte>     PLL
01 R20h <44 bytes>   VCOM LUT
01 R23h <42 bytes>   LUT: white
01 R24h <42 bytes>   LUT: black
01 R22h <42 bytes>   LUT: yellow
```

| # | ≥ °C | Address | PWR byte 5 | VDHR | PLL | Rate | Frames | Time |
|---|---|---|---|---:|---|---|---:|---:|
| 0 | 30 | 0x211E | `0a` | 4.4 V | `3a` | 100 Hz | 3108 | 31 s |
| 1 | 20 | 0x21E9 | `0a` | 4.4 V | `3a` | 100 Hz | 3108 | 31 s |
| 2 | 16 | 0x22B4 | `0c` | 4.8 V | `3a` | 100 Hz | 3347 | 33 s |
| 3 | 12 | 0x237F | `0d` | 5.0 V | `19` | 86 Hz | 3218 | 37 s |
| 4 | 8 | 0x244A | `0d` | 5.0 V | `19` | 86 Hz | 4092 | 48 s |
| 5 | 7 | 0x2515 | `0e` | 5.2 V | `3b` | 67 Hz | 3722 | 56 s |
| 6 | 6 | 0x25E0 | `0f` | 5.4 V | `3b` | 67 Hz | 3735 | 56 s |
| 7 | 5 | 0x26AB | `11` | 5.8 V | `3b` | 67 Hz | 3758 | 56 s |
| 8 | 4 | 0x2776 | `12` | 6.0 V | `3b` | 67 Hz | 3745 | 56 s |
| 9 | 3 | 0x2841 | `13` | 6.2 V | `3b` | 67 Hz | 3732 | 56 s |
| 10 | 2 | 0x290C | `15` | 6.6 V | `3c` | 50 Hz | 3412 | 68 s |
| 11 | 1 | 0x29D7 | `16` | 6.8 V | `3c` | 50 Hz | 3427 | 69 s |
| 12 | −5 | 0x2AA2 | `17` | 7.0 V | `3c` | 50 Hz | 3443 | 69 s |

**PWR** is `03 00 2b 2b xx` ✅:
- Byte 1 `03`: internal DC/DC for source and gate.
- Byte 2 `00`: VGH = +16 V and VGL = −16 V; VCOM swings between VDH + VCOM_DC and VDL + VCOM_DC.
- Bytes 3–4 `2b`: VDH = +11.0 V and VDL = −11.0 V (2.4 V + 0.2 V per step; `2b` is the top of the range).
- Byte 5 is **VDHR**, the red/yellow voltage, on the same scale, rising with cold.

**PLL** uses the same values as the Chroma60's (`3a`, `19`, `3b`, `3c`). The UC8176's rate table is laid out differently from the UC8159's, but gives the same rates for these four values ✅. **Frames** are from the LUTs ✅ and **times** are frames ÷ rate ⚠️.

Blocks 0 and 1 are identical.

### LUT format ⚠️

Each LUT is 7 groups of 6 bytes: a level byte (4 phases × 2 bits, MSB first), 4 frame counts and a repeat count. The VCOM LUT has 2 more bytes, zero in every block. The data fits this layout: in every block, all four LUTs have the same frame counts and repeats.

| Level | VCOM (R20h) | Colour LUTs |
|---|---|---|
| `00` | VCOM_DC | 0 V |
| `01` | VDH + VCOM_DC | VDH |
| `10` | VDL + VCOM_DC | VDL |
| `11` | floating | VDHR |

Only R22h uses level `11`, which is why it's the yellow LUT. R23h ends on VDL and R24h on VDH, the same polarity as the Chroma21 and Chroma29, where the LUT ending on `+` is black. In KWR mode these are the white (R23h) and black (R24h) LUTs ⚠️. The OEPL driver's names for them (`W2B`, `B2B`, `B2W`) are the KW-mode names.

The datasheet doesn't settle the register numbers. Its only list of the LUTs, in the BUSY_N table, reads LUTC, LUTWW/–, LUTWB/LUTW, LUTBW/LUTR, LUTBB/LUTB (B/W-mode name / B/W/red-mode name). Read in register order, that would put red in R23h. The UC8151C datasheet has the same list, yet the OEPL `chroma29_8151` driver loads its red LUT into R22h and white into R23h. So the list isn't in register order, and the data's evidence, VDHR only in R22h, stands.

The UC8151C, UC8154 and UC8159C datasheets all say a LUT ends at its first unused state: the UC8151C at one whose repeat count is 0 or whose frame counts are all 0. The UC8176 revision has no LUT pages. No LUT here has an unused state before a used one, so the frame counts are the same either way.

### Pixel data

From the stock image path (0x2342) ✅: DTM1 (R10h) gets the first plane as stored. DTM2 (R13h) gets the complement of the second plane, or zeros when the image has no colour. Fill patterns: `00`/`00`, `FF`/`00` and `FF`/`FF` (DTM1/DTM2).

### The waveform at 20 °C (blocks 0 and 1)

`0` = 0 V or VCOM_DC, `+` = VDH, `−` = VDL, `R` = VDHR.

| Step | Repeat | Frames | VCOM | White (R23h) | Black (R24h) | Yellow (R22h) |
|---|---:|---|---|---|---|---|
| 1 | 1 | 24 | `0` | `0` | `0` | `−` |
| 2 | 1 | 132/132 | `0 0` | `0 +` | `− −` | `− −` |
| 3 | 30 | 10/10 | `0 0` | `+ −` | `+ −` | `+ −` |
| 4 | 5 | 80/2/80/2 | `0 0 0 0` | `+ 0 − 0` | `+ 0 − 0` | `+ 0 − 0` |
| 5 | 36 | 3/2/6/2 | `0 0 0 0` | `− 0 0 0` | `0 0 + 0` | `− 0 + 0` |
| 6 | 6 | 10/5/50/5 | `0 0 0 0` | `0 0 0 0` | `0 0 0 0` | `− 0 R 0` |
| 7 | 8 | 5/6/50/3 | `0 0 0 0` | `0 0 0 −` | `0 + 0 0` | `− 0 R 0` |

The likely purpose of each step ⚠️:
- **Step 2** drives the old image out: white goes to `+`, black and yellow to `−`.
- **Steps 3–4** shake the particles. All three LUTs are identical.
- **Step 5** separates black from white.
- **Steps 6–7** develop yellow with VDHR, while black and white rest at 0 V, then top up black and white.

### How it changes with cold

| Blocks | ≥ °C | Shape |
|---|---|---|
| 0–1 | 30, 20 | As above |
| 2–3 | 16, 12 | The same 7 steps, with 35–40 repeats of the shaking step instead of 30 |
| 4–12 | 8 to −5 | Step 2 becomes three long phases (up to 240 frames). **VCOM swings**: `− +` in step 3, against the data's `+ −`, and `+ −` in step 5 |

At −5 °C (block 12):

| Step | Repeat | Frames | VCOM | White | Black | Yellow |
|---|---:|---|---|---|---|---|
| 1 | 1 | 79 | `0` | `0` | `0` | `−` |
| 2 | 1 | 108/220/200 | `0 0 0` | `+ + +` | `− − −` | `− − −` |
| 3 | 30 | 10/10 | `− +` | `+ −` | `+ −` | `+ −` |
| 4 | 4 | 80/80 | `0 0` | `+ −` | `+ −` | `+ −` |
| 5 | 20 | 12/12 | `+ −` | `− −` | `+ +` | `+ 0` |
| 6 | 6 | 8/5/60/5 | `+ 0 0 0` | `+ 0 0 0` | `+ 0 0 0` | `− 0 R 0` |
| 7 | 8 | 9/6/60/6 | `0 0 0 0` | `0 0 0 −` | `0 + 0 0` | `− 0 R 0` |

As on the Chroma60, the swinging VCOM doubles the voltage across the pixel, and a driver that only uses a warm block never reaches this mode.

### JH1 factory NVRAM

| Type | Value |
|---|---|
| `0x01` | `JH 10 00 50 98` |
| `0x05` | `d5` + `76Q1029842894-Y005` |
| `0x14` | `90 01 2c 01 07 00`: 400 × 300, controller type 7 |
| `0x09` / `0x12` | `02e7` / `0a5a` |

No VCOM record (`0x23`).

## What the stock firmware checks

Each stock image accepts only its own format version: JC00002947B (JC0) only `03 02`, and the JH1/JC1 image only `03 03`. The JH1 image:
- checks the magic and the version;
- takes every position from the header;
- walks the temperature table with no count limit;
- uses block offsets and record lengths without checking them;
- never checks the payload checksum before use.

The checksum is only checked when a waveform is downloaded over the radio. The JC0 image's lookup hasn't been traced. Details for all boards: [stock_firmware.md](stock_firmware.md#what-the-stock-firmware-checks).

## Compared with the OEPL drivers

**`chroma42/screen.c`** compiles in **row 2 of JC00002960B** (20–29 °C) byte for byte: VCOM1, R21h, R22h, G2, VCOM2, R0 and R1, with VCM_DC `0x08`. It uses PLL `0x39` for both refreshes; the stock row uses `0x39` for the B/W refresh and `0x29` for the red one.

**`chroma42/screen_8176.c`** compiles in **blocks 0/1 of JH10005098** as its `gBWY` table, byte for byte: PWR `03 00 2b 2b 0a`, PLL `3a`, the four LUTs and VDCS `12`. Its `gBWR` table, for JC1 tags, isn't from this dump.

| | Stock (JH1) | OEPL `screen_8176.c` |
|---|---|---|
| Waveform | The block for the current temperature | Blocks 0/1 (≥20 °C) at all temperatures |
| CDI | `57` | `87` |
| VDCS | `12` (−1.00 V) | `12` for BWY; `1e` (−1.60 V) in `gBWR` |
| Below −5 °C | Refuses to refresh | Refreshes with the warm block |

Both CDI values give a white border. They differ in DDX: with `87` (DDX = 00) a B/W bit of 0 selects the white LUT and 1 the black LUT, the opposite of the stock `57`. Red is the same in both.

## Open questions

1. JC0: the index map's G2 entries (see the Chroma29 doc).
2. JH1: whether any UC8176 tag has a VCOM-measure script.
3. Confirmation of the colour code at 0x203E / 0x203B. A JC1 (BWR, UC8176) tag should have `01`.
4. The UC8176 LUT registers (R20h–R24h): their layout, levels and which one holds red/yellow. The UC8176 datasheet revision these docs use (c_A 0.1) doesn't describe them.
5. Real refresh times. The figures above are calculated.
6. The waveform of a JC1 (BWR, UC8176) tag. The OEPL `gBWR` table (PWR `…0c`, PLL `29`) presumably comes from one.
7. The waveform of a JH0 (BWY, dual UC8154) tag. The wiki says the `chroma42` build's BWR LUTs wash out its yellow.
8. How the stock firmware drives the two cascaded UC8154s on JC0 and JH0 tags. No issue-2 stock image has been analysed.
