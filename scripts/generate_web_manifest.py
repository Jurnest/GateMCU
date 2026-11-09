#!/usr/bin/env python3
"""Prepare the ESP Web Tools installer from an ESP8266 RTOS SDK build."""

import argparse
import json
import re
import shutil
import struct
from pathlib import Path


PARTITION_MAGIC = 0x50AA
PARTITION_END = 0xEBEB
APP_TYPE = 0x00
FACTORY_SUBTYPE = 0x00
PARTITION_ENTRY_SIZE = 32


def config_value(path: Path, key: str) -> str:
    pattern = re.compile(rf"^{re.escape(key)}=(.+)$")
    for line in path.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line.strip())
        if match:
            return match.group(1).strip().strip('"')
    raise ValueError(f"{key} is missing from {path}")


def factory_app_offset(partition_table: Path) -> int:
    data = partition_table.read_bytes()
    for position in range(0, len(data) - PARTITION_ENTRY_SIZE + 1, PARTITION_ENTRY_SIZE):
        magic = struct.unpack_from("<H", data, position)[0]
        if magic == PARTITION_END:
            break
        if magic != PARTITION_MAGIC:
            raise ValueError(f"Invalid partition table entry at 0x{position:x}")

        entry_type, subtype = struct.unpack_from("<BB", data, position + 2)
        offset = struct.unpack_from("<I", data, position + 4)[0]
        if entry_type == APP_TYPE and subtype == FACTORY_SUBTYPE:
            return offset

    raise ValueError(f"Factory application partition not found in {partition_table}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--sdkconfig", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    build_dir = args.build_dir
    bootloader = build_dir / "bootloader" / "bootloader.bin"
    app = build_dir / "GateMCU.bin"
    partition_tables = sorted(build_dir.glob("partitions_*.bin"))
    if len(partition_tables) != 1:
        raise ValueError(f"Expected one generated partition table, found {len(partition_tables)}")
    partition_table = partition_tables[0]

    for artifact in (bootloader, app, partition_table):
        if not artifact.is_file() or artifact.stat().st_size == 0:
            raise FileNotFoundError(f"Missing or empty build output: {artifact}")

    bootloader_offset = int(config_value(args.sdkconfig, "CONFIG_BOOTLOADER_STORE_OFFSET"), 0)
    partition_offset = int(config_value(args.sdkconfig, "CONFIG_PARTITION_TABLE_OFFSET"), 0)
    app_offset = factory_app_offset(partition_table)
    if app_offset <= partition_offset:
        raise ValueError("Factory application offset overlaps the partition table")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(bootloader, args.output_dir / "bootloader.bin")
    shutil.copyfile(partition_table, args.output_dir / "partitions.bin")
    shutil.copyfile(app, args.output_dir / "firmware.bin")

    manifest = {
        "name": "GateMCU",
        "version": args.version,
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": "ESP8266",
                "parts": [
                    {"path": "bootloader.bin", "offset": bootloader_offset},
                    {"path": "partitions.bin", "offset": partition_offset},
                    {"path": "firmware.bin", "offset": app_offset},
                ],
            }
        ],
    }
    (args.output_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
