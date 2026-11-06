# spidump: dump a Chroma tag's SPI flash through the CC1110

A Chroma tag keeps its serial number, calibration and display waveforms in an external SPI flash, and a CC debugger can only read the CC1110's own 32 KB of flash. spidump is a small program that copies 28 KB of the SPI flash into that internal flash, so the debugger can read it back. It needs no clip or soldering on the SPI flash, only the debug connection used for flashing.

Do this before the first flash of OEPL if you want a backup: the factory data can't be recreated. Flashing the CC1110 doesn't touch the SPI flash, but the OEPL firmware writes parts of it, and resets the NVRAM if it can't read a valid one.

The tools are in `Chroma_Tag_FW/spidump/`.

## Quick start

Build the image once, on Linux or WSL with SDCC:

```
cd Chroma_Tag_FW/spidump
./build.sh
```

Then, with the tag on an [OpenEPaperLink Tag_Flasher](https://github.com/OpenEPaperLink/OpenEPaperLink/tree/master/Tag_Flasher) (Python with `pyserial`):

```
python chroma_spidump.py -p COM12 --restore -o my_tag
```

That leaves in `my_tag/`:

| File | Contents |
|---|---|
| `cc1110_backup.bin` | The CC1110 firmware, keep this |
| `spi_dump.bin` | The SPI flash |
| `readback_<address>.bin` | The raw 32 KB read back of each pass |

## Options

| Option | Meaning |
|---|---|
| `-p PORT` | The flasher's serial port |
| `--mode first` | Default. SPI flash 0x0000–0x6FFF: the factory NVRAM and waveforms. About 2.5 minutes. |
| `--mode full` | All of `--size`. About 1 minute per pass, plus the backup: 5 passes (about 7 minutes) for 128 KB, 37 passes (about 40 minutes) for 1 MB. |
| `--mode none` | No dump: back up the firmware, then write `--flash`. |
| `--size` | SPI flash size for `--mode full`: `128K` (default; Chroma21, Chroma29) or `1M` (Chroma60). |
| `--restore` | Write the backed up firmware back at the end, also after an error. |
| `--flash FILE` | Write FILE at the end instead, such as an OEPL `_full_` image. |
| `--reference FILE` | Stop before erasing anything if the backup differs from FILE. |
| `--wait S` | Seconds the dumper runs before it's read back. Default 3. |
| `--screen BOARD` | Show a test screen after pass 0, see below. |
| `--screen-wait S` | Seconds to wait for the screen pass. Default 45. |
| `-o DIR` | Output directory. |

Pass 0 holds everything a tag can't do without. On a stock Chroma21 the rest of the flash only holds the last screens the old system sent. It's worth a `full` dump only for an exact copy of the stock state.

`spidump_join.py` joins read backs by hand: `spidump_join.py [--size 1M] out.bin readback*.bin`.

## What it does

1. Reads the CC1110 flash twice. The two reads must match, and match `--reference` if given. Saves `cc1110_backup.bin`. Nothing is erased before this passes.
2. For each pass: patches the SPI flash address into the image, erases the chip, writes the image and verifies it, resets the tag so it runs, waits, then reads the 32 KB twice.
3. Joins the passes and looks for the factory NVRAM and waveforms.
4. Writes `--restore` or `--flash` and verifies it. After an error `--restore` still runs, and `--flash` runs if the chip was erased.

The exit code is 0 only if all of that worked.

A pass in the CC1110's flash:

| Address | Contents |
|---|---|
| 0x0000–0x0FEF | The dumper |
| 0x0FF0 | Tag: `SPIDUMP` + the 3 byte SPI flash address |
| 0x1000–0x7FFF | 28 KB of SPI flash from that address |

The copy loop runs from RAM at 0xF800, because it erases and writes flash. It shares its core with the OTA update applier (`soc/cc111x/flashcopy.h`).

## The test screen

`./build.sh --screen chroma21` builds `spidump_chroma21.bin`, and `--screen chroma21` uses it for pass 0. After the copy the panel shows "SPI DUMP DONE", the serial number read from the **dumped copy** of the NVRAM, the pass address in yellow, a corner marker at logical (0,0), and black, yellow and outlined white patches.

It tests a board's display code before the full OEPL firmware goes on the tag, using the same driver (`board/chroma21/uc8154.c`):

- Upright text: the scan direction and logical mapping are right.
- Negative image: the black and white pixel codes are swapped.
- No yellow: the second refresh isn't working.

`spidump/host_preview.c` renders the expected screen on a PC.

### Adding a board

| File | Contents |
|---|---|
| `spidump.c` | Generic dumper, calls `dumpScreenShow()` when done |
| `dumpscreen.c`, `.h` | Generic content, computed per pixel (no framebuffer) |
| `epd_<driver>.c` | Panel glue: stream `dsPixels8()` to the panel driver, refresh |
| `boards/<board>.h` | Geometry, logical mapping, `DS_DRIVER` |

1. Add `boards/<board>.h`. Copy the mapping from `board/<board>/drawing.c`.
2. Reuse the panel code rather than copying it. `build.sh` adds `board/<board>/<driver>.c` if it exists, so a board's driver has to work without the rest of the firmware, as `uc8154.c` does with its two hooks.
3. Run `./build.sh --screen <board>`. It fails if the code reaches the tag at 0x0FF0. The Chroma21 screen build ends at 0x0EA8.

## Debug log

`taglog.py` resets a tag and captures the OEPL firmware's log with time stamps, through the flasher:

```
python taglog.py -p COM12 -t 90 -o log.txt
```

Wire the tag's P1.6 (USART1 TX, 115200 baud) to the flasher's GPIO36, cable pin 5. The flasher's README labels that pin TXD, from the tag's side of the link.

## Limitations

**Tags**
- CC1110 tags only. The CC1310 Chroma21s (`MJ`, `LD` serials) need other tools.
- A tag whose CC1110 is debug locked can't be backed up. It reads as all 0xFF, and the run stops with "not answering" before erasing. Erasing it by other means loses the firmware.
- The SPI flash must take the standard `0x03` read and `0xAB` wake commands with 3 byte addresses.

**What has been tested**
- On hardware, only with Chroma21 JG10014233 (128 KB flash, S2 Mini Tag_Flasher, firmware 50). It's untested on any other Chroma model or flash size. The 1 MB mode has only run against the simulator in `test/spidump`.
- That hardware run used an earlier version of these sources, built with SDCC 4.2.0. The current sources build with SDCC 4.2.0 and 4.6.0 and pass the simulator tests, but they haven't run on a tag yet.
- The flow has not been run with the tag on batteries, only powered by the flasher.

**Accuracy**
- Each pass reads the SPI flash once. The two read backs check the debug link, not that SPI read. Nothing compares the dump with the flash a second time, so for certainty, compare two runs of pass 0.
- Two dumps of a tag that ran the stock firmware in between won't match exactly: it updates NVRAM records as it runs. `--restore` lets it run again.
- Waveform and NVRAM checks only look for the magic numbers. "Waveform found" doesn't prove the contents are intact. A missing waveform isn't an error, since other boards may not have one at 0x2000.

**Side effects**
- Every pass erases the whole CC1110, including its firmware. Without `--restore` or `--flash` the tag is left running the dumper, which stops after the copy, or draws the test screen once.
- A power loss during a run leaves the dumper or a partial firmware in the CC1110. Reflash `cc1110_backup.bin`. The SPI flash isn't written, so it's safe.
- CC1110 flash has limited erase cycles. A 1 MB dump uses 38, so avoid repeating it needlessly.
- The tool only reads the SPI flash, it has no way to write a dump back.

**The flasher**
- Its CC1110 support reports success whatever happens, and a dead link reads as all 0xFF. These tools verify by reading back, but a read failing in some other way isn't detected until a comparison fails.
- A blank SPI flash looks the same as a dumper that didn't run: all 0xFF, reported as "did not run".
- Everything runs in one serial session, because reopening an S2 Mini's port right after a write can hang its USB until it's replugged.
- `taglog.py --no-reset` has shown nothing in testing, even with the tag active. Use the default reset.

**The test screen**
- Chroma21 only. Porting needs a board header, plus a panel driver that builds outside the firmware.
- Shown on pass 0 only, because the script waits for it and other passes have no NVRAM.
- Always uses the waveform block for 20 °C, so it can look washed out at other temperatures.
- The refreshes take about 30 s. The script waits a fixed `--screen-wait`: reading back earlier resets the chip mid-refresh.
- It needs the factory waveform tables in SPI flash. Without them it silently skips the screen; the dump still works.

**Building**
- `spidump.bin` isn't committed. Windows users build it in WSL.
- The flasher protocol is duplicated from `OEPL-Flasher.py` in the OpenEPaperLink repo (`tagflasher.py`), because that's a script that can't be imported.
