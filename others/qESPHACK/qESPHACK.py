import os
import sys
import time
import requests
import serial.tools.list_ports
import subprocess

LOGO = r"""
       _____ ____  ____  _   _    _    ____ _  __
  __ _| ____/ ___||  _ \| | | |  / \  / ___| |/ /
 / _` |  _| \___ \| |_) | |_| | / _ \| |   | ' / 
| (_| | |___ ___) |  __/|  _  |/ ___ \ |___| . \ 
 \__, |_____|____/|_|   |_| |_/_/   \_\____|_|\_\
    |_|                                          
"""

print(LOGO)
time.sleep(0.8)

DATA_DIR = os.path.join(os.path.expanduser("~"), "Desktop")
os.makedirs(DATA_DIR, exist_ok=True)

ESPTool_NAME = "esptool.exe"
GITHUB_API = "https://api.github.com/repos/Teapot174/ESP-HACK/releases/latest"

FIRMWARES = {
    "1": {"name": "SH1106",  "keyword": "-sh"},
    "2": {"name": "SSD1306", "keyword": "-ssd"}
}

def find_port():
    for p in serial.tools.list_ports.comports():
        if p.vid is None or p.pid is None:
            continue
        vidpid = f"{p.vid:04X}:{p.pid:04X}".upper()
        manuf = (p.manufacturer or "").lower()
        descr = (p.description or "").lower()

        if (
            vidpid == "10C4:EA60" or
            vidpid == "1A86:7523" or
            vidpid == "1A86:55D4" or
            "silicon labs" in manuf or
            "ch340" in manuf or "ch341" in manuf or
            "ch340" in descr or "ch341" in descr
        ):
            return p.device
    return None

def get_firmware_url(keyword):
    try:
        r = requests.get(GITHUB_API, timeout=8)
        r.raise_for_status()
        for asset in r.json()["assets"]:
            name = asset["name"].lower()
            if name.endswith(".bin") and keyword in name:
                return asset["browser_download_url"], asset["name"]
    except:
        pass
    return None, None

def download(url, filename):
    path = os.path.join(DATA_DIR, filename)
    try:
        r = requests.get(url, stream=True, timeout=30)
        r.raise_for_status()
        with open(path, "wb") as f:
            for chunk in r.iter_content(32768):
                f.write(chunk)
        return path
    except:
        return None

def flash(bin_path, port):
    esptool = os.path.join(os.path.dirname(os.path.abspath(__file__)), ESPTool_NAME)

    if not os.path.isfile(esptool):
        print("esptool not found.")
        return

    cmd = [
        esptool,
        "--chip", "esp32",
        "--port", port,
        "--baud", "921600",
        "write-flash",
        "0x0",
        bin_path
    ]

    print("Flashing...")

    process = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        universal_newlines=True
    )

    while True:
        line = process.stdout.readline()
        if not line and process.poll() is not None:
            break
        if line:
            print(line, end='', flush=True)

    return_code = process.wait()

    if return_code == 0:
        print("\nSuccessfully.")
    else:
        print("\nError.")

def main():
    port = find_port()
    if not port:
        print("COM not found.")
        return

    print(f"ESP32 detected: {port}")
    print(" ")
    print("Select display:")
    print("1. SH1106")
    print("2. SSD1306")
    print(" ")
    choice = input("> ").strip()

    if choice not in FIRMWARES:
        print("Select 1 or 2.")
        return

    url, fname = get_firmware_url(FIRMWARES[choice]["keyword"])
    if not url:
        print("Error.")
        return

    print(f"Downloading {fname} ...")

    bin_path = download(url, fname)
    if not bin_path:
        print("Downloading error.")
        return

    flash(bin_path, port)

if __name__ == "__main__":
    main()
    input("Press enter to exit.")