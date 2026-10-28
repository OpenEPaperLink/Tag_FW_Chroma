# Packed-row G5 host tests

Run from the repository root on Linux or WSL with Python 3.8+ and a host C compiler:

```sh
python3 tests/packed_g5/run_tests.py
python3 tests/packed_g5/run_tests.py --sanitize
# Optional compiler selection:
HOST_CC=clang python3 tests/packed_g5/run_tests.py --sanitize
```

No packages, network access, sibling repositories, SDCC installation, or tag hardware are required. Temporary shared libraries and sanitizer executables are built outside the checkout. `HOST_CC` defaults to `cc`; the firmware's `CC=sdcc` setting is ignored.

The tests compile the **production** `Chroma_Tag_FW/common/g5.c` and `g5.h` with host declarations enabled. A single `ImageScratch` union backs both packed rows, decoder state, transfer state, and the 128-byte input buffer. The EEPROM mock checks every read and write, preserves source bytes and surrounding image-header sentinels, enforces row pitch and ordering, and emulates programming erased NOR. Unaligned payload addresses above 64 KiB exercise refill and page boundaries. Watchdog operations are empty, matching the current CC111x macros.

Coverage includes:

- Frozen AP encoder streams, with input pixels, exact source revision and file hashes in `fixtures/ap_streams.json`.
- All 87,380 pairs of rows for widths 1 through 8.
- Every vertical displacement, pass mode, and all four horizontal length combinations.
- Non-byte widths, exact-power-of-two widths, and 128/296/400/600/640-pixel rows, including alternating rows with more transitions than the legacy decoder allows.
- Full sequential two-plane images and the reference-row transition between planes.
- The six-byte image header, raw output size limits, invalid dimensions/plane counts, EOF, all selected truncated prefixes, sticky decoder errors, and malformed input.
- ASan/UBSan with exact-sized compressed-input/output allocations and 100,000 malformed streams when `--sanitize` is selected.

`reference.py` is an independent, deliberately small transition-list test encoder. It uses the G5 wire prefix map and fixed-length run fields; no third-party encoder implementation is vendored. Its output is checked byte-for-byte against the frozen AP streams. It clips transitions at the requested visible width, so it can generate valid non-byte-aligned test images even though the AP encoder's final padding-run handling can produce invalid endpoints for such widths.

The 600x448 fixtures exercise the Chroma60 geometry; they do not qualify its physical display hardware. Core tests deliberately include noisy compressed streams larger than a firmware image slot. Real transfer/storage policy must reject oversized downloads or use the AP's raw fallback.

These decoder tests check the public EEPROM read/write calls, including rows that cross physical NOR page boundaries. They do not execute slot allocation or the EEPROM driver's SPI transactions; the storage suite below covers those paths. Hardware timing and OTA concurrency still require separate verification.

## Storage and SPI regressions

Initialize the repository's `shared` protocol submodule, then run:

```sh
python3 tests/packed_g5/test_storage.py
# Optional compiler selection:
HOST_CC=clang python3 tests/packed_g5/test_storage.py
```

The thirteen tests compile the production G5 decoder together with eight storage functions extracted verbatim from `OEPL/syncedproto.c` and five EEPROM functions extracted from `common/eeprom.c`. Only the external-inline entry's linkage is adapted for the host compiler. The real shared protocol structures are used. Extraction and compilation happen in a temporary directory; firmware sources are never rewritten.

Coverage includes:

1. Public `eepromWrite()` completes programming before returning, with an unaligned 300-byte write spanning three NOR pages.
2. Raw downloads, the current-image shortcut and cached-image lookup.
3. G5 conversion to raw cache entries, separate source/output slots and compressed-version cache reuse.
4. Raw and G5 special preloads, including replacement after a successful commit.
5. Malformed G5 preserves the old custom/current images and sends no acknowledgement.
6. Source/output slot exhaustion preserves protected slots and sends no acknowledgement.
7. An interrupted G5 transfer resumes at the failed block while its image header remains invalid.
8. A new G5 slideshow preload preserves the existing slideshow images.
9. Chroma60 raw and G5 two-plane images retain their full 67,200-byte size.
10. Empty and oversized payloads do not erase image slots or receive an acknowledgement.
11. Repeated special/slideshow preloads acknowledge without fetching, erasing or programming, even with only one eligible free slot. Exact arguments, including LUT bits, select distinct requests; failed new versions preserve the old preload.
12. A cached committed special image finishes interrupted old-version cleanup without fetching or programming, and moves the current-slot reference to the replacement.
13. Interrupted special/slideshow preloads resume from their original source slot after an unrelated cached preload is acknowledged; no second source erase or duplicate transfer occurs.

The SPI model tracks NOR busy status, write enable and page-program transactions. It rejects reads while programming is pending. When the first valid-marker byte is programmed, the model requires a separate program operation starting at the marker address and verifies that version, size, type, ID and argument have already been programmed correctly. Successful download tests require this final marker commit. Publishing validity as part of the initial metadata write fails these assertions. Before a G5 replacement erases an old special image, the model also requires a valid, complete replacement header with the expected metadata.

The block-transfer mock supplies payload bytes and injects a block failure; it does not exercise radio packets or checksums. The SPI model and host compiler do not reproduce CC111x registers, 8051 stack depth, physical EEPROM timing, power interruption or display waveforms. Those remain hardware checks. Successful storage fixtures fit their compressed source slots; oversized core-decoder fixtures intentionally test a different boundary.
