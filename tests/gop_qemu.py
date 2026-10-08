#!/usr/bin/env python3
"""Build clean sources and check GOP with QEMU/OVMF. Use only the standard library."""

import argparse
import json
import os
from pathlib import Path
import re
import select
import shutil
import subprocess
import tempfile
import time


REPO = Path(__file__).resolve().parents[1]
SOURCES = (
    "Makefile", "linker.ld", "uefi.hpp", "runtime.hpp",
    "startup.cpp", "runtime.cpp", "main.cpp",
)
COLORS = (b"\xff\x00\x00", b"\x00\xff\x00", b"\x00\x00\xff")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


class Qemu:
    def __init__(self, directory, build, args, no_vga=False):
        self.directory = directory
        directory.mkdir()
        shutil.copytree(build / "esp", directory / "esp")
        shutil.copyfile(args.ovmf_vars, directory / "vars.fd")
        self.serial = directory / "serial.log"
        self.errors = (directory / "qemu.log").open("w")
        command = [
            "qemu-system-x86_64", "-accel", "tcg", "-m", "256M",
            "-drive", f"if=pflash,format=raw,readonly=on,file={args.ovmf_code}",
            "-drive", f"if=pflash,format=raw,file={directory}/vars.fd",
            "-drive", f"format=raw,file=fat:rw:{directory}/esp",
            "-display", "none", "-serial", f"file:{self.serial}",
            "-qmp", "stdio", "-net", "none", "-no-reboot",
        ]
        if no_vga:
            command.extend(["-vga", "none"])
        try:
            self.process = subprocess.Popen(
                command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=self.errors, bufsize=0,
            )
        except BaseException:
            self.errors.close()
            raise
        self.buffer = b""
        self.sequence = 0

    def receive(self, deadline):
        while b"\n" not in self.buffer:
            remaining = deadline - time.monotonic()
            require(remaining > 0, "QMP response timeout")
            ready = select.select([self.process.stdout], [], [], remaining)[0]
            require(ready, "QMP response timeout")
            data = os.read(self.process.stdout.fileno(), 65536)
            require(data, f"QEMU closed QMP. See {self.directory / 'qemu.log'}")
            self.buffer += data
        line, self.buffer = self.buffer.split(b"\n", 1)
        return json.loads(line)

    def command(self, name, arguments=None):
        self.sequence += 1
        request = {"execute": name, "id": self.sequence}
        if arguments is not None:
            request["arguments"] = arguments
        self.process.stdin.write((json.dumps(request) + "\n").encode())
        self.process.stdin.flush()
        deadline = time.monotonic() + 10
        while True:
            reply = self.receive(deadline)
            if reply.get("id") == self.sequence:
                require("error" not in reply, f"QMP {name}: {reply}")
                return reply["return"]

    def start(self):
        require("QMP" in self.receive(time.monotonic() + 10), "QMP greeting missing")
        self.command("qmp_capabilities")

    def text(self):
        data = self.serial.read_text(errors="replace") if self.serial.exists() else ""
        for failure in ("[FAIL]", "!!!!", "Exception Type"):
            require(failure not in data, f"Firmware failure: {failure}")
        return data

    def wait_for(self, predicate, timeout, description):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            data = self.text()
            if predicate(data):
                return data
            require(self.process.poll() is None, "QEMU exited before " + description)
            time.sleep(0.1)
        raise RuntimeError(f"Timeout waiting for {description}. See {self.serial}")

    def screenshot(self, name):
        path = self.directory / name
        self.command("screendump", {"filename": str(path)})
        return read_ppm(path)

    def close(self):
        try:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=5)
        finally:
            self.process.stdin.close()
            self.process.stdout.close()
            self.errors.close()


def read_ppm(path):
    with path.open("rb") as stream:
        require(stream.readline().strip() == b"P6", "Expected a binary PPM screenshot")
        dimensions = stream.readline()
        while dimensions.startswith(b"#"):
            dimensions = stream.readline()
        width, height = map(int, dimensions.split())
        require(stream.readline().strip() == b"255", "Expected 8-bit PPM channels")
        pixels = stream.read()
    require(width > 0 and height > 0, "Invalid screenshot dimensions")
    require(len(pixels) == width * height * 3, "Incomplete PPM screenshot")
    return width, height, pixels


