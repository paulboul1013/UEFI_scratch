#!/usr/bin/env python3
"""Build clean menu sources, exercise real OVMF input, and retain screenshots."""

import argparse
import json
import os
from pathlib import Path
import re
import select
import shutil
import struct
import subprocess
import tempfile
import time
import zlib


REPO = Path(__file__).resolve().parents[1]
SOURCES = (
    "Makefile", "linker.ld", "uefi.hpp", "runtime.hpp", "startup.cpp",
    "runtime.cpp", "menu.hpp", "menu.cpp", "station.cpp", "boot.hpp", "boot.cpp",
    "main.cpp", "tests/menu_qemu_probe.cpp",
)
OPTIONS = (
    "Select Language", "Device Manager", "Boot Manager", "Boot Maintenance Manager",
    "Continue", "Reset", "Return to Firmware",
)
CSI = re.compile(r"\x1b\[([0-9;=?]*)([@-~])")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


class Terminal:
    """Decode the firmware's cursor-addressed ANSI stream, independently of menu.cpp."""

    def __init__(self, text):
        # A generous canvas avoids assuming that the active mode is 80x25.
        self.width = 256
        self.height = 128
        self.cells = [[" "] * self.width for _ in range(self.height)]
        self.row = 0
        self.column = 0
        self.clears = 0
        index = 0
        while index < len(text):
            character = text[index]
            if character == "\x1b":
                match = CSI.match(text, index)
                if match is None:
                    # Serial files can be read while a CSI sequence is incomplete.
                    break
                values, operation = match.groups()
                parameters = [int(value or 0) for value in values.lstrip("=?").split(";")]
                if operation in ("H", "f"):
                    self.row = max(0, (parameters[0] or 1) - 1)
                    self.column = max(0, (parameters[1] if len(parameters) > 1 else 1) - 1)
                elif operation == "J" and parameters[0] == 2:
                    self.cells = [[" "] * self.width for _ in range(self.height)]
                    self.clears += 1
                elif operation == "K":
                    if self.row < self.height:
                        start = 0 if parameters[0] in (1, 2) else self.column
                        end = self.width if parameters[0] in (0, 2) else self.column + 1
                        self.cells[self.row][start:end] = [" "] * (end - start)
                index = match.end()
                continue
            if character == "\r":
                self.column = 0
            elif character == "\n":
                self.row += 1
            elif character == "\b":
                self.column = max(0, self.column - 1)
            elif character >= " ":
                if self.row < self.height and self.column < self.width:
                    self.cells[self.row][self.column] = character
                self.column += 1
            index += 1

    @property
    def lines(self):
        lines = ["".join(row).rstrip() for row in self.cells]
        while lines and not lines[-1]:
            lines.pop()
        return lines

    @property
    def text(self):
        return "\n".join(self.lines)

    def selected(self, label):
        return any(re.match(r"^\s*> " + re.escape(label) + r"(?:\s|$)", line)
                   for line in self.lines)


