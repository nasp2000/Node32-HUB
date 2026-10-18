"""
sign_firmware.py — Sign an ESP32 firmware binary with RSA 2048 + SHA-256.

Appends: [4-byte chip magic][256-byte RSA signature] to the firmware.

Usage:
    python scripts/sign_firmware.py --chip <S3__|P4__> <firmware.bin>

The chip magic prevents accidentally flashing S3 firmware on P4 and vice versa.
The private key (ota_private.pem) must exist in the project root.
"""

import os, sys, struct
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from cryptography.hazmat.primitives.serialization import load_pem_private_key

CHIP_MAGICS = {"S3__": b"S3__", "P4__": b"P4__"}

def main():
    if len(sys.argv) < 3:
        print("Usage: python scripts/sign_firmware.py --chip <S3__|P4__> <firmware.bin>")
        sys.exit(1)

    chip = None
    fw_path = None
    for i, arg in enumerate(sys.argv[1:]):
        if arg == "--chip" and i + 2 < len(sys.argv):
            chip = sys.argv[i + 2]
        elif arg.endswith(".bin"):
            fw_path = arg

    if chip not in CHIP_MAGICS:
        print(f"Error: chip must be one of {list(CHIP_MAGICS.keys())}")
        sys.exit(1)
    if not fw_path or not os.path.exists(fw_path):
        print(f"Error: firmware file not found: {fw_path}")
        sys.exit(1)

    project_dir = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
    priv_path = os.path.join(project_dir, "ota_private.pem")
    if not os.path.exists(priv_path):
        print(f"Error: private key not found at {priv_path}")
        print("Run scripts/ota_keygen.py first to generate a keypair.")
        sys.exit(1)

    with open(priv_path, "rb") as f:
        private_key = load_pem_private_key(f.read(), password=None)

    with open(fw_path, "rb") as f:
        fw_data = f.read()

    chip_magic = CHIP_MAGICS[chip]
    data_to_sign = fw_data + chip_magic

    signature = private_key.sign(
        data_to_sign,
        padding.PKCS1v15(),
        hashes.SHA256(),
    )

    if len(signature) != 256:
        print(f"Error: unexpected signature length {len(signature)} (expected 256)")
        sys.exit(1)

    signed = data_to_sign + signature

    out_path = fw_path.replace(".bin", "_signed.bin")
    with open(out_path, "wb") as f:
        f.write(signed)

    print(f"[SIGN] Signed firmware: {out_path}")
    print(f"[SIGN] Size: {len(fw_data)} bytes -> {len(signed)} bytes (+{len(signed)-len(fw_data)})")
    print(f"[SIGN] Chip: {chip}")
    print(f"[SIGN] Signature: {signature.hex()[:32]}...")

if __name__ == "__main__":
    main()
