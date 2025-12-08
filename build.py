import os
import subprocess
from pathlib import Path
import sys

Import("env")

def merge_binaries(source, target, env):
    # Get project directory and name
    project_dir = Path(env["PROJECT_DIR"])
    project_name = "ESP-HACK"
    merged_bin = project_dir / f"esp-hack_build.bin"

    # Paths to source files
    build_dir = Path(env.subst("$BUILD_DIR"))
    firmware_bin = build_dir / "firmware.bin"
    bootloader_bin = build_dir / "bootloader.bin"
    partitions_bin = build_dir / "partitions.bin"

    # Path to esptool.exe in project root
    esptool_exe = project_dir / "esptool.exe"

    # Check if esptool.exe exists
    if not esptool_exe.exists():
        print(f"❌ CRITICAL ERROR: esptool.exe not found in project root")
        print(f"   Place esptool.exe here: {project_dir}")
        sys.exit(1)

    # Check if source files exist
    missing_files = []
    if not firmware_bin.exists(): missing_files.append(str(firmware_bin))
    if not bootloader_bin.exists(): missing_files.append(str(bootloader_bin))
    if not partitions_bin.exists(): missing_files.append(str(partitions_bin))

    if missing_files:
        print(f"❌ Error: Missing files for merging:")
        for f in missing_files: print(f" - {f}")
        print("Ensure the build completed successfully")
        sys.exit(1)

    # Command for esptool.exe
    cmd = [
        str(esptool_exe),
        "--chip", "esp32",
        "merge_bin",
        "-o", str(merged_bin),
        "0x1000", str(bootloader_bin),
        "0x8000", str(partitions_bin),
        "0x10000", str(firmware_bin)
    ]

    print("🔨 Merging binary files:")
    print(f" - Bootloader:  {bootloader_bin} @ 0x1000")
    print(f" - Partitions:  {partitions_bin} @ 0x8000")
    print(f" - Firmware:    {firmware_bin} @ 0x10000")
    print(f"⚙️ Using: {esptool_exe}")

    try:
        # Execute command
        result = subprocess.run(
            cmd,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            shell=True
        )

        # Check output file size
        if merged_bin.exists():
            size = os.path.getsize(merged_bin) / 1024
            print(f"\n✅ Successfully created merged file: {merged_bin}")
            print(f"📏 File size: {size:.2f} KB")
            print(f"⚡ To flash, use: esptool.exe write_flash 0x0 \"{merged_bin}\"")
        else:
            print("❌ Error: Merged file was not created")
            if result.stderr:
                print("ℹ️ esptool error:")
                print(result.stderr)
            sys.exit(1)

    except subprocess.CalledProcessError as e:
        print(f"❌ Error running esptool.exe (code {e.returncode}):")
        print(e.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"❌ Unknown error: {str(e)}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

# Add script as post-action after build
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_binaries)