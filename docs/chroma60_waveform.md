# Chroma60 factory waveform tables (formats 03 03 and 02 03)

Decoded from the SPI flash of four Chroma60 tags with a UC8159 controller and a 600×448 panel. Three of them, JD (BWR) and JJ (BWY), have format 3.3, and their waveforms (0x2000–0x55C6) are byte-identical, so both colours use the same LUTs. The fourth, an older JD tag, has format 2.3, which is described in [Format 2.3](#format-23). Everything else in this document is about format 3.3.

Unlike the Chroma21's table format ([chroma21_waveform.md](chroma21_waveform.md)), this is a small **script** that the stock firmware interprets. The interpreter is at 0x205B in the stock CC1110 image (MD5 `860a7a49…`), and all firmware addresses below refer to that image. `Chroma_Tag_FW/board/chroma60/decode_factory.py dump.bin [block]` prints the NVRAM, the per-temperature settings and one block's LUTs.

Every byte from 0x2000 to 0x55C6 is accounted for, and the data ends exactly where the header says: 0x55C7 is the first erased (`0xFF`) byte.

Register meanings come from the UltraChip UC8159 datasheet (version c_B, revision 0.5, preliminary).

**Confidence:** ✅ confirmed (by the data, the stock firmware or the datasheet), ⚠️ strong inference, ❓ unknown.

## Layout

| Address | Size | Contents | |
|---|---|---|---|
| 0x2000 | 4 | Magic `ce fa ef be` (0xBEEFFACE, little endian) | ✅ |
| 0x2004 | 2 | Format version `03 03` (`02 03` in format 2.3); the firmware checks both bytes | ✅ |
| 0x2006 | 27 | Name, NUL padded: `P17D01.06 V3 UC8159 60E4` | ✅ |
| 0x2021 | 4 | `00 00 00 00` | ❓ |
| 0x2025 | 2 | Payload length, `0x359D` | ✅ |
| 0x2027 | 1 | `0x26`: the payload starts this far past 0x2004, at 0x202A | ✅ |
| 0x2028 | 1 | Payload checksum, `0xE3` | ✅ |
| 0x2029 | 1 | `00` | ❓ |
| 0x202A | 16 | Section offsets (see below) | ✅ |
| 0x203A | 1 | `0x32`: power-on delay × 100 ms (5 s). `0` also means 5 s | ✅ |
| 0x203B | 1 | `01`: the stock firmware copies it into a radio response; no display use. Probably the colour code, red (`02` would be yellow). The JJ (BWY) tag also has `01`, because its waveform is the red one. See [Colour code](chroma29_waveform.md#colour-code-0x203e-and-0x203b). | ⚠️ |
| 0x203C | 12 | Zero | |
| 0x2048 | 79 | Init script | ✅ |
| 0x2097 | 35 | Refresh script | ✅ |
| 0x20BA | 39 | VCOM-measure script | ✅ |
| 0x20E1 | 14 × 9 | Temperature table | ✅ |
| 0x215F | 13 × 1032 | Temperature blocks | ✅ |
| 0x55C7 | | End; erased flash follows | ✅ |

The stock firmware loads the 68 bytes from 0x2004 to 0x2047 into RAM at boot, after checking the magic.

**Checksum.** The bytes from 0x202A to 0x55C6 plus the byte at 0x2028 sum to 0 modulo 256. The Chroma21 script obeys the same rule (`0x8A`), and the same four bytes at 0x2025 give its length and header size too, so its `85 06 31 8a` is not a 32-bit checksum.

### Section offsets (0x202A)

Four little-endian u32s, relative to the payload start at 0x202A:

| Address | Offset | Target | Firmware use |
|---|---|---|---|
| 0x202A | `0x1E` | 0x2048 | Init script |
| 0x202E | `0xB7` | 0x20E1 | Temperature table |
| 0x2032 | `0x6D` | 0x2097 | Refresh script |
| 0x2036 | `0x90` | 0x20BA | VCOM-measure script; `0` skips the measurement |

0x203A–0x203B are not a fifth offset. They look like `0x132`, which would point inside the temperature table, but the firmware reads 0x203A as the delay byte and 0x203B separately.

## Script records

Each record starts with a type byte and a little-endian u16 argument. The dispatcher is a jump table at 0x20FC.

| Type | Bytes after the header | Action | |
|---|---|---|---|
| `00` | none | End of script | ✅ |
| `01` | `arg` | Send a command: 1 command byte and `arg − 1` data bytes | ✅ |
| `02` | none | Delay `arg` ms. Not used in this script | ✅ |
| `03` | none | Poll BUSY every 10 ms, up to 120 s | ✅ |
| `04` | 1 | Send the command byte, then `arg` zero bytes | ✅ |
| `05` | none | Sleep until BUSY rises (500 ms, then up to 23 periods of 10 s), then poll as `03` | ✅ |
| `06` | none | Reset pulse: 200 µs low, then 10 ms | ✅ |
| `07` | none | Send VDCS, TRES and the image, with up to 3 attempts (see [Image data](#image-data)) | ✅ |
| `08` | none | Read R81h and save it as VCOM in NVRAM record `0x23` | ✅ |
| `09` | none | Panel power: `arg` bit 0 = on | ✅ |

Types 02–09 are 3 bytes long. They are not all waits, and type 04 sends `arg` zeros, not `arg − 1`.

## Scripts

A refresh runs **init, then the temperature block, then refresh**.

### Init (0x2048)

```
09 arg=1      panel power on
06            reset pulse
06            reset pulse
01 R06h 0e cd 26   BTST
01 R00h ef 80      PSR
01 R03h 00         PFS
01 R82h 22         VDCS placeholder; type 07 replaces it with the NVRAM value
04 R29h 260 zeros  XON LUT
04 R23h 260 zeros  LUT, code 001
04 R24h 260 zeros  LUT, code 010
04 R26h 260 zeros  LUT, code 101
04 R27h 260 zeros  LUT, code 110
04 R28h 260 zeros  LUT, code 111
01 R60h 22         TCON
01 R41h 80         TSE
01 R50h 77         CDI
01 R65h 00         DAM off: no host bypass to the panel's flash pins
00
```

R29h is 200 bytes long, so 60 of its zeros are surplus.

### Refresh (0x2097)

```
07            VDCS, TRES, image
01 R04h       PON
03            wait for BUSY
01 R12h       DRF
05            sleep until BUSY
03            wait for BUSY
01 R02h       POF
03            wait for BUSY
01 R07h a5    deep sleep
00
```

The panel is powered up **after** the image has been sent.

### VCOM measure (0x20BA)

```
07            VDCS, TRES, image
01 R01h 07 01 04 3c   PWR
01 R04h       PON
03            wait for BUSY
01 R80h 11    AMV: start the measurement
03            wait for BUSY
08            read R81h, save to NVRAM
01 R02h       POF
03            wait for BUSY
00
```

This doesn't run on a normal refresh. It runs when a radio command sets bit 2 of the flags at XRAM 0xF48F, after the init script. That is how each tag's VCOM got into NVRAM.

### Register values

| Register | Value | Meaning | |
|---|---|---|---|
| R00h PSR | `ef` | RES = 11 (600×448), UD = 1 (scan up), SHL = 1, SHD_N = 1, RST_N = 1 | ✅ |
| | | Bits 5–4 are "don't care" in the datasheet. Other UC8159 drivers treat bit 5 as selecting LUTs from registers instead of flash, which matches how the stock firmware works. | ⚠️ |
| | `80` | Byte 2. The datasheet lists all 8 bits as "don't care". | ❓ |
| R03h PFS | `00` | Power-off sequence of 1 frame | ✅ |
| R06h BTST | `0e` | Phase A: 10 ms, strength 001 (reserved), minimum off time 3.43 µs | ✅ |
| | `cd` | Phase B: 40 ms, strength 001 (reserved), minimum off time 1.61 µs | ✅ |
| | `26` | Phase C: strength 3, minimum off time 3.43 µs | ✅ |
| R41h TSE | `80` | External temperature sensor, offset 0. The stock firmware picks the waveform itself, using the CC1110's sensor. | ✅ |
| R50h CDI | `77` | VBD = 011: the border follows the White LUT. DDX = 1, interval 10 Hsync. | ✅ |
| R60h TCON | `22` | Source-to-gate and gate-to-source non-overlap both 12 × 500 ns (the default) | ✅ |
| R65h DAM | `00` | Host bypass to the panel's flash pins off (the default) | ✅ |
| R80h AMV | `11` | Measure for 5 s, gates scanning, source at 0 V, result read with R81h | ✅ |
| R82h VDCS | NVRAM | −0.05 V per step: `0x20` = −1.60 V, `0x21` = −1.65 V | ✅ |

## Temperature table (0x20E1, 14 entries × 9 bytes)

Warmest first. Each entry is an int8 threshold in °C, a little-endian u32 offset from 0x202A, and 4 zero bytes.

The thresholds are 25, 20, 16, 12, 8, 7, 6, 5, 4, 3, 2, 1, −5, the same as the Chroma21's. The last entry, −100 with offset `0x359D`, marks the end.

The lookup at 0x26B3 takes the first entry whose threshold is at or below the temperature. A threshold below −99 ends the search with offset 0, which the caller treats as "no waveform": **the stock firmware refuses to refresh below −5 °C.** See [Stock refresh checks](#stock-refresh-checks).

## Temperature blocks (0x215F, 13 × 1032 bytes)

Every block has the same six records, in this order, followed by `00 00 00`:

```
01 R01h <4 bytes>    PWR
01 R30h <1 byte>     PLL
01 R20h <220 bytes>  VCOM LUT
01 R22h <260 bytes>  LUT, code 011: white
01 R21h <260 bytes>  LUT, code 000: black
01 R25h <260 bytes>  LUT, code 100: red/yellow
```

R29h stays zero, so XON isn't used.

| # | ≥ °C | Address | PWR byte 3 | VSH_LV | PLL | Rate | Frames | Time |
|---|---|---|---|---:|---|---|---:|---:|
| 0 | 25 | 0x215F | `06` | 4.2 V | `3a` | 100 Hz | 2379 | 24 s |
| 1 | 20 | 0x2567 | `07` | 4.4 V | `3a` | 100 Hz | 2379 | 24 s |
| 2 | 16 | 0x296F | `07` | 4.4 V | `19` | 86 Hz | 2443 | 28 s |
| 3 | 12 | 0x2D77 | `0c` | 5.4 V | `19` | 86 Hz | 2860 | 33 s |
| 4 | 8 | 0x317F | `0e` | 5.8 V | `3b` | 67 Hz | 2652 | 40 s |
| 5 | 7 | 0x3587 | `0f` | 6.0 V | `11` | 57 Hz | 2784 | 49 s |
| 6 | 6 | 0x398F | `10` | 6.2 V | `11` | 57 Hz | 2793 | 49 s |
| 7 | 5 | 0x3D97 | `11` | 6.4 V | `11` | 57 Hz | 2802 | 49 s |
| 8 | 4 | 0x419F | `13` | 6.8 V | `11` | 57 Hz | 2819 | 49 s |
| 9 | 3 | 0x45A7 | `15` | 7.2 V | `11` | 57 Hz | 2837 | 50 s |
| 10 | 2 | 0x49AF | `17` | 7.6 V | `3c` | 50 Hz | 2984 | 60 s |
| 11 | 1 | 0x4DB7 | `19` | 8.0 V | `3c` | 50 Hz | 3003 | 60 s |
| 12 | −5 | 0x51BF | `1a` | 8.2 V | `3c` | 50 Hz | 3012 | 60 s |

**PWR** is `07 01 xx 3c`:

| Byte | Value | Meaning | |
|---|---|---|---|
| 1 | `07` | VCOM driven (not floating); internal DC/DC for source, source LV and gate power | ✅ |
| 2 | `01` | VGH = 19 V, VGL = −19 V | ✅ |
| 3 | varies | VSHC_LVL: **VSH_LV**, the red LUT voltage. 3.0 V + 0.2 V per step, rising with cold. | ✅ |
| 4 | `3c` | VSLC_LVL: VSL_LV = −15 V. No LUT uses it. | ✅ |

The VCOM-measure script's `07 01 04 3c` sets VSH_LV to 3.8 V.

**PLL** is `M << 3 | N`, and the rates are from the datasheet's table: `3a` is M = 7, N = 2; `19` is M = 3, N = 1; `3b` is M = 7, N = 3; `11` is M = 2, N = 1; `3c` is M = 7, N = 4 ✅.

**Frames** are from the LUTs ✅. **Time** is frames ÷ rate, not a measurement ⚠️.

**Blocks 0 and 1 have identical LUTs and PLL**; only PWR byte 3 differs. Within each block, all four LUTs share the same step timing.

## LUT format

VCOM LUTs are 20 rows of 11 bytes and colour LUTs are 20 rows of 13 bytes. Only the first 8 rows are used in any block, so 82 and 98 bytes are significant; the rest are zero.

| LUT | Repeat | Levels | Frames |
|---|---|---|---|
| VCOM | 1 byte | 2 bytes: 8 × 2 bits, MSB first | 8 bytes |
| Colour | 1 byte | 4 bytes: 8 × 4 bits, MSB first; the top bit of each is unused | 8 bytes |

| Level | VCOM (R20h) | Colour |
|---|---|---|
| 0 | VCM_DC | 0 V |
| 1 | +15 V + VCM_DC | VSH (+15 V) |
| 2 | −15 V + VCM_DC | VSL (−15 V) |
| 3 | floating | VSH_LV |
| 4–6 | | VSL_LV, VSH_LVX, VSL_LVX |
| 7 | | floating |

The colour LUTs only use levels 0–3, and the VCOM LUT only uses levels 0–2.

## Pixel codes

| Code | Register | Colour | |
|---|---|---|---|
| `000` | R21h LUTB | Black | ✅ |
| `011` | R22h LUTW | White | ✅ |
| `100` | R25h LUTR0 | Red/yellow | ✅ |

This is the datasheet's mapping for DDX = 1. Unlike on the Chroma21's UC8154, the LUT names match the colours they produce. The stock firmware's fill patterns agree: `0x00`, `0x33` and `0x44` at 4 bits per pixel.

## The waveform at 20 °C

Blocks 0–2 have this shape; these frame counts are from blocks 0 and 1. `0` = VCM_DC in the VCOM column and 0 V elsewhere, `+` = VSH, `−` = VSL, `L` = VSH_LV.

| Step | Repeat | Frames | VCOM | Black | White | Red/yellow |
|---|---:|---|---|---|---|---|
| 1 | 1 | 55/100/36/72 | `0 0 0 0` | `0 0 − −` | `0 0 0 +` | `− − − −` |
| 2 | 4 | 85/85 | `0 0` | `− +` | `− +` | `− +` |
| 3 | 12 | 10/10 | `0 0` | `− +` | `− +` | `− +` |
| 4 | 15 | 4/6 | `0 0` | `0 +` | `− 0` | `0 +` |
| 5 | 8 | 5/45/15 | `0 0 0` | `0 0 0` | `0 0 0` | `− L 0` |
| 6 | 4 | 4/45/15 | `0 0 0` | `0 0 0` | `0 0 0` | `− L 0` |
| 7 | 2 | 60/60 | `0 0` | `0 0` | `0 0` | `L L` |
| 8 | 3 | 4/6 | `0 0` | `0 +` | `− 0` | `0 0` |

The likely purpose of each step ⚠️:
- **Step 1** drives the old image out, with different pre-drive for each colour.
- **Steps 2–3** shake the particles. All three LUTs are identical here.
- **Step 4** separates black from white: black ends on `+` and white on `−`.
- **Steps 5–7** develop red with the low voltage, while black and white are held at 0 V.
- **Step 8** tops up black and white, and red stays at 0 V.

## How it changes with cold

Step timing for each block, as `repeat × frames`:

| Block | ≥ °C | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|---|
| 0, 1 | 25, 20 | 55/100/36/72 | 4×85/85 | 12×10/10 | 15×4/6 | 8×5/45/15 | 4×4/45/15 | 2×60/60 | 3×4/6 |
| 2 | 16 | 43/100/42/84 | 4×80/80 | 12×12/12 | 18×4/6 | 8×5/40/20 | 4×4/45/20 | 2×60/60 | 3×4/6 |
| 3 | 12 | 73/100/25/125 | 5×80/80 | 12×12/12 | 22×5/4/2 | 4×6/40/20 | 8×5/50/25 | 3×45/45 | 3×5/6 |
| 4 | 8 | 46/100/22/110 | 5×90/90 | 12×10/10 | 10×5/6 | 4×6/40/10 | 8×6/40/30 | 3×45/45 | 2×5/6 |
| 5 | 7 | 16/40/40/156 | 5×90/90 | 12×10/10 | 12×6/6 | 4×7/40/40 | 8×7/40/40 | 2×45/45 | 2×6/6 |
| 6 | 6 | 5/50/50/156 | 5×90/90 | 12×10/10 | 12×6/6 | 4×7/40/40 | 8×7/40/40 | 2×45/45 | 2×6/6 |
| 7 | 5 | 4/10/100/156 | 5×90/90 | 12×10/10 | 12×6/6 | 4×7/40/40 | 8×7/40/40 | 2×45/45 | 2×6/6 |
| 8 | 4 | 31/50/50/156 | 5×90/90 | 12×10/10 | 12×6/6 | 4×7/40/40 | 8×7/40/40 | 2×45/45 | 2×6/6 |
| 9 | 3 | 49/50/50/156 | 5×90/90 | 12×10/10 | 12×6/6 | 4×7/40/40 | 8×7/40/40 | 2×45/45 | 2×6/6 |
| 10 | 2 | 78/50/50/192 | 5×100/100 | 15×10/10 | 15×6/6 | 3×8/40 | 8×7/40/40 | 3×45/45 | 2×6/6 |
| 11 | 1 | 97/50/50/192 | 5×100/100 | 15×10/10 | 15×6/6 | 3×8/40 | 8×7/40/40 | 3×45/45 | 2×6/6 |
| 12 | −5 | 56/50/100/192 | 5×100/100 | 15×10/10 | 15×6/6 | 3×8/40 | 8×7/40/40 | 3×45/45 | 2×6/6 |

From 12 °C down the levels change as well as the timing:

| Blocks | ≥ °C | VCOM | Black | White | Red/yellow |
|---|---|---|---|---|---|
| 0–2 | 16 and up | Flat at VCM_DC | as above | as above | as above |
| 3 | 12 | Flat at VCM_DC | Step 4 `0 + +` | Step 4 `− 0 0` | Step 4 `0 + 0` |
| 4–9 | 8 to 3 | Steps 3–4 alternate `+ −` | Step 4 `+ +` | Step 4 `− −` | Step 4 `+ 0` |
| 10–12 | 2 to −5 | Steps 2–4 alternate `+ −` | as 4–9 | as 4–9 | as 4–9 |

In addition, from block 5 down, black's step 1 is `0 0 0 −` rather than `0 0 − −`.

The swinging VCOM runs in antiphase with the `− +` source drive, which puts 30 V across the pixel instead of 15 V. **A driver that only uses a warm block therefore doesn't reach this mode at all.**

## Image data

Type 07 at 0x218C runs three steps, up to 3 times:
1. Send R82h with the VCOM from NVRAM, and R61h with the resolution.
2. Send the image with R10h (DTM1) at 4 bits per pixel, or a fill.
3. Read R71h and check bit 3, `data_flag`.

If all three attempts fail, the firmware resets the panel, puts it in deep sleep and abandons the script.

The datasheet says a host must send R11h (DSP) to end the transfer. The stock firmware never does: it checks R71h and goes on to DRF. OEPL doesn't send it either.

The stock image has two 1-bit planes, 33600 bytes each, read 120 bytes at a time. The table at 0x07E7 turns each pair of plane bits into a 4-bit code:

| Plane 1 | Plane 2 | Code | Colour |
|---|---|---|---|
| 0 | 1 | `000` | Black |
| 1 | 1 | `011` | White |
| 1 | 0 | `100` | Red/yellow |
| 0 | 0 | `011` | White |

So plane 1 means "not black" and plane 2 means "not red", as on the Chroma21.

## Stock refresh checks

The check at 0x1E32 returns one of these:

| Status | Condition | |
|---|---|---|
| 1 | Go ahead | ✅ |
| 2 | Battery below the threshold, by default 2200 mV | ✅ |
| 3 | No waveform, or no block for this temperature (below −5 °C) | ✅ |
| 8 | Sensor reading below −300, presumably −30.0 °C | ⚠️ |

The temperature reading is divided by 10 before the block lookup.

**Power on** (type 09), if the panel isn't already powered:
1. Wait 0x203A × 100 ms.
2. P0.6 low, wait 20 ms.
3. nCS and RESET high, P0.0 and D/C high.
4. Reset pulse, deep sleep (`R07h a5`), wait 10 ms.

The stock firmware does this at boot, so a normal refresh doesn't repeat the 5 s delay.

## What the stock firmware checks

The stock JD/JJ image accepts only format `03 03`: its version compare is at 0x2807, so it would reject the 2.3 waveform of JD00000725. That tag's own stock image hasn't been dumped. Apart from the checks above, the image:
- checks the magic and the version;
- takes the section offsets from the header;
- walks the temperature table until a threshold below −99;
- uses block offsets and record lengths without checking them;
- never checks the payload checksum before use.

Details for all boards: [stock_firmware.md](stock_firmware.md#what-the-stock-firmware-checks).

## Factory NVRAM (same record format as the other Chroma boards)

| Type | Meaning |
|---|---|
| `0x01` | Tag serial |
| `0x05` | Panel serial as printed on the back of the panel, with a leading flag byte. JD panels end in `-M…`, the JJ panel in `-Y003`. |
| `0x09` / `0x12` | Temperature sensor calibration (intercept / slope) |
| `0x14` | Resolution, `58 02 c0 01 00 00` = 600 × 448. The stock firmware writes this default if the record is missing. |
| `0x23` | VCOM, 5 bytes, only the first is used. Measured per tag: `0x20` and `0x21` on the JD tags, `0x21` on the JJ tag. |

The NVRAM may be in sector 0 or sector 1, because the stock firmware moves it between them.

## Format 2.3

An older JD tag (JD00000725) has format `02 03`, named `8159v2EPD P15D02.00 UC 60E4`. It's the same script format as 3.3: the header fields, the record types and the init and refresh scripts are the same, and every block has the same six records in the same order. These differ:

| | 3.3 | 2.3 |
|---|---|---|
| Version at 0x2004 | `03 03` | `02 03` |
| Payload length, checksum | `0x359D`, `0xE3` | `0x1C65`, `0xE4`. The data ends at 0x3C8E; 0x3C8F is the first erased byte. |
| Init, refresh, VCOM-measure scripts | 0x2048, 0x2097, 0x20BA | 0x204B, 0x209A, 0x20BD |
| 0x203B, probably the colour code ⚠️ | `01` | `00`: not set, as in other older-revision waveforms |
| VCOM-measure script | Starts with a wait (type `07`); PWR `07 01 04 3c` | No wait; PWR `07 01 06 3c` |
| Temperature table | 0x20E1, 13 entries and the end marker | 0x20E1, 7 entries and the end marker |
| Blocks | 13 × 1032 bytes from 0x215F | 7 × 1002 bytes from 0x2129 |
| Colour LUT records | 260 bytes | 250 bytes: the same 20 rows of 13 bytes without the last 10, which are zero in both formats |

The checksum rule is the same, and holds for this dump.

The blocks ✅ (times calculated as for 3.3 ⚠️):

| # | ≥ °C | Address | PWR byte 3 | VSH_LV | PLL | Rate | Frames | Time |
|---|---|---|---|---:|---|---|---:|---:|
| 0 | 35 | 0x2129 | `06` | 4.2 V | `3a` | 100 Hz | 758 | 8 s |
| 1 | 30 | 0x2513 | `06` | 4.2 V | `3a` | 100 Hz | 1018 | 10 s |
| 2 | 20 | 0x28FD | `06` | 4.2 V | `3a` | 100 Hz | 1443 | 14 s |
| 3 | 15 | 0x2CE7 | `07` | 4.4 V | `3a` | 100 Hz | 1586 | 16 s |
| 4 | 10 | 0x30D1 | `0a` | 5.0 V | `3a` | 100 Hz | 1830 | 18 s |
| 5 | 3 | 0x34BB | `10` | 6.2 V | `3a` | 100 Hz | 2042 | 20 s |
| 6 | −5 | 0x38A5 | `18` | 7.8 V | `3c` | 50 Hz | 1264 | 25 s |

As in 3.3, all four LUTs in a block share the same step timing and only the first 8 rows are used. Unlike 3.3, no two blocks have the same LUTs.

The waveforms are not a subset of 3.3's. At 20 °C (block 2), using the symbols of [The waveform at 20 °C](#the-waveform-at-20-c):

| Step | Repeat | Frames | VCOM | Black | White | Red/yellow |
|---|---:|---|---|---|---|---|
| 1 | 1 | 10/11 | `0 0` | `0 0` | `0 0` | `− −` |
| 2 | 25 | 3/4 | `0 0` | `0 −` | `+ 0` | `0 −` |
| 3 | 25 | 10/10 | `0 0` | `− +` | `− +` | `− +` |
| 4 | 25 | 3/4 | `0 0` | `0 +` | `− 0` | `0 0` |
| 5 | 1 | 1/1 | `0 0` | `0 0` | `0 0` | `0 0` |
| 6 | 1 | 1/1 | `0 0` | `0 0` | `0 0` | `0 0` |
| 7 | 6 | 5/53 | `0 0` | `0 0` | `0 0` | `− L` |
| 8 | 2 | 55/55 | `0 0` | `0 0` | `0 0` | `L L` |

There is no long pre-drive step. Instead, step 2 drives black and white the opposite way to step 4, before the shaking ⚠️. The same LUT registers hold the same colours as in 3.3 (R21h black, R22h white, R25h red), so OEPL's register remap applies unchanged.

## Compared with the OEPL driver

`Chroma_Tag_FW/board/chroma60/screen.c` reads the block for the current temperature from SPI flash at run time. It differs from the stock sequence in these ways:

| | Stock | OEPL |
|---|---|---|
| Formats | Each stock image checks for its own version | Accepts 3.3 and 2.3 |
| Order | Init, block, image, PON, DRF | BTST, PWR, PON, then the rest of the setup, the LUTs, the image, DRF |
| White / red LUT registers | R22h / R25h, codes `011` / `100` | R27h / R28h, codes `110` / `111` |
| Below −5 °C | Refuses to refresh | Uses the −5 °C block |
| No waveform in flash | Refuses to refresh | Also refuses: there's no compiled-in fallback, so the LUTs would be empty |
| PFS, TCON, TSE | Sent | Left at their reset values |
| Border | VBD = 011: the White LUT in R22h | VBD = 110 (`0xd7`): R27h, which holds the same LUT |

## Tested on hardware

On a JD (BWR) tag, an image displays correctly with the block selected by temperature, both at room temperature and with the display cooled to about 5 °C, where the refresh is noticeably slower. Earlier runs, using block 1 and other settings, gave these results:

| Change | Result |
|---|---|
| VCOM from `0x08` to `0x3a` | Only moves the threshold |
| Forcing blocks 3 and 12 | No change to the grey below |
| XON LUT, other pixel codes, the exact factory command order | No change |
| Half the repeats in steps 2 and 3 | Less bloat on thick text |

The problem being tested is that thin vertical features render grey, while horizontal ones are fine, and rows with many transitions are greyer. It isn't solved. The S2 Mini's 3.3 V supply sagging under load was the leading suspect, pending a test on batteries.

Not tested yet:
- The blocks below about 5 °C.
- Yellow rendering on the JJ tag with this waveform.
- Format 2.3. It has only been checked on a host harness against the JD00000725 dump.

## Open questions

1. PSR byte 2 (`80`) and bit 5, which the datasheet doesn't define.
2. BTST strength 001, which the datasheet calls reserved.
3. 0x2021 and 0x2029, and confirmation of the colour code at 0x203B.
4. Real refresh times. The figures above are calculated.
5. Which tags have which format, and whether there are others. It doesn't follow the serial number range: JD00000725 has 2.3, but JD00006303 has 3.3.
