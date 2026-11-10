# Chroma29 factory waveform tables (formats 03 02 and 04 02)

Decoded from the SPI flash of two Chroma29 tags (296×128, per NVRAM record `0x14`):

| Tag | Format | Name |
|---|---|---|
| JA00013753B | `03 02` | `v3EPD T14C22.4 UC 29E4` |
| JA10023278B | `04 02` | `P15H01.06 EPD V4 UC 29E4` |

Both use the table format of the Chroma21 ([chroma21_waveform.md](chroma21_waveform.md)): a parameter block, a temperature table, an index map and a pool of 15-byte LUTs. The differences are in the counts, and JA10023278B uses several temperature entries per threshold to run extra refresh passes (see [Extra refresh passes](#extra-refresh-passes)). `Chroma_Tag_FW/tools/decode_waveform.py dump.bin [block] [--steps]` prints everything below for either dump; `decode_chroma21.py` can't read the `04 02` dump, because it assumes the map has one row per temperature entry.

Firmware addresses refer to the stock CC1110 image of JA10023278B (from a disassembly of that image). Its EPD routines are at the same addresses as the Chroma21's: 0x20F0 (send a 15-byte LUT), 0x2175 (PSR + TRES), 0x21B4 (PWR), 0x2606 (refresh). No stock image from a `03 02` tag has been analysed.

**Controller.** Chroma29 tags have a UC8154, except JA1…C tags, which have a UC8151C (the `chroma29_8151` build). Both dumps here are from UC8154 tags (JA0…B and JA1…B), which fits the data: the command set (R20h–R27h, PLL, PSR `0x83`/`0x93`) and the 15-byte LUTs are the UC8154's. The frame rates below are from the UC8154 PLL table ✅. No UC8151C waveform has been dumped yet.

Every byte of both payloads is accounted for, and each ends where the header says: the next byte is erased (`0xFF`).

**Confidence:** ✅ confirmed (by the data, the stock firmware or a datasheet), ⚠️ strong inference, ❓ unknown.

## Layout

| Address | Size | Contents | |
|---|---|---|---|
| 0x2000 | 4 | Magic `ce fa ef be` | ✅ |
| 0x2004 | 2 | Format version. 0x2005 is probably the format family (`02` table, `03` script) and 0x2004 its revision ⚠️ | ✅ |
| 0x2006 | 27 | Name, NUL padded | ✅ |
| 0x2021 | 4 | `00 00 00 00` | ❓ |
| 0x2025 | 2 | Payload length | ✅ |
| 0x2027 | 1 | `0x31`: the payload starts this far past 0x2004, at 0x2035 | ✅ |
| 0x2028 | 1 | Payload checksum: the payload bytes plus this byte sum to 0 modulo 256 | ✅ |
| 0x2029 | 3 | `00 00 00` | ❓ |
| 0x202C | 3 | Temperature table: entry count, then a u16 offset from 0x2035 | ✅ |
| 0x202F | 3 | Index map: row count, offset | ✅ |
| 0x2032 | 3 | LUT pool: LUT count, offset | ✅ |
| 0x2035 | 18 | Parameters (see below) | partly |
| | | Temperature table, index map, LUT pool | ✅ |

| | JA00013753B (`03 02`) | JA10023278B (`04 02`) |
|---|---|---|
| Payload | 0x2035–0x23BF, 907 bytes, checksum `0x32` | 0x2035–0x2409, 981 bytes, checksum `0xDF` |
| Temperature table | 7 × 10 bytes at 0x2047 | 21 × 10 bytes at 0x2047 |
| Index map | 7 × 12 bytes at 0x208D | 9 × 12 bytes at 0x2119 |
| LUT pool | 49 × 15 bytes at 0x20E1 | 43 × 15 bytes at 0x2185 |
| First erased byte | 0x23C0 | 0x240A |

The checksum and the three table descriptors are the same in the Chroma21's waveform. Its unexplained header bytes `12`, `94` and `30 01` are the offsets 0x12, 0x94 and 0x130 of its three tables, and `85 06 31 8a` at 0x2025 is the length, the header size and the checksum.

## Parameters (0x2035)

The stock firmware copies these into RAM, as on the Chroma21.

| Address | Meaning | JA0 | JA1 | |
|---|---|---|---|---|
| 0x2035 | **VCM_DC (R82h)**, sent before every refresh | `08` (−0.8 V) | `0a` (−1.0 V) | ✅ |
| 0x2036 | TCON (R60h) | `22` | `22` | ✅ |
| 0x2037 | CDI (R50h) | `17` | `17` | ✅ |
| 0x2038 | BTST (R06h) | `07 07 0f` | `07 07 0f` | ✅ |
| 0x203B | PWR (R01h) bytes 1–2 | `03 00` | `03 00` | ✅ |
| 0x203D | Delay before the EPD is powered on, × 100 ms | `00` | `32` (5 s) | ✅ |
| 0x203E | Probably the **colour code**: `01` red, `02` yellow, `00` not set. Only reported to the host. See [Colour code](#colour-code-0x203e-and-0x203b). | `00` | `01` | ⚠️ |
| 0x203F | Delay before power off, ms (u16) | 2000 | 2000 | ✅ |
| 0x2041 | Zero | | | |

## Colour code (0x203E and 0x203B)

The table format has a power-on delay byte at 0x203D, and the script format (Chroma42 JH1, Chroma60, Chroma74) has one at 0x203A. The byte after it, 0x203E or 0x203B, behaves the same way in both formats. This section covers both.

**What the stock firmware does with it** ✅: it only reports the byte to the host. In the JA10023278B image, the one reader (0x1F62, returning byte +9 of the parameter copy, which is 0x203E) is called only from the handler for command `0x1A`, "Get displaySize". The JH10005098 (Chroma42) image is the same: the one call to its accessor (0x1EA7) is in its `0x1A` handler. Both put the byte last, at offset 0x0F of the 16-byte reply, after the height, width and controller type. Neither uses it to drive the display.

**What it probably means** ⚠️ (inferred from the values below; no firmware or document states it): the colour of the waveform's third colour, so the host knows how to render images for the tag. `01` = red, `02` = yellow, `00` = not set.

| Tag | Format | Waveform name | Panel | Value |
|---|---|---|---|---|
| JA00013753B Chroma29 | `03 02` | `v3EPD T14C22.4 UC 29E4` | | `00` |
| JC00002960B Chroma42 | `03 02` | `v3EPD P15C28.4 UC 42E4` | red | `00` |
| JD00000725 Chroma60 | `02 03` | `8159v2EPD P15D02.00 UC 60E4` | red | `00` |
| JA10023278B Chroma29 | `04 02` | `P15H01.06 EPD V4 UC 29E4` | | `01` |
| JL10420082B Chroma74 | `03 03` | `D20R20031DPP0BE_V3 UC59 74E41f` | red | `01` |
| JD Chroma60, 3 tags | `03 03` | `P17D01.06 V3 UC8159 60E4` | red | `01` |
| JJ Chroma60 | `03 03` | `P17D01.06 V3 UC8159 60E4` | yellow | `01` |
| JG Chroma21, 2 tags | `04 02` | `D18Y00010WY005_UC8154 21E4` | yellow | `02` |
| JH10005098 Chroma42 | `03 03` | `P16Y00.63 V3 UC8176 42E4` | yellow | `02` |
| JM10525366C Chroma74 | `03 03` | `D19Y10090WY06G_UC8159 74E42e` | yellow | `02` |

The reasons for the inference:
- Every waveform with `Y` in its name code (`D18Y`, `P16Y`, `D19Y`) has `02`. The `R`-named one (`D20R`) has `01`, and so do the other waveforms used on red panels.
- The JJ Chroma60 is a yellow panel with `01`, but its waveform is byte-identical to the red JD tags'. So the byte describes the waveform, not the panel, which fits a value written by whoever built the waveform.
- Every `00` is in the older revision of its format (`03 02` against `04 02`, `02 03` against `03 03`), and those names use the older `v3EPD…` / `8159v2EPD…` style. The field probably didn't exist yet and was left at zero.

The data also suggests that byte 0x2005 is the format family (`02` table, `03` script) and 0x2004 its revision ⚠️.

A dump from a red JB Chroma21 or a JC1 Chroma42 (BWR, UC8176) should have `01`, and a yellow tag with a `04 02` Chroma29 waveform should have `02`. Either would test the inference.

The value isn't a reliable colour test on its own: JJ reports `01`, and older formats report `00`.

## Temperature table (0x2047, 10-byte entries)

The byte meanings are the Chroma21's, which its stock firmware confirmed:

| Byte | Meaning |
|---|---|
| 0 | Threshold in °C (signed): the entry applies at or above it |
| 1 | PLL (R30h) for the first (B/W) refresh |
| 2 | `00` |
| 3 | Index map row |
| 4 | **VDPS_LV**, PWR byte 3: the red positive voltage, 2.4 V + 0.2 V per step |
| 5 | `80`: VDNS_LV = 0, bit 7 sets RVSHLS[0] in PWR byte 1 |
| 6 | Delay in ms, after the second refresh starts, before the red voltages are applied |
| 7 | PLL for the second (red) refresh |
| 8, 9 | `00` |

Frame counts are from the LUTs ✅. **Times** are frames ÷ rate for each refresh, added together, not measurements ⚠️.

### JA00013753B

| # | ≥ °C | Row | PLL 1 | PLL 2 | VDPS_LV | Byte 6 | Frames B/W + red | Time |
|---|---|---|---|---|---:|---:|---|---:|
| 0 | 35 | 0 | `39` 137 Hz | `39` 137 Hz | 3.0 V | 11 ms | 428 + 405 | 6 s |
| 1 | 30 | 1 | `39` 137 Hz | `39` 137 Hz | 3.0 V | 11 ms | 428 + 405 | 6 s |
| 2 | 20 | 2 | `29` 98 Hz | `29` 98 Hz | 4.2 V | 16 ms | 448 + 346 | 8 s |
| 3 | 15 | 3 | `29` 98 Hz | `29` 98 Hz | 4.2 V | 16 ms | 527 + 421 | 10 s |
| 4 | 10 | 4 | `29` 98 Hz | `29` 98 Hz | 4.4 V | 16 ms | 532 + 416 | 10 s |
| 5 | 3 | 5 | `29` 98 Hz | `29` 98 Hz | 6.4 V | 16 ms | 692 + 754 | 15 s |
| 6 | −5 | 6 | `2a` 50 Hz | `2a` 50 Hz | 7.8 V | 30 ms | 476 + 572 | 21 s |

### JA10023278B

| # | ≥ °C | Row | PLL 1 | PLL 2 | VDPS_LV | Byte 6 | Frames B/W + red | Time |
|---|---|---|---|---|---:|---:|---|---:|
| 0 | 30 | 0 | `39` 137 Hz | `29` 98 Hz | 3.6 V | 16 ms | 890 + 430 | 11 s |
| 1 | 25 | 1 | `39` 137 Hz | `29` 98 Hz | 3.6 V | 16 ms | 890 + 466 | 11 s |
| 2 | 20 | 2 | `39` 137 Hz | `29` 98 Hz | 3.8 V | 16 ms | 890 + 519 | 12 s |
| 3 | 15 | 3 | `29` 98 Hz | `29` 98 Hz | 5.0 V | 16 ms | 711 + 0 | |
| 4–8 | 15 | 4 | `29` 98 Hz | `29` 98 Hz | 5.0 V | 16 ms | 0 + 189, five times | 17 s in all |
| 9 | 10 | 5 | `29` 98 Hz | `29` 98 Hz | 5.6 V | 16 ms | 812 + 0 | |
| 10–14 | 10 | 6 | `29` 98 Hz | `29` 98 Hz | 5.6 V | 16 ms | 0 + 252, five times | 21 s in all |
| 15 | −5 | 7 | `2a` 50 Hz | `2a` 50 Hz | 7.0 V | 30 ms | 760 + 0 | |
| 16–20 | −5 | 8 | `2a` 50 Hz | `2a` 50 Hz | 7.0 V | 30 ms | 0 + 252, five times | 40 s in all |

## Extra refresh passes

In JA10023278B, entries 3–8, 9–14 and 15–20 share a threshold. The stock firmware runs each of them as a separate refresh pass ✅:

- The refresh loop at 0x2606 runs a pass, then calls 0x27C5.
- 0x27C5 keeps the index of the current entry (XRAM 0xF722) and reads the next one. If its threshold equals the current entry's, it copies the entry in, loads that entry's LUTs (0x2A2D) and returns "again". Otherwise it stops.
- From the second pass on, the loop also sends PWR again, with an argument that depends on the pass number ❓.

So below 20 °C a refresh is one pass with only B/W-stage LUTs (the red-stage slots of rows 3, 5 and 7 point at the all-zero `pool[0]`), followed by five passes with only red-stage LUTs (rows 4, 6 and 8 have all-zero B/W slots). The red/yellow colour is built up in five short refreshes.

JA00013753B has one entry per threshold, so it always runs one pass.

## Index map

Same slots as the Chroma21: 8 one-byte pool indexes, then 4 zero bytes. The stock refresh code (0x2606) loads R20h, R21h, R22h and R24h for the B/W stage, then R25h, R26h and R27h for the red stage ✅:

| Slot | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| Register | R20h VCOM1 | R25h VCOM2 | R22h | R23h G1, always `ff` | R24h G2 | R21h | R26h R0 | R27h R1 |
| Colour | | | white | | red | black | red | |

R21h drives black and R22h white, the opposite of the datasheet's names ⚠️. That was confirmed on Chroma21 hardware, and here too R21h is the LUT that ends on `+` and R22h the one that ends on `−`.

**The `03 02` map looks wrong in 5 of 7 rows** ⚠️. Rows 1, 3, 4, 5 and 6 point slot 4 (G2) at the same pool entry as slot 2 (white). Each of these rows' seven pool entries has one that nothing references, and it has the shape of a G2 LUT: the white LUT with a VSL pre-drive. In row 1, for example, the orphan `pool[10]` is byte-for-byte the G2 LUT of row 0. The stock firmware would use the map as it is, so at those temperatures red pixels get the white LUT in the B/W stage. The `04 02` map of JA10023278B doesn't have this problem.

## LUT format

The UC8154's 15-byte LUT: 5 groups of `[phase A] [phase B] [repeat]`, each phase byte `level << 6 | frames` ✅. In the B/W stage the levels are 0 V, VSH, VSL and floating. In the red stage they are 0 V, the red VSH (VDPS_LV) and the red VSL. Both VCOM LUTs stay at VCM_DC throughout, apart from JA10023278B's coldest row (see below).

## The waveform at 20 °C

`0` = 0 V or VCM_DC, `+` = VSH, `−` = VSL. In the red stage, `+` and `−` are the red voltages.

### JA00013753B, row 2

This is the block the OEPL driver uses (see [Compared with the OEPL driver](#compared-with-the-oepl-driver)).

B/W stage, 448 frames at 98 Hz:

| Step | Repeat | Frames | VCOM1 | Black (R21h) | White (R22h) | Red (R24h) |
|---|---:|---|---|---|---|---|
| 1 | 1 | 1/1 | `0 0` | `0 0` | `0 0` | `0 −` |
| 2 | 9 | 3/4 | `0 0` | `0 −` | `+ 0` | `− −` |
| 3 | 10 | 6/6 | `0 0` | `− +` | `− +` | `− +` |
| 4 | 25 | 4/4 | `0 0` | `− +` | `− +` | `− +` |
| 5 | 9 | 3/4 | `0 0` | `0 +` | `− 0` | `0 0` |

Red stage, 346 frames at 98 Hz:

| Step | Repeat | Frames | VCOM2 | R0 | R1 |
|---|---:|---|---|---|---|
| 1 | 1 | 10/10 | `0 0` | `+ +` | `0 0` |
| 2 | 13 | 2/20 | `0 0` | `− +` | `0 0` |
| 3 | 1 | 20/20 | `0 0` | `+ +` | `0 0` |

The likely purpose of each step ⚠️:
- **Step 2** drives black and white the opposite way to their final state.
- **Steps 3–4** shake the particles. All three LUTs are identical here.
- **Step 5** sets black (ends on `+`) and white (ends on `−`); red pixels are left at 0 V.
- **The red stage** drives only the pixels marked red (R0), with the red voltages.

### JA10023278B, row 2

B/W stage, 890 frames at 137 Hz:

| Step | Repeat | Frames | VCOM1 | Black (R21h) | White (R22h) | Red (R24h) |
|---|---:|---|---|---|---|---|
| 1 | 5 | 7/4 | `0 0` | `− 0` | `0 +` | `0 0` |
| 2 | 10 | 7/4 | `0 0` | `− 0` | `0 +` | `− −` |
| 3 | 20 | 14/14 | `0 0` | `− +` | `− +` | `− +` |
| 4 | 8 | 4/7 | `0 0` | `0 +` | `− 0` | `− 0` |
| 5 | 7 | 4/7 | `0 0` | `0 +` | `− 0` | `0 0` |

Red stage, 519 frames at 98 Hz:

| Step | Repeat | Frames | VCOM2 | R0 | R1 |
|---|---:|---|---|---|---|
| 1 | 1 | 25/25 | `0 0` | `+ +` | `0 0` |
| 2 | 4 | 4/49 | `0 0` | `− +` | `0 0` |
| 3 | 3 | 4/49 | `0 0` | `− +` | `0 0` |
| 4 | 2 | 25/24 | `0 0` | `+ +` | `0 0` |

The same pattern, with a longer pre-drive and one long shaking step.

## How it changes with cold

**JA00013753B.** All rows have the same shape: a pre-drive, two shaking steps and a final step. Colder rows use more repeats and longer frames, and the red stage gets longer (754 and 572 frames in rows 5 and 6, against 346 at 20 °C). Rows 0 and 1 have the same LUTs; only the index map differs.

**JA10023278B.** Rows 3, 5 and 7 (the B/W passes) replace the pre-drive with a long single step (30/21, then 36/36 frames at 15 °C). The red passes (rows 4, 6, 8) are five single-repeat steps:

| Step | Frames | R0 at 15 °C (row 4) |
|---|---|---|
| 1 | 3/30 | `− +` |
| 2 | 30/3 | `0 −` |
| 3 | 30/30 | `+ 0` |
| 4 | 3/30 | `− +` |
| 5 | 15/15 | `0 0` |

Rows 6 and 8 have the same pattern with 4 and 40 frames in place of 3 and 30, and 20/20 in step 5. Row 7 (−5 °C) is the only one where VCOM swings: VCOM1 is `+ −` in steps 3–5. In step 3 that runs against the data's `− +`, which doubles the voltage across the pixel, as on the Chroma60 below 8 °C.

## Factory NVRAM (same record format as the other Chroma boards)

| Type | JA00013753B | JA10023278B |
|---|---|---|
| `0x01` | `JA 00 01 37 53` | `JA 10 02 32 78` |
| `0x05` | `b1` + `00E4U54O0909439908` | `b1` + `00E4U54P0907336039` |
| `0x14` | `28 01 80 00 00 06`: 296 × 128 | same |
| `0x09` / `0x12` | `02f1` / `0a5a` | `0305` / `0a5a` |

Neither has a VCOM record (`0x23`). VCOM comes from 0x2035. Both NVRAMs are in sector 0.

## What the stock firmware checks

The JA00013753B image accepts only `03 02` and the JA10023278B image only `04 02`. JA10023278B:
- checks the magic and the version;
- takes the table positions from the header;
- walks the temperature table up to its entry count;
- rejects an index map row of `ff` or one past the row count;
- skips pool index `ff`, but doesn't check other pool indexes against the LUT count;
- never checks the payload checksum before use.

The JA10023278B image is also the Chroma21's stock image. Details for all boards: [stock_firmware.md](stock_firmware.md#what-the-stock-firmware-checks).

## Compared with the OEPL driver

`Chroma_Tag_FW/board/chroma29/screen.c` compiles in **row 2 of JA00013753B** (20–29 °C) byte for byte: VCOM1 `pool[14]`, R21h `pool[16]`, R22h `pool[15]`, G2 `pool[17]`, VCOM2 and R1 `pool[18]`/`pool[20]`, R0 `pool[19]`. It also uses that row's PLL `0x29`, VDPS_LV `0x09` and the dump's VCM_DC `0x08`.

| | Stock | OEPL |
|---|---|---|
| Waveform | The row for the current temperature, from SPI flash | Row 2 of a `03 02` waveform at all temperatures |
| JA1…B tags | Their own `04 02` waveform, VCM_DC −1.0 V, extra passes below 20 °C | The same compiled-in `03 02` row, VCM_DC −0.8 V |

`screen_8151.c` (the `chroma29_8151` build, for JA1…C tags) drives the UC8151C, with 42-byte LUTs. Neither dump here is from such a tag.

## Open questions

1. 0x2021–0x202B, and confirmation of the colour code at 0x203E.
2. The argument of the PWR resend in passes 2 and later.
3. Whether the `03 02` index map's G2 entries are a factory error, and what the stock firmware of a `03 02` tag does with them.
4. Real refresh times. The times above are calculated.
5. Whether JA1…B tags look better with their own `04 02` waveform than with the compiled-in `03 02` row. Not tested.
6. The waveform of a JA1…C (UC8151C) tag: its format, and whether it has temperature tables.
