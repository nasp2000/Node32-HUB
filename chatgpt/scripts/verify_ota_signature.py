#!/usr/bin/env python3
"""verify_ota_signature.py — prove a release binary is OTA-signed with our key.

The device only accepts firmware whose last 260 bytes are `[4-byte chip magic]
[256-byte RSA-2048 signature]`, and it verifies that signature against
`src/ota_pubkey.h` (see `src/ota_security.cpp`). A build without the private key
is still flashable over USB but is rejected by OTA, so publishing it is a silent
failure — this script turns that into a hard error before the release goes out.

Usage:
  python chatgpt/scripts/verify_ota_signature.py chatgpt/build/.../x.bin
  python chatgpt/scripts/verify_ota_signature.py --zip <release>.zip
  python chatgpt/scripts/verify_ota_signature.py --zip <release>.zip --chip P4__

Exit codes: 0 = signed and valid, 1 = unsigned/invalid/wrong chip.
"""

import argparse
import os
import re
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
PUBKEY_H = os.path.join(PROJECT_ROOT, "chatgpt", "src", "ota_pubkey.h")

MAGIC_LEN = 4
SIG_LEN = 256
CHIP_MAGICS = {"S3__": b"S3__", "P4__": b"P4__"}
SKIP_BINS = {"bootloader.bin", "partitions.bin"}


def load_public_key():
    from cryptography.hazmat.primitives.serialization import load_der_public_key

    with open(PUBKEY_H, "r", encoding="utf-8") as f:
        text = f.read()
    m = re.search(r"OTA_PUBKEY_DER\[\]\s*PROGMEM\s*=\s*\{(.*?)\}", text, re.S)
    if not m:
        raise SystemExit(f"ERROR: cannot parse OTA_PUBKEY_DER in {PUBKEY_H}")
    der = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})", m.group(1)))
    return load_der_public_key(der)


def firmware_bytes(args):
    if args.zip:
        with zipfile.ZipFile(args.zip) as z:
            bins = [
                n for n in z.namelist()
                if n.lower().endswith(".bin")
                and os.path.basename(n) not in SKIP_BINS
            ]
            if len(bins) != 1:
                raise SystemExit(
                    f"ERROR: expected exactly one firmware .bin in {args.zip}, found {bins or 'none'}"
                )
            print(f">>> checking {bins[0]} inside {os.path.basename(args.zip)}")
            return z.read(bins[0])
    if not os.path.isfile(args.bin):
        raise SystemExit(f"ERROR: not a file: {args.bin}")
    with open(args.bin, "rb") as f:
        return f.read()


def main():
    ap = argparse.ArgumentParser(description="verify the OTA signature of a firmware binary")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("bin", nargs="?", help="firmware .bin to check")
    src.add_argument("--zip", default="", help="release .zip (checks the firmware inside)")
    ap.add_argument("--chip", default="", choices=sorted(CHIP_MAGICS), help="expected chip magic")
    args = ap.parse_args()

    data = firmware_bytes(args)
    if len(data) <= MAGIC_LEN + SIG_LEN:
        raise SystemExit("ERROR: file is too small to carry an OTA signature (unsigned)")

    magic = data[-(MAGIC_LEN + SIG_LEN):-SIG_LEN]
    signature = data[-SIG_LEN:]
    signed_payload = data[:-SIG_LEN]

    known = {v: k for k, v in CHIP_MAGICS.items()}
    if magic not in known:
        raise SystemExit(
            f"ERROR: no chip magic at the end of the binary (got {magic!r}) — firmware is NOT signed"
        )
    chip = known[magic]
    if args.chip and chip != args.chip:
        raise SystemExit(f"ERROR: binary is signed for {chip}, expected {args.chip}")

    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import padding

    try:
        load_public_key().verify(
            signature, signed_payload, padding.PKCS1v15(), hashes.SHA256()
        )
    except InvalidSignature:
        raise SystemExit(
            "ERROR: OTA signature does not match src/ota_pubkey.h — "
            "the device would reject this firmware"
        )

    print(f">>> OTA signature OK (chip={chip}, {len(data) / 1024:.1f} KB)")


if __name__ == "__main__":
    main()