class Qemu:
    def __init__(self, directory, esp, args, no_vga=False):
        self.directory = directory
        directory.mkdir()
        shutil.copytree(esp, directory / "esp")
        shutil.copyfile(args.ovmf_vars, directory / "vars.fd")
        self.serial = directory / "serial.log"
        self.events = []
        self.buffer = b""
        self.sequence = 0
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
        (directory / "command.json").write_text(json.dumps(command, indent=2) + "\n")
        try:
            self.process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                            stdout=subprocess.PIPE, stderr=self.errors,
                                            bufsize=0)
        except BaseException:
            self.errors.close()
            raise

    def receive(self, deadline):
        while b"\n" not in self.buffer:
            remaining = deadline - time.monotonic()
            require(remaining > 0, "QMP response timeout")
            ready = select.select([self.process.stdout], [], [], remaining)[0]
            require(ready, "QMP response timeout")
            data = os.read(self.process.stdout.fileno(), 65536)
            if not data:
                return None
            self.buffer += data
        line, self.buffer = self.buffer.split(b"\n", 1)
        reply = json.loads(line)
        if "event" in reply:
            self.events.append(reply)
        return reply

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
            require(reply is not None, f"QEMU closed QMP during {name}; see {self.directory}")
            if reply.get("id") == self.sequence:
                require("error" not in reply, f"QMP {name}: {reply}")
                return reply["return"]

    def start(self):
        greeting = self.receive(time.monotonic() + 10)
        require(greeting is not None and "QMP" in greeting, "QMP greeting missing")
        self.command("qmp_capabilities")

    def text(self):
        text = self.serial.read_bytes().decode(errors="replace") if self.serial.exists() else ""
        for failure in ("[FAIL]", "!!!!", "Exception Type", "[MENU_PROBE] RESTORE_FAILED"):
            require(failure not in text, f"Firmware failure: {failure}; see {self.serial}")
        return text

    def terminal(self):
        return Terminal(self.text())

    def wait_for(self, predicate, timeout, description):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.text()
            if predicate(text):
                return text
            require(self.process.poll() is None, "QEMU exited before " + description)
            time.sleep(0.05)
        raise RuntimeError(f"Timeout waiting for {description}; see {self.serial}")

    def wait_canvas(self, predicate, args, description):
        self.wait_for(lambda text: predicate(Terminal(text)), args.timeout, description)
        # Wait until the whole redraw has reached the serial file before comparing it.
        self.settle()
        canvas = self.terminal()
        require(predicate(canvas), "Final canvas differs after " + description)
        return canvas

    def settle(self):
        deadline = time.monotonic() + 3
        previous = self.text()
        stable_since = time.monotonic()
        while time.monotonic() < deadline:
            time.sleep(0.05)
            current = self.text()
            if current != previous:
                previous = current
                stable_since = time.monotonic()
            elif time.monotonic() - stable_since >= 0.25:
                return
        raise RuntimeError("Console did not finish its redraw")

    def key(self, name):
        self.command("human-monitor-command", {"command-line": "sendkey " + name})

    def screenshot(self, name):
        path = self.directory / (name + ".ppm")
        self.command("screendump", {"filename": str(path)})
        width, height, pixels = read_ppm(path)
        write_png(self.directory / (name + ".png"), width, height, pixels)
        require(width > 100 and height > 100, "Screenshot is too small to contain a menu")
        # Screen evidence must contain many dark and light pixels, not a blank frame.
        dark = sum(1 for index in range(0, len(pixels), 3)
                   if max(pixels[index:index + 3]) < 80)
        light = sum(1 for index in range(0, len(pixels), 3)
                    if min(pixels[index:index + 3]) > 128)
        require(dark > 1000 and light > 1000, "Screenshot lacks visible text/contrast")
        return width, height, pixels

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
            (self.directory / "qmp-events.json").write_text(
                json.dumps(self.events, indent=2) + "\n")
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
    require(len(pixels) == width * height * 3, "Incomplete PPM screenshot")
    return width, height, pixels


def write_png(path, width, height, pixels):
    """Convert the unmodified screenshot to PNG with only the standard library."""
    def chunk(name, value):
        return (struct.pack(">I", len(value)) + name + value
                + struct.pack(">I", zlib.crc32(name + value) & 0xffffffff))
    stride = width * 3
    scanlines = b"".join(b"\x00" + pixels[offset:offset + stride]
                         for offset in range(0, len(pixels), stride))
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b""))


def front(canvas, selected=0):
    return ("UEFI Station 0.2" in canvas.text
            and all(option in canvas.text for option in OPTIONS)
            and canvas.selected(OPTIONS[selected]))


def wait_front(vm, args, selected=0):
    return vm.wait_canvas(lambda canvas: front(canvas, selected), args,
                          "front page with " + OPTIONS[selected])


def unchanged(vm, key, description):
    vm.settle()
    before = vm.text()
    vm.key(key)
    time.sleep(0.35)
    vm.settle()
    require(vm.text() == before, description + " caused an unnecessary redraw")


def firmware_return(vm, args, offset):
    text = vm.wait_for(lambda text: "Device Manager" in text[offset:]
                       and "Boot Maintenance Manager" in text[offset:],
                       args.timeout, "firmware menu after application return")
    require("[MENU_PROBE] RESTORE_FAILED" not in text, "Guest observer failed")
    vm.settle()


