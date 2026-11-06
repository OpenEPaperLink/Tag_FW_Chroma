# Chroma21 factory waveform tables (format 04 02)

> The tables up to "Compared with the Chroma60" were worked out from the data alone. Where they disagree with [Confirmed from the stock firmware](#confirmed-from-the-stock-firmware) or [Confirmed on hardware](#confirmed-on-hardware), the later sections are right. `Chroma_Tag_FW/board/chroma21/uc8154.c` implements this.

Decoded from a 128 KB SPI flash from a JG (BWY) Chroma21 with a UC8154 controller. Register meanings come from the UltraChip UC8154 datasheet (version UC8154c_D0.6). Running `Chroma_Tag_FW/board/chroma21/decode_chroma21.py dump.bin [block]` prints everything below for any dump in this format, including the decoded frame rates and voltages.

Every byte from 0x2000 to 0x26B9 is accounted for, and the data ends exactly where the tables predict: 0x26BA is the first erased (`0xFF`) byte.

**Confidence:** ✅ confirmed (by the data's structure or the datasheet), ⚠️ strong inference, ❓ unknown.

## Layout

| Address | Size | Contents | |
|---|---|---|---|
| 0x2000 | 4 | Magic `ce fa ef be` (0xBEEFFACE, little endian) | ✅ |
| 0x2004 | 2 | Format version `04 02` | ✅ |
| 0x2006 | 27 | Name, NUL padded: `D18Y00010WY005_UC8154 21E4` | ✅ |
| 0x2021 | 4 | `00 00 00 00` | ❓ |
| 0x2025 | 4 | `85 06 31 8a`: likely a checksum. The Chroma60 header has a 32-bit value at the same offset. | ⚠️ |
| 0x2029 | 30 | Header parameters (see below) | partly |
| 0x2047 | 13 × 10 | Temperature table | ✅ |
| 0x20C9 | 13 × 12 | Index map: which pool LUTs each temperature block uses | ✅ |
| 0x2165 | 91 × 15 | LUT pool | ✅ |
| 0x26BA | | End; erased flash follows | ✅ |

The table positions follow from two counts in the header:
- Temperature table starts at 0x2047.
- Index map = 0x2047 + 13 × 10.
- LUT pool = 0x2047 + 13 × 22.
- End = pool + 91 × 15.

## Header parameters (0x2029–0x2046)

```
00 00 00 0d 12 00 0d 94 00 5b 30 01 0a 22 17 07 07 0f 03 00 32 02 d0 07 00 00 00 00 00 00
```

| Offset | Bytes | Meaning | |
|---|---|---|---|
| 0x202C | `0d` | Number of temperature entries (13). Repeated at 0x202F. | ✅ |
| 0x2032 | `5b` | Number of LUTs in the pool (91) | ✅ |
| 0x2035 | `0a` | Possibly **VCM_DC (R82h)**: `0x0a` = −1.0 V. See the VCOM note below. | ❓ |
| 0x2036 | `22` | **TCON (R60h)**: source-to-gate and gate-to-source non-overlap both 12 clocks (the datasheet default). The Chroma29 driver uses `0x22` too. | ✅ |
| 0x2037 | `17` | **VCOM and data interval (R50h)**: border driven normally, DDX = 1, interval 10 Hsync (the datasheet default). The Chroma29 driver uses `0x17` too. | ✅ |
| 0x2038 | `07 07 0f` | **Booster soft start (R06h)**: phases A and B at 10 ms soft start, strength 1, minimum off time 6.58 µs; phase C at strength 2, 6.58 µs. The Chroma29 driver uses the same values. | ✅ |
| 0x203B | `03 00` | Probably **power setting (R01h) bytes 1–2**: internal DC/DC for source and gate (`VDS_EN` = `VDG_EN` = 1), RVSHLS = 00; VGH = 20 V, VGL = −19.3 V. `03` and `00` are the datasheet defaults. | ⚠️ |
| 0x203D | `32 02` | — | ❓ |
| 0x203F | `d0 07` | 2000 (u16 LE), perhaps a busy timeout in ms | ❓ |
| others | `12`, `94`, `30 01` | — | ❓ |

## Temperature table (0x2047, 13 entries × 10 bytes)

Warmest first, coldest last. The thresholds are the same as the Chroma60's.

```
#   temp  b1  b2  b3  b4  b5  b6  b7  b8  b9     b1 PLL   b7 PLL   b4 as VDPS
0    25   39  00  00  02  80  10  29  00  00     137 Hz    98 Hz   2.8 V
1    20   39  00  01  03  80  10  29  00  00     137 Hz    98 Hz   3.0 V
2    16   29  00  02  03  80  10  29  00  00      98 Hz    98 Hz   3.0 V
3    12   29  00  03  05  80  10  29  00  00      98 Hz    98 Hz   3.4 V
4     8   21  00  04  06  81  14  21  00  00      78 Hz    78 Hz   3.6 V
5     7   11  00  05  07  82  27  11  00  00      39 Hz    39 Hz   3.8 V
6     6   11  00  06  08  83  27  11  00  00      39 Hz    39 Hz   4.0 V
7     5   11  00  07  09  80  27  11  00  00      39 Hz    39 Hz   4.2 V
8     4   11  00  08  0b  81  27  11  00  00      39 Hz    39 Hz   4.6 V
9     3   11  00  09  0d  82  27  11  00  00      39 Hz    39 Hz   5.0 V
10    2   11  00  0a  0f  83  27  11  00  00      39 Hz    39 Hz   5.4 V
11    1   11  00  0b  10  84  27  11  00  00      39 Hz    39 Hz   5.6 V
12   -5   11  00  0c  10  85  27  11  00  00      39 Hz    39 Hz   5.6 V
```

| Byte | Meaning | |
|---|---|---|
| 0 | Threshold in °C (signed): the entry applies at or above it | ✅ |
| 1 | **PLL (R30h)**, = `M << 3 \| N`. Every value is a valid datasheet setting, and the frame rate falls as it gets colder: `0x39` 137 Hz, `0x29` 98 Hz, `0x21` 78 Hz, `0x11` 39 Hz. (Datasheet default: `0x2A` = 50 Hz.) | ✅ |
| 7 | **A second PLL value**: same encoding, but it stays at 98 Hz where byte 1 is 137 Hz. The UC8154 has one PLL register, so the stock firmware may run the black/white and red stages as separate refreshes, or use one value for a different refresh mode. Which is which is unknown. | ⚠️ |
| 4 | Probably **VDPS_LV**, power setting (R01h) byte 3: the **red/yellow positive voltage**, 2.4 V + 0.2 V per step. It rises with cold, from 2.8 V to 5.6 V, as the Chroma60's red drive voltage does. The Chroma29 driver uses `0x09` (4.2 V). | ⚠️ |
| 2 | Always `00`. Possibly **VDNS_LV**, R01h byte 4 (red negative voltage, `00` = −2.4 V); the Chroma29 driver also sets VDNS to `0x00`. | ❓ |
| 3 | Block number 0–12 (matches the row) | ✅ |
| 5 | `0x80` + 0–5: the low part rises with cold but restarts at 5 °C (0, 0, 0, 0, 1, 2, 3, 0, 1, 2, 3, 4, 5). It doesn't match any register field. | ❓ |
| 6 | `0x10` → `0x14` → `0x27`. Read as TCON nibbles it would be 8/4, 8/20 and 12/32 clocks, but that's a guess. | ❓ |
| 8, 9 | Always 0 | |

## Index map (0x20C9, 13 rows × 12 bytes)

Row *n* belongs to temperature entry *n*: 8 one-byte slots, then 4 bytes that are always 0. A slot holds a pool index, or `ff` for "not loaded":

```
0:  00 04 01 ff 03 02 05 06 | 00 00 00 00
1:  07 0b 08 ff 0a 09 0c 0d | 00 00 00 00
...
12: 54 58 55 ff 57 56 59 5a | 00 00 00 00
```

Each block uses 7 consecutive pool entries (13 × 7 = 91), always in the same slot order.

### Which LUT register each slot loads

| Slot | Register | Evidence | |
|---|---|---|---|
| 0 | **0x20 VCOM1** | All levels `00` (VCM_DC); black/white-stage timing | ✅ |
| 1 | **0x25 VCOM2** | All levels `00`; red-stage timing | ✅ |
| 2 | **0x22** (LUTB) | Starts with VSH and ends with VSL in all 13 blocks. Drives pixels **white** on this panel. | ✅ |
| 3 | **0x23 G1**, not loaded | Always `ff`. The Chroma29 driver never loads 0x23 either. | ⚠️ |
| 4 | **0x24 G2** | The only one that drives in group 1. Ends like slot 2, so yellow pixels go to white first. | ✅ |
| 5 | **0x21** (LUTW) | Starts with VSL and ends with VSH in all 13 blocks. Drives pixels **black** on this panel. | ✅ |
| 6 | **0x26 R0** | Red-stage timing; drives the red/yellow voltages | ✅ |
| 7 | **0x27 R1** | Identical to slot 1 in every block. The Chroma29 driver's R1 is also identical to its VCOM2. | ✅ |

How it was worked out:
1. The UC8154 runs a black/white stage (VCOM1, W, B, G1, G2) and then a red stage (VCOM2, R0, R1); per the datasheet's panel setting, `KWR` = 0 runs both. LUTs in the same stage must share timing. In all 13 blocks the slots split by timing into **{0, 2, 4, 5}** and **{1, 6, 7}**.
2. The stock firmware's refresh code gives the register for each slot.
3. Effectively the slot order is the register order with 0x21 and 0x25 swapped: `20 25 22 23 24 21 26 27`.

**The register names are misleading.** The datasheet calls R21h the white LUT and R22h the black LUT, but with these waveforms, as with the Chroma29 driver's, R21h makes pixels black. Hardware settled this: sending black as `00` gave a negative image.

## LUT pool (0x2165, 91 × 15 bytes)

✅ Format from the datasheet (R20h–R27h). Each LUT is a 16-byte command: the register byte plus these 15 data bytes, made of 5 groups of `[phase A] [phase B] [repeat]`.
- Each phase byte is `level << 6 | frames` (frames 0–63). Repeat is the number of times the group repeats; an all-zero group is unused.
- Level meaning depends on the LUT:

| LUT | `00` | `01` | `10` | `11` |
|---|---|---|---|---|
| VCOM1 (R20h) | VCM_DC | VCM_DC + 15 V | VCM_DC − 15 V | floating |
| W, B, G1, G2 (R21h–R24h) | 0 V | VSH (+15 V) | VSL (−15 V) | floating |
| VCOM2 (R25h) | VCM_DC | VSH(red) + VCM_DC | VSL(red) + VCM_DC | floating |
| R0, R1 (R26h–R27h) | 0 V | VSH (red) | VSL (red) | floating |

Both VCOM LUTs use only level `00`, so VCOM stays at **VCM_DC** for the whole refresh. That makes the VCM_DC setting (R82h) important; see the VCOM note.

Example, block 0 (≥25 °C):

```
slot 0 VCOM1  17 17 02 20 20 02 08 08 32 3f 3f 06 04 02 20   0:23/0:23 x2  0:32/0:32 x2  0:8/0:8 x50  0:63/0:63 x6  0:4/0:2 x32
slot 1 VCOM2  28 28 03 05 3f 04 02 3f 08 14 14 01 00 00 00   0:40/0:40 x3  0:5/0:63 x4   0:2/0:63 x8  0:20/0:20 x1
slot 2 (0x22) 17 17 02 60 20 02 48 88 32 7f bf 06 04 82 20   0:23/0:23 x2  1:32/0:32 x2  1:8/2:8 x50  1:63/2:63 x6 0:4/2:2 x32
slot 4 G2     97 97 02 a0 20 02 48 88 32 7f bf 06 04 82 20   2:23/2:23 x2  2:32/0:32 x2  1:8/2:8 x50  1:63/2:63 x6 0:4/2:2 x32
slot 5 (0x21) 17 17 02 a0 a0 02 48 88 32 7f bf 06 44 02 20   0:23/0:23 x2  2:32/2:32 x2  1:8/2:8 x50  1:63/2:63 x6 1:4/0:2 x32
slot 6 R0     68 68 03 85 7f 04 82 7f 08 54 54 01 00 00 00   1:40/1:40 x3  2:5/1:63 x4   2:2/1:63 x8  1:20/1:20 x1
slot 7 R1     28 28 03 05 3f 04 02 3f 08 14 14 01 00 00 00   (same as slot 1)
```

66 of the 91 LUTs are distinct; the duplicates are mostly the VCOM2/R1 pairs.

## Driving the panel (from the datasheet)

- **Pixel coding.** CDI `0x17` sets DDX = 1, so in DTM1 (R10h, 2 bits per pixel) the datasheet maps `00` to LUTB, `01` to gray 1, `10` to gray 2 and `11` to LUTW. In DTM2 (R13h, 1 bit per pixel) `0` = red 0 and `1` = red 1. With G1 never loaded, only codes `00`, `10` and `11` are usable. See [Confirmed from the stock firmware](#confirmed-from-the-stock-firmware) for what they do on this panel.
- **Resolution.** The panel setting's built-in resolutions (94×230, 94×252, 128×296, 200×300) don't include 212×104, so set the resolution with TRES (R61h), which overrides it.

## Factory NVRAM (same record format as the other Chroma boards)

In this dump the NVRAM is in **sector 1** (0x1000), and sector 0 is erased. The stock firmware moves the NVRAM between sectors 0 and 1.

| Type | Value | Meaning |
|---|---|---|
| `0x01` | `JG 10 01 48 96` | Tag serial **JG10014896** |
| `0x05` | `d5` + `54Q1029235815-Y005` | Panel serial number (as printed on the back of the panel) |
| `0x14` | `d4 00 68 00 00 06` | Resolution **212 × 104** |
| `0x09` / `0x12` | `02e3` / `0a5a` | Temperature sensor calibration (intercept / slope) |
| `0x02`, `0x03`, `0x06`, `0x0a`, `0x0b`, `0x0c`, `0x0f`, `0x13`, `0x15`, `0x16`, `0x18`, `0x20` | | Stock-firmware settings and state; unknown |

## VCOM

**There's no VCOM record (`0x23`)** in the factory NVRAM, unlike the Chroma60/74. VCOM comes from header byte 0x2035 (`0a`, −1.0 V), see below. It's held at VCM_DC throughout the refresh.

## Compared with the Chroma60 (format 03 03)

| | Chroma21 (`04 02`) | Chroma60 (`03 03`) |
|---|---|---|
| Structure | Header + temperature table + index map + shared LUT pool | Command script: init, refresh and VCOM-measure sequences, a temperature table, and one complete command block per temperature |
| Temperature thresholds | 25, 20, 16, 12, 8, 7, 6, 5, 4, 3, 2, 1, −5 | same |
| Per-temperature settings | PLL, red drive voltage, LUT set | Power setting (red voltage), PLL, LUTs |
| Controller | UC8154, 15-byte LUTs | UC8159, 220/260-byte LUTs |
| Panel specific? | **Yes.** The name contains `Y005`, which is also the panel serial's suffix. | No. A BWR (JD) and a BWY (JJ) tag have identical scripts named "P17D01.06 V3 UC8159 60E4". |

Because the name ties the waveform to the panel type (`Y005`, a yellow panel, matching `JG` = BWY), **a JB (BWR) Chroma21 will probably have different LUTs.** Compare against a JB dump before assuming one waveform fits both colours.

## Confirmed from the stock firmware

A disassembly of the stock CC1110 firmware (made with `Chroma_Tag_FW/tools/dis8051.py`) answers most of the questions above. The EPD routines are at 0x1f88 (power on/off), 0x2061/0x2074 (select/deselect), 0x2087/0x208d (command/data byte), 0x20f0 (send a 15-byte LUT), 0x2175 (PSR + TRES), 0x21b4 (PWR), 0x2313 (power on/off sequence), 0x23df/0x2470 (image data) and 0x2606 (refresh).

**The RAM copy.** The stock firmware builds one structure in RAM (pointer at XRAM 0xF45F):

| Offset | Source | Use |
|---|---|---|
| +0x00 | header 0x2035 | **VCM_DC (R82h)**, sent before every refresh. So VCOM is `0x0a` = −1.0 V on this panel. |
| +0x01 | 0x2036 | TCON (R60h) |
| +0x02 | 0x2037 | CDI (R50h) |
| +0x03..05 | 0x2038 | BTST (R06h) |
| +0x06..07 | 0x203B | PWR (R01h) bytes 1 and 2 (`03 00`) |
| +0x08 | 0x203D | `0x32` × 100 ms = 5 s: delay before the EPD is powered on again |
| +0x0A..0B | 0x203F | `0x07d0` = 2000 ms: delay before power off (see below) |
| +0x12..1B | temperature entry | the entry for the current temperature |
| +0x1C | 7 × 15 bytes | the entry's LUTs, in map slot order 0, 1, 2, 4, 5, 6, 7 |

**Temperature entry bytes**, corrected:

| Byte | Meaning |
|---|---|
| 1 | PLL for the **first** (B/W) refresh |
| 4 | **VDPS_LV**, PWR byte 3. Bit 7 set would mean VSH = 15 V (sets RVSHLS[1] in PWR byte 1) |
| 5 | **VDNS_LV**, PWR byte 4 (low 5 bits). Bit 7 sets RVSHLS[0] (VSL = −15 V) in PWR byte 1, so PWR = `07 00 VDPS VDNS` |
| 6 | Delay in ms after the second refresh starts before the red/yellow voltages are applied |
| 7 | PLL for the **second** (yellow) refresh |

**LUT registers.** The refresh code loads 0x20 ← slot 0, 0x21 ← slot 5, 0x22 ← slot 2, 0x24 ← slot 4, 0x25 ← slot 1, 0x26 ← slot 6, 0x27 ← slot 7. This matches the table above.

**Pixel coding.** The stock image has two planes, A = "not black" and B = "not yellow". DTM1 gets `11` for black, `00` for white and `10` (gray 2, LUTG2) for **yellow**. DTM2 is plane B as is, so `0` (Red0, LUTR0) marks yellow. A yellow pixel is driven to white in the first refresh, then to yellow in the second.

**Refresh sequence:**
1. Power on: P0.6 low, wait 20 ms, then nCS, RESET, P0.0 and D/C high. Reset pulse (200 µs low, then 10 ms), PWR, BTST, PON, wait for BUSY.
2. PSR `0x93` (KWR = 1, B/W only), TRES `68 00 d4`.
3. DTM1, then DTM2.
4. PLL = byte 1, CDI, VCM_DC, TCON, LUTs 0x20/0x21/0x22/0x24, then DRF. Wait for BUSY.
5. If the image has colour:
   - load **all-zero** LUTs into 0x20/0x21/0x22/0x24, and the real ones into 0x25/0x26/0x27
   - PSR `0x83` (KWR = 0), PWR `02 00 00 00`, PLL = byte 7, then DRF
   - wait byte 6 ms, then send the real PWR. Wait for BUSY.
6. Power off: VCM_DC = 0, PWR `02 00 00 00`, wait 2000 ms (0x203F), CDI `0x37` (border floating), POF, wait 20 ms. Then P0.6 high and all EPD pins low.

**Pins** (stock firmware): EPD SPI on USART0 alt 2 (P1.3 SCK, P1.5 SDIN); SPI flash on USART1 alt 1 (P0.3–P0.5) with nCS on P2.0; BUSY (active low) P1.0, nCS P1.1, RESET P1.2, D/C P0.7, nENABLE P0.6. P0.0 is driven low together with nCS for each transfer. The pinout is the same as the Chroma29's.

## Open questions

1. The remaining header bytes: `12`, `94`, `30 01`, and `02` at 0x203E.
2. Whether 0x2025 is a checksum, and over which range.
## Confirmed on hardware

On JG10014233: firmware screens and an image from an AP show upright with correct black, white and yellow, using the sequence above, with two differences from the stock firmware:

- **Scan direction:** PSR `0x8f`/`0x9f` (UD = SHL = 1, the same as the Chroma29), not the stock `0x83`/`0x93`, which shows OEPL's layout upside down.
- **Power on:** the stock wait of 0x203D × 100 ms before powering the EPD isn't used.

The temperature entry for 25 °C and above is the only one run on the panel so far.