def check_blocks(screen):
    """Find each solid rectangle from pixels without using the application's geometry."""
    width, height, pixels = screen
    bounds = [[width, height, -1, -1, 0] for _ in COLORS]
    indices = {color: i for i, color in enumerate(COLORS)}
    for y in range(height // 2, height):
        for x in range(width):
            offset = (y * width + x) * 3
            index = indices.get(pixels[offset:offset + 3])
            if index is not None:
                box = bounds[index]
                box[0] = min(box[0], x)
                box[1] = min(box[1], y)
                box[2] = max(box[2], x)
                box[3] = max(box[3], y)
                box[4] += 1
    for name, box in zip(("red", "green", "blue"), bounds):
        left, top, right, bottom, count = box
        require(count > 0, f"Missing {name} block")
        require(count == (right - left + 1) * (bottom - top + 1),
                f"The {name} block is not a solid rectangle")
        require(right > left and bottom > top, f"The {name} block is too small")
        require(top > height // 2 and bottom < height - 1,
                f"The {name} block touches the lower-half boundary")
    require(bounds[0][2] < bounds[1][0] and bounds[1][2] < bounds[2][0],
            "Expected separate red, green, blue blocks from left to right")
    require(len({(b[1], b[3], b[2] - b[0]) for b in bounds}) == 1,
            "The blocks have different dimensions or vertical positions")
    return bounds


def firmware_menu(data):
    return "Device Manager" in data and "Boot Maintenance Manager" in data


def positive(directory, build, args):
    vm = Qemu(directory, build, args)
    try:
        vm.start()
        before = vm.wait_for(lambda data: "[GOP] DRAW_OK" in data, args.timeout, "DRAW_OK")
        for message in ("[OK] Global constructor executed", "Hello UEFI", "[GOP] FOUND",
                        "Press any key to exit..."):
            require(message in before, f"Missing application message: {message}")
        require("[GOP] ERROR" not in before, "GOP reported an error")
        resolution = re.search(r"\[GOP\] RESOLUTION (\d+)x(\d+)", before)
        require(resolution is not None, "Resolution message missing")
        screen = vm.screenshot("before-key.ppm")
        require(screen[:2] == tuple(map(int, resolution.groups())),
                "Reported resolution differs from the screenshot")
        boxes = check_blocks(screen)
        time.sleep(1)
        stable = vm.screenshot("waiting.ppm")
        require(check_blocks(stable) == boxes, "The blocks changed before key input")
        require(vm.text() == before, "Unexpected console output before key input")

        # Observe firmware output that starts after the application marker.
        vm.command("human-monitor-command", {"command-line": "sendkey spc"})
        vm.wait_for(lambda data: firmware_menu(data[len(before):]), args.timeout, "firmware menu")
        time.sleep(1)
        after = vm.screenshot("after-key.ppm")
        require(after != stable, "The screen did not change after key input")
        # OVMF can select a different resolution for its firmware menu.
        if after[:2] == stable[:2]:
            for color, (left, top, right, bottom, _) in zip(COLORS, boxes):
                x, y = (left + right) // 2, (top + bottom) // 2
                offset = (y * after[0] + x) * 3
                require(after[2][offset:offset + 3] != color, "A block remains after firmware return")
        print(f"PASS: {screen[0]}x{screen[1]}, RGB rectangles, stable wait, keyboard return", flush=True)
    finally:
        vm.close()


def negative(directory, build, args):
    vm = Qemu(directory, build, args, no_vga=True)
    try:
        vm.start()
        data = vm.wait_for(lambda text: "[GOP] ERROR" in text or "[GOP] FOUND" in text,
                           args.timeout, "GOP result without VGA")
        require("[GOP] FOUND" not in data,
                "This OVMF configuration still provides GOP with -vga none. "
                "Use tests/gop_host.cpp to inject EFI_NOT_FOUND. This QEMU case did not pass.")
        marker = "[GOP] ERROR LocateProtocol status=0x800000000000000E"
        require(marker in data, "Expected EFI_NOT_FOUND from LocateProtocol")
        for message in ("[OK] Global constructor executed", "Hello UEFI"):
            require(message in data, f"Missing application message: {message}")
        vm.wait_for(lambda text: "failed to start Boot" in text.split(marker, 1)[-1],
                    args.timeout, "firmware error return")
        require("[GOP] DRAW_OK" not in vm.text() and "Press any key to exit..." not in vm.text(),
                "The no-GOP path reached drawing or keyboard wait")
        print("PASS: -vga none, EFI_NOT_FOUND, immediate firmware return", flush=True)
    finally:
        vm.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=float, default=45, help="Boot deadline in seconds per case")
    parser.add_argument("--ovmf-code", type=Path, default=Path("/usr/share/OVMF/OVMF_CODE_4M.fd"))
    parser.add_argument("--ovmf-vars", type=Path, default=Path("/usr/share/OVMF/OVMF_VARS_4M.fd"))
    args = parser.parse_args()
    args.ovmf_code = args.ovmf_code.resolve()
    args.ovmf_vars = args.ovmf_vars.resolve()
    require(args.timeout > 0, "The timeout must be positive")
    require(args.ovmf_code.is_file() and args.ovmf_vars.is_file(), "OVMF firmware files missing")
    artifacts = Path(tempfile.mkdtemp(prefix="uefi-gop-qemu-", dir="/tmp"))
    print(f"Artifacts: {artifacts}", flush=True)
    build = artifacts / "build"
    build.mkdir()
    for name in SOURCES:
        shutil.copyfile(REPO / name, build / name)
    with (artifacts / "build.log").open("w") as log:
        subprocess.run(["make", "test", f"OVMF_CODE_SRC={args.ovmf_code}",
                        f"OVMF_VARS_SRC={args.ovmf_vars}"],
                       cwd=build, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=60)
    print("PASS: make test from clean sources", flush=True)
    positive(artifacts / "positive", build, args)
    negative(artifacts / "no-vga", build, args)


if __name__ == "__main__":
    main()