def navigation_case(directory, esp, args):
    vm = Qemu(directory, esp, args)
    try:
        vm.start()
        initial = wait_front(vm, args)
        dimensions = re.search(r"Text:\s*(\d+)x(\d+)", initial.text)
        require(dimensions is not None, "Current text mode summary missing")
        require(int(dimensions[1]) >= 80 and int(dimensions[2]) >= 25,
                "This VM did not exercise the full layout")
        require("Firmware:" in initial.text and "UEFI:" in initial.text,
                "Firmware header is incomplete")
        initial_screen = vm.screenshot("main")
        graphics = re.search(r"Graphics:\s*(\d+)x(\d+)", initial.text)
        require(graphics is not None and tuple(map(int, graphics.groups())) == initial_screen[:2],
                "Reported graphics dimensions differ from the real QMP display")
        (directory / "main.txt").write_text(initial.text + "\n")
        unchanged(vm, "up", "Upper navigation boundary")
        unchanged(vm, "x", "Unknown key")
        unchanged(vm, "spc", "Space key")

        # Check every highlight twice. The same selection must yield identical text
        # and pixels after traversing all descriptions in both directions.
        reference = {0: initial.text}
        for index in range(1, len(OPTIONS)):
            vm.key("down")
            reference[index] = wait_front(vm, args, index).text
        unchanged(vm, "down", "Lower navigation boundary")
        for repetition in range(2):
            for index in range(len(OPTIONS) - 2, -1, -1):
                vm.key("up")
                require(wait_front(vm, args, index).text == reference[index],
                        "Residual description or shifted layout after Up")
            require(vm.screenshot(f"repeat-{repetition}") == initial_screen,
                    "Pixels changed after returning to the original selection")
            if repetition == 0:
                for index in range(1, len(OPTIONS)):
                    vm.key("down")
                    require(wait_front(vm, args, index).text == reference[index],
                            "Residual description or shifted layout after Down")

        pages = (
            ("Current language: English", "Traditional Chinese: Not available yet"),
            ("Firmware Vendor:", "Firmware Revision:", "UEFI Revision:", "Text Mode:",
             "Graphics Mode:", "Resolution:", "Pixel Format:"),
            ("GOP Demo", "Path: \\EFI\\TOOLS\\GOP.EFI", "Enter: Start"),
            ("Default Boot Entry: Not configured",),
            ("No default boot entry configured",),
        )
        for index, messages in enumerate(pages):
            if index != 0:
                vm.key("down")
                wait_front(vm, args, index)
            vm.key("ret")
            page = vm.wait_canvas(lambda canvas: all(text in canvas.text for text in messages),
                                  args, OPTIONS[index] + " information page")
            require(not front(page, index), "Enter failed to open the child page")
            (directory / f"page-{index}.txt").write_text(page.text + "\n")
            if index == 1:
                require(vm.screenshot("device")[:2] == initial_screen[:2],
                        "Entering Device Manager changed the graphics mode")
                require("Graphics unavailable" not in page.text,
                        "The VGA case did not expose valid graphics information")
                reported = re.search(r"Resolution:\s*(\d+)x(\d+)", page.text)
                require(reported is not None
                        and tuple(map(int, reported.groups())) == initial_screen[:2],
                        "Device Manager resolution differs from the real QMP display")
                require(f"Text Mode: {dimensions[1]}x{dimensions[2]}" in page.text,
                        "Device Manager text dimensions differ from the front page")
            unchanged(vm, "x", "Unknown key on child page")
            vm.key("esc")
            require(wait_front(vm, args, index).text == reference[index],
                    "Child Esc failed to preserve the main selection")

        vm.key("down")
        wait_front(vm, args, 5)
        vm.key("ret")
        reset = vm.wait_canvas(lambda canvas: "Restart the machine?" in canvas.text
                              and canvas.selected("Cancel"), args, "Reset defaults to Cancel")
        vm.screenshot("reset")
        (directory / "reset.txt").write_text(reset.text + "\n")
        vm.key("ret")
        wait_front(vm, args, 5)
        require(vm.process.poll() is None, "Default Cancel restarted QEMU")
        vm.key("ret")
        vm.wait_canvas(lambda canvas: canvas.selected("Cancel"), args, "Reset reopened")
        vm.key("down")
        vm.wait_canvas(lambda canvas: canvas.selected("Restart"), args, "Restart highlight")
        vm.key("esc")
        wait_front(vm, args, 5)
        require(vm.process.poll() is None, "Reset Esc restarted QEMU")

        vm.key("down")
        wait_front(vm, args, 6)
        offset = len(vm.text())
        vm.key("ret")
        firmware_return(vm, args, offset)
        vm.screenshot("returned-to-firmware")
        print("PASS: production full layout, all seven selections, every child Esc, "
              "Reset Cancel/Esc, repeated redraw pixels, Return to Firmware", flush=True)
    finally:
        vm.close()


def probe_case(directory, esp, args):
    vm = Qemu(directory, esp, args)
    try:
        vm.start()
        wait_front(vm, args)
        offset = len(vm.text())
        vm.key("esc")
        text = vm.wait_for(lambda text: "[MENU_PROBE] RUNTIME_FINI" in text,
                           args.timeout, "independent restoration and runtime finalization")
        verified = re.search(r"\[MENU_PROBE\] RESTORE_VERIFIED text=(\d+)x(\d+) "
                             r"attribute=10 cursor=7,3 visible=1 graphics=(\d+) "
                             r"(\d+)x(\d+) status=0", text[offset:])
        require(verified is not None, "Observer did not confirm seeded console restoration")
        require(text.index("[MENU_PROBE] RESTORE_VERIFIED")
                < text.index("[MENU_PROBE] RUNTIME_FINI"),
                "Runtime cleanup ran before the independent return observer")
        firmware_return(vm, args, offset)
        vm.screenshot("probe-returned")
        print("PASS: main Esc, independent guest restored attribute=10 cursor=7,3 "
              "visible=1, unchanged text/GOP mode, subsequent runtime_fini", flush=True)
        if "[MENU_PROBE] SMALL_MODE_UNAVAILABLE" in text:
            print("LIMIT: this OVMF console exposes no supported single-column mode; "
                  "small layout requires host-fixture coverage", flush=True)
        else:
            print("LIMIT: a smaller OVMF mode exists but this probe keeps its current mode; "
                  "small layout requires host-fixture coverage", flush=True)
    finally:
        vm.close()


