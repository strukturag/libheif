#!/usr/bin/env python3
"""Modify the NCLX colr box(es) in a HEIF/AVIF file.

The colr box in HEIF/AVIF carries CICP colour signalling
(colour_primaries, transfer_characteristics, matrix_coefficients,
full_range_flag). Some files carry incorrect values: for example, some
Sony HIF files tag YCbCr range as "limited" in the colr box while the
HEVC VUI and the actual pixel data are full-range (libheif issue #1770).
libheif emits a warning when the colr box and the bitstream signalling
disagree; this script lets you correct the colr box.

Modes (all NCLX colr boxes in the file are modified in either mode):

* Default (no override flags): invoke heif-dec on the input, extract the
  bitstream's colour signalling from libheif's mismatch warning, and
  copy those values into the colr box(es). If heif-dec reports no
  mismatch, the input is copied to the output unchanged so the script
  is safe to use in batch loops.

* Manual override: each -p / -t / -m / --full-range / --limited-range
  flag overwrites that one CICP field. Unspecified fields keep their
  existing colr-box values. heif-dec is not invoked in this mode.

The NCLX variant of the colr box has a fixed size, so the file is
modified at byte level without any re-offsetting.

Usage examples:
  # Inspect (no -o => report only)
  heif-modify-colr.py JAM07897.HIF

  # Auto-copy bitstream CICP into the colr box(es)
  heif-modify-colr.py JAM07897.HIF -o fixed.HIF

  # Manual override of one field (e.g. force full_range_flag = 1)
  heif-modify-colr.py JAM07897.HIF -o fixed.HIF --full-range

  # Manual override of all four CICP fields
  heif-modify-colr.py in.heif -o out.heif -p 1 -t 13 -m 5 --full-range
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def parse_box_header(data, offset, end):
    """Return (box_size, box_type, header_size) or None if malformed."""
    if offset + 8 > end:
        return None
    size = struct.unpack_from('>I', data, offset)[0]
    box_type = bytes(data[offset + 4:offset + 8])
    header_size = 8
    if size == 1:
        if offset + 16 > end:
            return None
        size = struct.unpack_from('>Q', data, offset + 8)[0]
        header_size = 16
    elif size == 0:
        size = end - offset
    if box_type == b'uuid':
        header_size += 16
    if size < header_size or offset + size > end:
        return None
    return size, box_type, header_size


def find_child_box(data, start, end, target_type):
    """Find the first direct child box of the given type in [start, end).
    Returns (payload_start, payload_end) or None."""
    offset = start
    while offset + 8 <= end:
        header = parse_box_header(data, offset, end)
        if header is None:
            return None
        size, btype, hsize = header
        if btype == target_type:
            return offset + hsize, offset + size
        offset += size
    return None


def find_all_child_boxes(data, start, end, target_type):
    """Find all direct child boxes of the given type in [start, end).
    Returns list of (payload_start, payload_end)."""
    found = []
    offset = start
    while offset + 8 <= end:
        header = parse_box_header(data, offset, end)
        if header is None:
            break
        size, btype, hsize = header
        if btype == target_type:
            found.append((offset + hsize, offset + size))
        offset += size
    return found


def find_colr_payloads(data):
    """Walk meta > iprp > ipco > colr* and return [(payload_start, payload_end), ...].
    meta is a FullBox so its 4-byte version+flags header is skipped."""
    meta = find_child_box(data, 0, len(data), b'meta')
    if meta is None:
        return []
    iprp = find_child_box(data, meta[0] + 4, meta[1], b'iprp')
    if iprp is None:
        return []
    ipco = find_child_box(data, iprp[0], iprp[1], b'ipco')
    if ipco is None:
        return []
    return find_all_child_boxes(data, ipco[0], ipco[1], b'colr')


def read_nclx(data, payload_start, payload_end):
    """Return (cp, tc, mc, full_range_flag) if this colr is NCLX, else None."""
    if payload_end - payload_start < 11:
        return None
    if bytes(data[payload_start:payload_start + 4]) != b'nclx':
        return None
    cp = struct.unpack_from('>H', data, payload_start + 4)[0]
    tc = struct.unpack_from('>H', data, payload_start + 6)[0]
    mc = struct.unpack_from('>H', data, payload_start + 8)[0]
    frf = (data[payload_start + 10] >> 7) & 1
    return cp, tc, mc, frf


def write_nclx(data, payload_start, cp=None, tc=None, mc=None, frf=None):
    if cp is not None:
        struct.pack_into('>H', data, payload_start + 4, cp)
    if tc is not None:
        struct.pack_into('>H', data, payload_start + 6, tc)
    if mc is not None:
        struct.pack_into('>H', data, payload_start + 8, mc)
    if frf is not None:
        b = data[payload_start + 10] & 0x7F
        data[payload_start + 10] = b | (0x80 if frf else 0x00)


# Matches libheif's mismatch warning. The bitstream values are in the
# second parenthesized triplet. Format:
#   "...bitstream signalling (CP/TC/MC/{full|limited})..."
_BITSTREAM_WARNING_RE = re.compile(
    r'bitstream signalling \((\d+)/(\d+)/(\d+)/(full|limited)\)'
)


def extract_bitstream_nclx(input_path, heif_dec):
    """Run heif-dec and parse the NCLX-mismatch warning. Returns (cp, tc, mc, frf)
    or None if no mismatch warning was found."""
    if heif_dec is None:
        heif_dec = shutil.which('heif-dec')
        if heif_dec is None:
            raise RuntimeError(
                'heif-dec not found in PATH. Pass --heif-dec PATH or add it to PATH.')
    with tempfile.TemporaryDirectory() as td:
        # Write JPEG, not PNG: heif-dec picks the encoder from the
        # extension, and PNG encoding adds noticeable time we don't need
        # since we only care about the warning on stderr.
        out = os.path.join(td, 'out.jpg')
        try:
            proc = subprocess.run(
                [heif_dec, str(input_path), out],
                capture_output=True, text=True, check=False,
            )
        except FileNotFoundError as e:
            raise RuntimeError(f'Failed to execute {heif_dec}: {e}')
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        raise RuntimeError(f'{heif_dec} exited with status {proc.returncode}')
    match = _BITSTREAM_WARNING_RE.search(proc.stderr)
    if not match:
        return None
    cp = int(match.group(1))
    tc = int(match.group(2))
    mc = int(match.group(3))
    frf = 1 if match.group(4) == 'full' else 0
    return cp, tc, mc, frf


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument('input', type=Path, help='Input HEIF/AVIF file')
    ap.add_argument('-o', '--output', type=Path,
                    help='Output file. If omitted, the script only reports '
                         'the current NCLX values and exits without modifying.')
    ap.add_argument('-p', '--colour-primaries', type=int, metavar='N')
    ap.add_argument('-t', '--transfer-characteristics', type=int, metavar='N')
    ap.add_argument('-m', '--matrix-coefficients', type=int, metavar='N')
    rg = ap.add_mutually_exclusive_group()
    rg.add_argument('--full-range', dest='full_range',
                    action='store_const', const=1,
                    help='Set full_range_flag = 1')
    rg.add_argument('--limited-range', dest='full_range',
                    action='store_const', const=0,
                    help='Set full_range_flag = 0')
    ap.add_argument('--heif-dec', dest='heif_dec', metavar='PATH', default=None,
                    help='Path to the heif-dec executable used in auto mode '
                         '(default: search PATH).')
    args = ap.parse_args()

    data = bytearray(args.input.read_bytes())
    colr_payloads = find_colr_payloads(data)
    print(f'Found {len(colr_payloads)} colr box(es) in {args.input}')

    nclx_payloads = []
    for pstart, pend in colr_payloads:
        box_offset = pstart - 8  # NCLX colr has an 8-byte standard header
        nclx = read_nclx(data, pstart, pend)
        if nclx is None:
            ctype = bytes(data[pstart:pstart + 4]).decode('ascii', errors='replace')
            print(f'  @ {box_offset:#x}: colour_type={ctype!r} (not nclx, skipped)')
            continue
        cp, tc, mc, frf = nclx
        print(f'  @ {box_offset:#x}: nclx cp={cp} tc={tc} mc={mc} full_range={frf}')
        nclx_payloads.append(pstart)

    if args.output is None:
        return 0

    manual_given = (args.colour_primaries is not None
                    or args.transfer_characteristics is not None
                    or args.matrix_coefficients is not None
                    or args.full_range is not None)

    if manual_given:
        cp_new, tc_new, mc_new, frf_new = (
            args.colour_primaries,
            args.transfer_characteristics,
            args.matrix_coefficients,
            args.full_range,
        )
    else:
        # Auto mode: pull CICP from the bitstream via heif-dec's warning.
        try:
            bs = extract_bitstream_nclx(args.input, args.heif_dec)
        except RuntimeError as e:
            print(f'error: {e}', file=sys.stderr)
            return 2
        if bs is None:
            # No mismatch reported. Copy the input through so batch jobs
            # always produce an output file regardless of whether a fix
            # was needed.
            print('No NCLX/VUI mismatch reported by heif-dec — '
                  'colr already matches the bitstream.')
            if args.output.resolve() == args.input.resolve():
                print('Input and output are the same file; left unchanged.')
            else:
                shutil.copyfile(args.input, args.output)
                print(f'Copied {args.input} -> {args.output} unchanged.')
            return 0
        cp_new, tc_new, mc_new, frf_new = bs
        print(f'Bitstream CICP from heif-dec: cp={cp_new} tc={tc_new} '
              f'mc={mc_new} full_range={frf_new}')

    if not nclx_payloads:
        print('No NCLX colr boxes found to modify.', file=sys.stderr)
        return 1

    for pstart in nclx_payloads:
        write_nclx(data, pstart, cp=cp_new, tc=tc_new, mc=mc_new, frf=frf_new)
        cp, tc, mc, frf = read_nclx(data, pstart, pstart + 11)
        print(f'  patched @ {pstart - 8:#x}: cp={cp} tc={tc} mc={mc} full_range={frf}')

    args.output.write_bytes(bytes(data))
    print(f'Wrote {args.output} ({len(nclx_payloads)} colr box(es) modified)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
