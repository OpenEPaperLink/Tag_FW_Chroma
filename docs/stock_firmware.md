# Chroma stock firmware

Notes on the stock CC1110 firmware of the Chroma tags, from disassemblies of the stock images and from comparing them. The waveform formats themselves are described per board: [Chroma21](chroma21_waveform.md), [Chroma29](chroma29_waveform.md), [Chroma42](chroma42_waveform.md), [Chroma60](chroma60_waveform.md), [Chroma74](chroma74_waveform.md).

**Confidence:** ✅ confirmed (by the data or the disassembly), ⚠️ strong inference, ❓ unknown.

## Stock images

| Tag | Board | MD5 | Waveform format accepted | Version compare at | Analysis |
|---|---|---|---|---|---|
| JA00013753B | Chroma29 | `ee7b201f…` | `03 02` | 0x28ED | Version compare only |
| JA10023278B | Chroma29 | `afdd7022…` | `04 02` | 0x2990 | Disassembly |
| JG10014233 | Chroma21 | `afdd7022…` | `04 02` | 0x2990 | The same image as JA10023278B; see also [chroma21_waveform.md](chroma21_waveform.md) |
| JC00002947B | Chroma42 JC0 (dual UC8154) | `3710105e…` | `03 02` | 0x29F0 | Version compare only |
| JH10005098, JC10063658B | Chroma42 JH1 and JC1 (UC8176) | `cecb8889…` | `03 03` | 0x2637 | Disassembly |
| JL10420082B | Chroma74 BWR | `3a21ab46…` | `03 03` | 0x280D | Disassembly |
| JM10525366C | Chroma74 BWY | `65579dbf…` | `03 03` | 0x2812 | Version compare and pixel table only |
| JD and JJ Chroma60 | Chroma60 | `860a7a49…` | `03 03` | 0x2807 | [chroma60_waveform.md](chroma60_waveform.md) |

Several boards share one image:
- The **Chroma21** (212×104, yellow) and the **JA1…B Chroma29** (296×128, red) run the same stock image. It takes the panel size from NVRAM record `0x14` and everything panel-specific from the SPI waveform.
- The **JH1** (yellow) and **JC1** (red) Chroma42 run the same image.
- The **JD** (red) and **JJ** (yellow) Chroma60 run the same image, and also have the same waveform.

## What the stock firmware checks

The table family is based on the JA10023278B image. The script family is based on JH10005098 and JL10420082B, and on the Chroma60 analysis.

### Version

Every stock image accepts exactly one format version, the one it was shipped with (see the table above). The factory installs the firmware and the waveform as a matched pair, so a stock image only ever sees one version.

An OEPL build has to accept every version a board shipped with: `03 02` and `04 02` on the Chroma29, and `02 03` and `03 03` on the Chroma60.

### At refresh time

| Check | Table family (JA10023278B) | Script family (JH10005098, JL10420082B) |
|---|---|---|
| Magic | ✅ (0x28BE) | ✅ (JH1 0x2589, JL 0x275F) |
| Version | Exactly `04 02` (0x2988) | Exactly `03 03` (JH1 0x262F, JL 0x2805) |
| Payload length and checksum | **Not checked**: nothing reads 0x2025 or 0x2028 | **Not checked** |
| Where positions come from | The header: the size at 0x2027 and the count/offset triplets | The header: the size at 0x2027 and the section offsets at 0x202A |
| Temperature table | Walked up to the entry count at 0x202C (0x283D) | Walked until a threshold below −99, **with no count limit** (JH1 0x24E3, JL 0x26B9) |
| Index map row | `ff` rejected; checked against the row count at 0x202F (0x2A2D) | |
| LUT pool index | `ff` skipped; **not checked** against the LUT count at 0x2032, which is never read (0x29BD) | |
| Block offsets | | **Not checked**: used as read from the table |
| Records | Fixed 15-byte LUTs | Type byte and u16 length. Unknown types are skipped as 3 bytes, type `00` ends the block, and **no length is checked** against the register's size (JH1 0x1F04) |
| No usable waveform | Refuses to refresh | Refuses: status 3 (JH1 0x1CC7) |

Both families read every position from the header, and the script interpreter walks records by type and length. Beyond that, the stock firmware trusts the data.

For the `03 02` images (JA00013753B, JC00002947B), only the version compare has been found. It isn't known whether their lookup reads the map row from the temperature entry or assumes one row per entry; both `03 02` waveforms work either way.

### At download

The stock firmware can rewrite the waveform over the radio, with commands `0x32` (header) and `0x33` (data). In JH10005098:
- `0x32` (0x3CF9) rejects a header whose version isn't `03 03`.
- 0x26D3 erases the area and writes the magic and the header first. It then starts a stream seeded with the header's checksum byte.
- The stream (0x17D9) reports a bad checksum at the end of the transfer ✅.

The header is already in flash by then, so a transfer that fails its checksum would leave a waveform that passes the refresh-time checks ⚠️.

The JL10420082B handler for `0x32` (0x4027) calls the same version check. JA10023278B's (0x41C5) checks for `04 02` and uses the same stream code, at the same buffer and counter addresses, so it probably checks the checksum too ⚠️.

So the checksum is only ever checked when a waveform is downloaded, never before one is used.

## Open questions

1. How the `03 02` images (JA00013753B, JC00002947B) look up the index map row and the LUT pool.
2. Whether JA10023278B and JL10420082B check the checksum at download, as JH10005098 does.
3. The JM10525366C image beyond its version compare and pixel table.
4. The stock image of a `02 03` Chroma60 (JD00000725), which hasn't been dumped.
