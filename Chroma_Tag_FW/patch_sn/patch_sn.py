#!/usr/bin/env python

import os
import argparse
import zlib
from intelhex import IntelHex

class SnException(Exception):
    pass

def patch_sn(file_path, sn):
    hex_file = False
    binary_file = False
    ota_file = False
    ota_offset = 0

    try:
        if file_path.lower().endswith('.hex'):
            file_type = "full hex"
            hex_file = True
            ih = IntelHex(file_path)
            bin = bytearray(ih.tobinarray())
        else:
            file_size = os.path.getsize(file_path)
            if file_size < 32768 or file_size > 32768 + 256:
                raise SnException()

            with open(file_path, "rb") as file:
                bin = bytearray(file.read())
                if file_size == 32768:
                    file_type = "full binary"
                    binary_file = True
                    pass
                else:
                    tag_type = bin[9:15].decode("utf-8")
                    if tag_type == 'chroma':
                        # ota file
                        if bin[8] == 1:
                            file_type = "OTA"
                            ota_offset = 25
                            ota_file = True
                        else:
                            print(f'Header version {bin[8]} is not supported')
                            raise SnException()

        SnOffset = (bin[32767 + ota_offset] << 8) + bin[32766 + ota_offset]
        SnOffset += ota_offset
        MagicOffset = SnOffset - 6
        NvMagic = bytearray([0x56, 0x12, 0x09, 0x85, 0x01, 0x08])
        SnOffset = bin.find(NvMagic)

        if SnOffset < 0:
            print(f'NVRAM magic not found')
            binary_file = False
            ota_file = False
            raise SnException()

        SnOffset += 6
        tag_type = bin[SnOffset:SnOffset+2].decode("utf-8")

        if sn is None:
            print(f'SN is {tag_type}{bin[SnOffset+2:SnOffset+6].hex()}')
            return

        sn = sn.upper()
        if sn[0:2] != tag_type:
            print(f'Invalid SN, first two letters much be "{tag_type}" for this tag type')
            raise SnException()

        new_sn = bytes.fromhex(sn[2:10])
        print(f'SN in {file_type} file changed from {tag_type}{bin[SnOffset+2:SnOffset+6].hex()} to {sn}')
        bin[SnOffset+2:SnOffset+6] = new_sn
        if hex_file:
            ih = IntelHex()
            ih.frombytes(bin)
            ih.write_hex_file(file_path)
        else:
            with open(file_path, "r+b") as file:
                file.seek(SnOffset+2)
                file.write(new_sn)
                if ota_file:
                    crc = zlib.crc32(bin[ota_offset:])
                    file.seek(0)
                    file.write(crc.to_bytes(4,'little'))
                file.close()

    except FileNotFoundError:
        print("Error: File not found.")
    except Exception as e:
        print(f"An error occurred: {e}")
    except SnException:
        pass

def sn_help():
    print('Invalid SN.')
    print('The SN must be 2 letters, 8 digits and one letter. For example "JM10339094B".')

def validate_arguments(args):
    result = True
    if args.SN is None:
        pass
    else:
        SN = args.SN.upper()
        sn_len = len(SN)
        if sn_len != 10 and sn_len != 11:
            result = False
        elif not SN[0:2].isalpha() or not SN[2:10].isdigit():
            result = False

    if not result:
        sn_help()

    return result

def main():
    parser = argparse.ArgumentParser(description="Patch serial number in a binary firmware file for a Chroma tag")
    parser.add_argument("filename", help="Binary file to patch (.bin or .hex)")
    parser.add_argument("SN", nargs='?', help="New Serial Number")

    args = parser.parse_args()

    if not validate_arguments(args):
        return
    
    patch_sn(args.filename, args.SN)

if __name__ == "__main__":
    main()