def reset_case(directory, esp, args):
    vm = Qemu(directory, esp, args)
    try:
        vm.start()
        wait_front(vm, args)
        for index in range(1, 6):
            vm.key("down")
            wait_front(vm, args, index)
        vm.key("ret")
        vm.wait_canvas(lambda canvas: canvas.selected("Cancel"), args, "Reset Cancel")
        vm.key("down")
        vm.wait_canvas(lambda canvas: canvas.selected("Restart"), args, "Reset Restart")
        vm.screenshot("restart-selected")
        vm.key("ret")
        deadline = time.monotonic() + args.timeout
        while vm.process.poll() is None:
            require(time.monotonic() < deadline, "Confirmed Restart did not shut down -no-reboot QEMU")
            reply = vm.receive(deadline)
            if reply is None:
                break
        vm.process.wait(timeout=5)
        vm.text()
        require(vm.process.returncode == 0, "QEMU exited abnormally during Reset")
        require(any(event.get("event") == "SHUTDOWN"
                    and event.get("data", {}).get("guest") is True
                    and event.get("data", {}).get("reason") == "guest-reset"
                    for event in vm.events),
                "No QMP guest-reset evidence; process exit alone cannot verify ResetSystem")
        print("PASS: confirmed Restart caused a QMP guest-reset shutdown using "
              "independent ESP/VARS (-no-reboot)", flush=True)
    finally:
        vm.close()


def no_graphics_case(directory, esp, args):
    vm = Qemu(directory, esp, args, no_vga=True)
    try:
        vm.start()
        wait_front(vm, args)
        vm.key("down")
        wait_front(vm, args, 1)
        vm.key("ret")
        page = vm.wait_canvas(lambda canvas: "Graphics unavailable" in canvas.text
                              and "Firmware Vendor:" in canvas.text,
                              args, "Device Manager without VGA")
        (directory / "device.txt").write_text(page.text + "\n")
        vm.key("esc")
        wait_front(vm, args, 1)
        offset = len(vm.text())
        vm.key("esc")
        firmware_return(vm, args, offset)
        print("PASS: real -vga none, Graphics unavailable, serial text navigation and return", flush=True)
    finally:
        vm.close()


def clean_build(artifacts, name, target, args):
    build = artifacts / name
    build.mkdir()
    for source in SOURCES:
        destination = build / source
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(REPO / source, destination)
    log_path = artifacts / (name + ".log")
    with log_path.open("w") as log:
        subprocess.run(["make", target, f"OVMF_CODE_SRC={args.ovmf_code}",
                        f"OVMF_VARS_SRC={args.ovmf_vars}"], cwd=build,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
    print(f"PASS: clean-source make {target} ({log_path})", flush=True)
    return build


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=float, default=45,
                        help="Boot/input deadline in seconds per case")
    parser.add_argument("--ovmf-code", type=Path,
                        default=Path("/usr/share/OVMF/OVMF_CODE_4M.fd"))
    parser.add_argument("--ovmf-vars", type=Path,
                        default=Path("/usr/share/OVMF/OVMF_VARS_4M.fd"))
    args = parser.parse_args()
    args.ovmf_code = args.ovmf_code.resolve()
    args.ovmf_vars = args.ovmf_vars.resolve()
    require(args.timeout > 0, "The timeout must be positive")
    require(args.ovmf_code.is_file() and args.ovmf_vars.is_file(), "OVMF firmware files missing")
    artifacts = Path(tempfile.mkdtemp(prefix="uefi-menu-qemu-", dir="/tmp"))
    print(f"Artifacts: {artifacts}", flush=True)
    build = clean_build(artifacts, "build", "test-menu-static", args)
    probe = clean_build(artifacts, "probe-build", "test-menu-probe", args)
    navigation_case(artifacts / "navigation", build / "output/menu/esp", args)
    probe_case(artifacts / "probe", probe / "output/menu/tests/probe-esp", args)
    reset_case(artifacts / "reset", build / "output/menu/esp", args)
    no_graphics_case(artifacts / "no-vga", build / "output/menu/esp", args)
    print("Screenshots are unmodified QMP screendumps; PNG copies require visual review.", flush=True)


if __name__ == "__main__":
    main()
