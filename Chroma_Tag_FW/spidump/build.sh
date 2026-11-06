#!/bin/bash
# Build the spidump image.  Needs SDCC and binutils, run it on Linux or WSL.
#
# usage: build.sh [--screen <board>]
#   --screen <board>   add the screen for boards/<board>.h (see dumpscreen.h)
#
# Writes spidump.bin or spidump_<board>.bin.  Set SDCC to use another compiler.
set -e
cd "$(dirname "$0")"
SDCC=${SDCC:-sdcc}
FW=..
OUT=build
TAG_ADR=$(awk '/#define DUMP_TAG_ADR/ {print $3}' spidump.h)

BOARD=
if [ "$1" = "--screen" ]; then
   BOARD=$2
   [ -f boards/$BOARD.h ] || { echo "no boards/$BOARD.h"; exit 1; }
   DRIVER=$(awk '/#define DS_DRIVER/ {print $3}' boards/$BOARD.h)
   [ -f epd_$DRIVER.c ] || { echo "no epd_$DRIVER.c for $BOARD"; exit 1; }
fi

SRCS=(spidump.c $FW/soc/cc111x/clock.c)
DEFS=()
INCS=(-I. -I$FW/soc/cc111x -I$FW/board)
NAME=spidump
if [ -n "$BOARD" ]; then
   NAME=spidump_$BOARD
   SRCS+=(dumpscreen.c epd_$DRIVER.c)
   [ -f $FW/board/$BOARD/$DRIVER.c ] && SRCS+=($FW/board/$BOARD/$DRIVER.c)
   DEFS+=(-DDUMP_SCREEN "-DDS_BOARD_H=\"boards/$BOARD.h\"" -DUC8154_NO_DEFAULT_WAVEFORM)
   INCS+=(-I$FW/board/$BOARD -I$FW/common -I$FW/cpu/8051 -I$FW/../shared)
fi

rm -rf $OUT
mkdir -p $OUT
RELS=()
for s in "${SRCS[@]}"; do
   r=$OUT/$(basename ${s%.c}).rel
   $SDCC -c -mmcs51 --model-small --opt-code-size "${DEFS[@]}" "${INCS[@]}" $s -o $r
   RELS+=($r)
done
# the file with main() first
$SDCC -mmcs51 --model-small --code-size 0x1000 --xram-loc 0xf000 --xram-size 0x800 \
      --iram-size 0x100 "${RELS[@]}" -o $OUT/$NAME.ihx

# The code must end below the tag
python3 - $OUT/$NAME.ihx $TAG_ADR <<'EOF'
import sys
tag, top = int(sys.argv[2], 0), 0
for l in open(sys.argv[1]):
    n, a, t = int(l[1:3], 16), int(l[3:7], 16), int(l[7:9], 16)
    if t == 0 and n and a != tag:
        top = max(top, a + n)
print(f'code ends at 0x{top:04x}, tag at 0x{tag:04x}')
if top > tag:
    sys.exit('the code overlaps the tag')
EOF
objcopy -I ihex -O binary --gap-fill 0xff $OUT/$NAME.ihx $NAME.bin
rm -rf $OUT
ls -l $NAME.bin
