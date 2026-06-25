import os
import re
import subprocess
from pathlib import Path
import sys

Import("env")


def read_firmware_version(project_dir):
    config_path = project_dir / "src" / "CONFIG.h"
    try:
        config_text = config_path.read_text(encoding="utf-8")
    except OSError as e:
        print(f"ERROR: Unable to read firmware version from {config_path}: {e}")
        sys.exit(1)

    match = re.search(r'static\s+const\s+char\s*\*\s*FIRMWARE\s*=\s*"([^"]+)"', config_text)
    if not match:
        print(f'ERROR: FIRMWARE value was not found in {config_path}')
        sys.exit(1)

    return match.group(1)


def get_display_suffix(build_env):
    pio_env = build_env.get("PIOENV", "").lower()
    if "ssd1306" in pio_env:
        return "ssd"
    if "sh1106" in pio_env:
        return "sh"

    for define in build_env.get("CPPDEFINES", []):
        if isinstance(define, (list, tuple)) and len(define) >= 2 and define[0] == "DISPLAY_TYPE":
            if define[1] == "DISPLAY_SSD1306":
                return "ssd"
            if define[1] == "DISPLAY_SH1106":
                return "sh"

    print("ERROR: Unknown display environment. Use SH1106 or SSD1306.")
    sys.exit(1)


def merge_binaries(source, target, env):
    build_env = env
    project_dir = Path(build_env["PROJECT_DIR"])
    firmware_version = read_firmware_version(project_dir)
    display_suffix = get_display_suffix(build_env)
    merged_bin = project_dir / f"esp-hack_{firmware_version}-{display_suffix}.bin"

    build_dir = Path(build_env.subst("$BUILD_DIR"))
    firmware_bin = build_dir / "firmware.bin"
    bootloader_bin = build_dir / "bootloader.bin"
    partitions_bin = build_dir / "partitions.bin"

    esptool_exe = project_dir / "esptool.exe"
    if not esptool_exe.exists():
        print("ERROR: esptool.exe not found in project root")
        print(f"Place esptool.exe here: {project_dir}")
        sys.exit(1)

    missing_files = []
    if not firmware_bin.exists():
        missing_files.append(str(firmware_bin))
    if not bootloader_bin.exists():
        missing_files.append(str(bootloader_bin))
    if not partitions_bin.exists():
        missing_files.append(str(partitions_bin))

    if missing_files:
        print("ERROR: Missing files for merging:")
        for missing_file in missing_files:
            print(f" - {missing_file}")
        print("Ensure the build completed successfully")
        sys.exit(1)

    cmd = [
        str(esptool_exe),
        "--chip",
        "esp32",
        "merge_bin",
        "-o",
        str(merged_bin),
        "0x1000",
        str(bootloader_bin),
        "0x8000",
        str(partitions_bin),
        "0x10000",
        str(firmware_bin),
    ]

    print("Merging binary files:")
    print(f" - Bootloader:  {bootloader_bin} @ 0x1000")
    print(f" - Partitions:  {partitions_bin} @ 0x8000")
    print(f" - Firmware:    {firmware_bin} @ 0x10000")
    print(f" - Output:      {merged_bin}")
    print(f"Using: {esptool_exe}")

    try:
        result = subprocess.run(
            cmd,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            shell=True,
        )

        if merged_bin.exists():
            size = os.path.getsize(merged_bin) / 1024
            print(f"\nSuccessfully created merged file: {merged_bin}")
            print(f"File size: {size:.2f} KB")
            print(f'To flash, use: esptool.exe write_flash 0x0 "{merged_bin}"')
        else:
            print("ERROR: Merged file was not created")
            if result.stderr:
                print("esptool error:")
                print(result.stderr)
            sys.exit(1)

    except subprocess.CalledProcessError as e:
        print(f"ERROR: esptool.exe failed with code {e.returncode}:")
        print(e.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"ERROR: Unknown error: {str(e)}")
        import traceback

        traceback.print_exc()
        sys.exit(1)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_binaries)
