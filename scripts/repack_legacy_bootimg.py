#!/usr/bin/env python3
"""Rebuild a legacy Android boot image from a known-good base image.

This script is intentionally limited to pre-header_version boot images that
store a separate dt section in the legacy Samsung/Android layout:

  header page
  kernel
  ramdisk
  second stage (optional)
  dtb

It preserves the base ramdisk/cmdline/addresses and swaps in a newly built
kernel and dtb, which avoids recovery-side magiskboot repacking entirely.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path


BOOT_MAGIC = b"ANDROID!"
HEADER_FORMAT = "<8s10I16s512s32s1024s"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)


class BootImageError(Exception):
    """Raised when the base boot image is invalid or unsupported."""


def align(value: int, page_size: int) -> int:
    return ((value + page_size - 1) // page_size) * page_size


def decode_c_string(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("ascii", errors="ignore")


def extract_slice(blob: bytes, start: int, size: int, label: str) -> bytes:
    end = start + size
    if end > len(blob):
        raise BootImageError(f"{label} exceeds base boot image size")
    return blob[start:end]


def parse_base_boot(path: Path) -> dict[str, object]:
    image = path.read_bytes()
    if len(image) < HEADER_SIZE:
        raise BootImageError("base boot image is smaller than the legacy header")

    header = struct.unpack(HEADER_FORMAT, image[:HEADER_SIZE])
    magic = header[0]
    if magic != BOOT_MAGIC:
        raise BootImageError("base boot image does not start with ANDROID! magic")

    (
        kernel_size,
        kernel_addr,
        ramdisk_size,
        ramdisk_addr,
        second_size,
        second_addr,
        tags_addr,
        page_size,
        dt_size,
        unused,
    ) = header[1:11]
    name = header[11]
    cmdline = header[12]
    _image_id = header[13]
    extra_cmdline = header[14]

    if page_size < HEADER_SIZE:
        raise BootImageError(f"unsupported page size {page_size}")

    kernel_offset = page_size
    ramdisk_offset = kernel_offset + align(kernel_size, page_size)
    second_offset = ramdisk_offset + align(ramdisk_size, page_size)
    dt_offset = second_offset + align(second_size, page_size)

    kernel = extract_slice(image, kernel_offset, kernel_size, "kernel")
    ramdisk = extract_slice(image, ramdisk_offset, ramdisk_size, "ramdisk")
    second = extract_slice(image, second_offset, second_size, "second stage")
    dt = extract_slice(image, dt_offset, dt_size, "dtb")

    return {
        "path": path,
        "size": len(image),
        "page_size": page_size,
        "kernel_addr": kernel_addr,
        "ramdisk_addr": ramdisk_addr,
        "second_addr": second_addr,
        "tags_addr": tags_addr,
        "unused": unused,
        "name": name,
        "cmdline": cmdline,
        "extra_cmdline": extra_cmdline,
        "kernel": kernel,
        "ramdisk": ramdisk,
        "second": second,
        "dt": dt,
    }


def update_sha(sha: "hashlib._Hash", payload: bytes) -> None:
    sha.update(payload)
    sha.update(struct.pack("<I", len(payload)))


def build_image(base: dict[str, object], kernel: bytes, dt: bytes) -> bytes:
    ramdisk = base["ramdisk"]
    second = base["second"]
    page_size = base["page_size"]
    assert isinstance(ramdisk, bytes)
    assert isinstance(second, bytes)
    assert isinstance(page_size, int)

    sha = hashlib.sha1()
    update_sha(sha, kernel)
    update_sha(sha, ramdisk)
    if second:
        update_sha(sha, second)
    if dt:
        update_sha(sha, dt)
    image_id = sha.digest() + (b"\0" * 12)

    header = struct.pack(
        HEADER_FORMAT,
        BOOT_MAGIC,
        len(kernel),
        int(base["kernel_addr"]),
        len(ramdisk),
        int(base["ramdisk_addr"]),
        len(second),
        int(base["second_addr"]),
        int(base["tags_addr"]),
        page_size,
        len(dt),
        int(base["unused"]),
        base["name"],
        base["cmdline"],
        image_id,
        base["extra_cmdline"],
    )

    output = bytearray()
    output.extend(header)
    output.extend(b"\0" * (page_size - len(header)))

    for payload in (kernel, ramdisk, second, dt):
        if not payload:
            continue
        output.extend(payload)
        output.extend(b"\0" * (align(len(payload), page_size) - len(payload)))

    base_size = int(base["size"])
    if len(output) > base_size:
        raise BootImageError(
            f"rebuilt boot image ({len(output)} bytes) exceeds base partition dump "
            f"size ({base_size} bytes)"
        )

    # Pad back to the original partition dump length so direct dd writes fully
    # overwrite the existing boot partition contents on this device.
    output.extend(b"\0" * (base_size - len(output)))
    return bytes(output)


def inspect_base(base: dict[str, object]) -> str:
    kernel = base["kernel"]
    ramdisk = base["ramdisk"]
    second = base["second"]
    dt = base["dt"]
    return "\n".join(
        [
            f"base: {base['path']}",
            f"size: {base['size']}",
            f"page_size: {base['page_size']}",
            f"product_name: {decode_c_string(base['name'])}",
            f"cmdline: {decode_c_string(base['cmdline'])}",
            f"extra_cmdline: {decode_c_string(base['extra_cmdline'])}",
            f"kernel_size: {len(kernel)}",
            f"ramdisk_size: {len(ramdisk)}",
            f"second_size: {len(second)}",
            f"dt_size: {len(dt)}",
            f"kernel_addr: 0x{int(base['kernel_addr']):08x}",
            f"ramdisk_addr: 0x{int(base['ramdisk_addr']):08x}",
            f"second_addr: 0x{int(base['second_addr']):08x}",
            f"tags_addr: 0x{int(base['tags_addr']):08x}",
        ]
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Repack a legacy Samsung/Android boot image from a base dump."
    )
    parser.add_argument("--base-boot", required=True, help="Path to base boot.emmc.win or boot.img")
    parser.add_argument("--kernel", help="Path to replacement Image.gz")
    parser.add_argument("--dtb", help="Path to replacement dtb.img; defaults to the base dt section")
    parser.add_argument("--output", help="Path to write the rebuilt boot.img")
    parser.add_argument(
        "--inspect",
        action="store_true",
        help="Print parsed base image metadata and exit",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        base = parse_base_boot(Path(args.base_boot))
        if args.inspect:
            print(inspect_base(base))
            return 0

        if not args.kernel or not args.output:
            raise BootImageError("--kernel and --output are required unless --inspect is used")

        kernel = Path(args.kernel).read_bytes()
        dt = Path(args.dtb).read_bytes() if args.dtb else base["dt"]
        if not kernel:
            raise BootImageError("replacement kernel is empty")
        if not dt:
            raise BootImageError("replacement dtb is empty")

        rebuilt = build_image(base, kernel, dt)
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_bytes(rebuilt)

        print(
            f"rebuilt boot image: {output_path} "
            f"({len(rebuilt)} bytes, page_size={base['page_size']})"
        )
        return 0
    except (OSError, BootImageError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
