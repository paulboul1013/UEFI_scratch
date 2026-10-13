#!/usr/bin/env python3
"""Verify real EFI loading, menu round trips, restoration, and watchdog expiry."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

from gop_qemu import check_blocks
from menu_qemu import Qemu as MenuQemu, firmware_return, require, wait_front


REPO = Path(__file__).resolve().parents[1]
SOURCES = (
    "Makefile", "linker.ld", "uefi.hpp", "runtime.hpp", "startup.cpp", "runtime.cpp",
    "boot.hpp", "boot.cpp", "menu.hpp", "menu.cpp", "station.cpp", "main.cpp",
    "tests/boot_probe.cpp", "tests/boot_child.cpp", "tests/menu_boot_probe.cpp",
    "tests/menu_boot_qemu.py", "tests/menu_qemu.py", "tests/gop_qemu.py",
)
SUCCESS = "0x0000000000000000"
DEVICE_ERROR = "0x8000000000000007"


class Qemu(MenuQemu):
    def __init__(self, directory, esp, args, no_vga=False):
        hashes = {str(path.relative_to(esp)): hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in sorted(esp.rglob("*")) if path.is_file()}
        super().__init__(directory, esp, args, no_vga=no_vga)
        (directory / "esp-sha256.json").write_text(json.dumps(hashes, indent=2) + "\n")

    def text(self):
        text = super().text()
        for marker in ("[BOOT_PROBE] FAILED", "[BOOT_CHILD] FAILED",
                       "[MENU_BOOT_PROBE] FAILED"):
            require(marker not in text, f"Guest verification failed: {marker}; see {self.serial}")
        return text

    def wait_round(self, offset, predicate, args, description):
        text = self.wait_for(lambda text: predicate(text[offset:]), args.timeout, description)
        return text[offset:]


def clean_build(artifacts, args):
    directory = artifacts / "build"
    directory.mkdir()
    hashes = {}
    for source in SOURCES:
        destination = directory / source
        destination.parent.mkdir(parents=True, exist_ok=True)
        content = (REPO / source).read_bytes()
        destination.write_bytes(content)
        hashes[source] = hashlib.sha256(content).hexdigest()
    (artifacts / "source-sha256.json").write_text(json.dumps(hashes, indent=2) + "\n")
    firmware = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in (args.ovmf_code, args.ovmf_vars)}
    (artifacts / "firmware-sha256.json").write_text(json.dumps(firmware, indent=2) + "\n")
    targets = ["build-boot-probe"] if args.core_only else [
        "test-menu-static", "build-boot-probe", "build-menu-boot-probe", "build-boot-children",
    ]
    command = ["make", *targets, f"OVMF_CODE_SRC={args.ovmf_code}",
               f"OVMF_VARS_SRC={args.ovmf_vars}"]
    (artifacts / "build-command.json").write_text(json.dumps(command, indent=2) + "\n")
    with (artifacts / "build.log").open("w") as log:
        subprocess.run(command, cwd=directory, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=90)
    images = sorted(directory.rglob("*.EFI")) + sorted(directory.rglob("*.efi"))
    image_hashes = {str(path.relative_to(directory)): hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in images}
    (artifacts / "image-sha256.json").write_text(json.dumps(image_hashes, indent=2) + "\n")
    official = (directory / "output/BOOTX64.efi").read_bytes()
    probe_child = directory / "output/menu/tests/boot-probe-esp/EFI/TOOLS/GOP.EFI"
    require(probe_child.read_bytes() == official, "Core probe child differs from clean production GOP")
    if not args.core_only:
        station = directory / "output/menu/esp/EFI/BOOT/BOOTX64.EFI"
        child = directory / "output/menu/esp/EFI/TOOLS/GOP.EFI"
        require(child.read_bytes() == official, "Production menu child differs from clean GOP")
        require(station.read_bytes() != official, "Station was copied to GOP.EFI")
    print("PASS: clean-source build and separate production/test image hashes", flush=True)
    return directory


def boot_page(vm, args):
    return vm.wait_canvas(lambda canvas: canvas.selected("GOP Demo")
                          and "Path: \\EFI\\TOOLS\\GOP.EFI" in canvas.text,
                          args, "configured Boot Manager list")


def open_boot(vm, args):
    wait_front(vm, args)
    for index in (1, 2):
        vm.key("down")
        wait_front(vm, args, index)
    offset = len(vm.text())
    vm.key("ret")
    boot_page(vm, args)
    new = vm.text()[offset:]
    require("[GOP] FOUND" not in new and "[BOOT_CHILD] ENTRY" not in new
            and "[MENU_BOOT_PROBE] BEFORE_START" not in new,
            "Opening the list unexpectedly started an image")


def drawing(vm, offset, args, name):
    new = vm.wait_round(offset, lambda text: "[GOP] DRAW_OK" in text
                        and "Press any key to exit..." in text,
                        args, "current-round GOP drawing")
    for marker in ("[OK] Global constructor executed", "Hello UEFI", "[GOP] FOUND",
                   "[GOP] RESOLUTION 800x600", "Press any key to exit..."):
        require(marker in new, "Missing current-round child evidence: " + marker)
    require("[GOP] ERROR" not in new, "Current-round GOP error")
    screen = vm.screenshot(name)
    require(screen[:2] == (800, 600), "Actual child screenshot is not 800x600")
    check_blocks(screen)
    return new


def core_case(directory, esp, args):
    vm = Qemu(directory, esp, args)
    try:
        vm.start()
        vm.wait_for(lambda text: "[BOOT_PROBE] ENTRY" in text, args.timeout, "core parent entry")
        drawing(vm, 0, args, "core-gop")
        offset = len(vm.text())
        vm.key("spc")
        new = vm.wait_round(offset, lambda text: "[BOOT_PROBE] RUNTIME_FINI" in text,
                            args, "core loader return and parent runtime cleanup")
        require(re.search(r"\[BOOT_PROBE\] RETURN stage=[0-4] status=" + SUCCESS
                          + " cleanup=" + SUCCESS + r" pending=0", new) is not None,
                "Real loader did not return success with the child released")
        require(new.index("[BOOT_PROBE] RETURN") < new.index("[BOOT_PROBE] RUNTIME_FINI"),
                "Parent runtime_fini ran before control returned")
        firmware_return(vm, args, offset)
        vm.screenshot("core-returned")
        print("PASS: real parent handle, production GOP LoadImage/StartImage, 800x600 RGB, return", flush=True)
    finally:
        vm.close()


def product_round_trips(directory, esp, args):
    vm = Qemu(directory, esp, args)
    try:
        vm.start()
        open_boot(vm, args)
        original_screen = vm.screenshot("configured-list")
        for round_number in range(1, 4):
            offset = len(vm.text())
            vm.key("ret")
            drawing(vm, offset, args, f"gop-{round_number}")
            return_offset = len(vm.text())
            vm.key("spc")
            new = vm.wait_round(return_offset, lambda text: "Image returned" in text
                                and "Status: " + SUCCESS in text,
                                args, f"round {round_number} product return")
            require("Status: " + SUCCESS in new, "Current-round UI return status is missing")
            page = boot_page(vm, args)
            require("Image returned" in page.text and "Status: " + SUCCESS in page.text,
                    "Product return status is absent from the final canvas")
            returned_screen = vm.screenshot(f"returned-{round_number}")
            require(returned_screen[:2] == original_screen[:2], "Product return changed geometry")
            require("[GOP] ERROR" not in vm.text()[offset:], "GOP reported a round-trip error")
        vm.key("esc")
        wait_front(vm, args, 2)
        for index in (3, 4, 5):
            vm.key("down")
            wait_front(vm, args, index)
        vm.key("ret")
        vm.wait_canvas(lambda canvas: "Restart the machine?" in canvas.text
                       and canvas.selected("Cancel"), args, "Reset after three children")
        vm.key("ret")
        wait_front(vm, args, 5)
        vm.key("down")
        wait_front(vm, args, 6)
        offset = len(vm.text())
        vm.key("ret")
        firmware_return(vm, args, offset)
        vm.screenshot("product-firmware-return")
        print("PASS: production Station three GOP starts, per-round 800x600 RGB screenshots, "
              "list return, navigation, Reset Cancel, firmware return", flush=True)
    finally:
        vm.close()


def payload_esp(artifacts, name, original, payload=None, corrupt=False, missing=False):
    esp = artifacts / (name + "-template")
    shutil.copytree(original, esp)
    child = esp / "EFI/TOOLS/GOP.EFI"
    if missing:
        child.unlink()
    elif corrupt:
        child.write_bytes(b"This is not a PE/COFF EFI application.\r\n")
    elif payload is not None:
        shutil.copyfile(payload, child)
    return esp


def observed_round(vm, args, round_number, kind):
    offset = len(vm.text())
    vm.key("ret")
    marker = f"[MENU_BOOT_PROBE] RESTORE_VERIFIED round={round_number} "
    new = vm.wait_round(offset, lambda text: marker in text, args, kind + " independent restore")
    require("before_wait=1" in new and "watchdog=0" in new,
            "Observer did not verify before the next real wait")
    require(f"[MENU_BOOT_PROBE] BEFORE_START round={round_number}" in new,
            "Restoration marker does not have a current-round StartImage marker")
    expected = DEVICE_ERROR if kind == "ERROR" else SUCCESS
    for marker in (f"[BOOT_CHILD] ENTRY {kind}", "[BOOT_CHILD] BEFORE_RETURN status=" + expected,
                   "[BOOT_CHILD] RUNTIME_FINI",
                   f"[MENU_BOOT_PROBE] CHILD_RETURN round={round_number} status=" + expected):
        require(marker in new, "Missing current-round lifecycle evidence: " + marker)
    require(new.index("[BOOT_CHILD] ENTRY") < new.index("[BOOT_CHILD] BEFORE_RETURN")
            < new.index("[BOOT_CHILD] RUNTIME_FINI") < new.index("[MENU_BOOT_PROBE] CHILD_RETURN")
            < new.index("[MENU_BOOT_PROBE] RESTORE_VERIFIED"), "Child lifecycle order is wrong")
    require("[MENU_BOOT_PROBE] RUNTIME_FINI" not in new, "Child return finalized its parent")
    page = boot_page(vm, args)
    require(("Start failed" if kind == "ERROR" else "Image returned") in page.text
            and "Status: " + expected in page.text, "UI reports an incorrect child status")
    if kind == "DIRTY":
        require("changed_text=1 changed_graphics=1 changed_attribute=1 changed_cursor=1" in new,
                "Dirty child did not leave all actual settings changed")
        require("[BOOT_CHILD] WATCHDOG_ARMED seconds=2" in new, "Short watchdog was not armed")
        # The control VM proves this successful call actually enables a reset timer.
        deadline = time.monotonic() + 4.5
        while time.monotonic() < deadline:
            require(vm.process.poll() is None, "Returned dirty child left an active watchdog")
            vm.text()
            time.sleep(0.1)
        require(vm.command("query-status")["running"], "Parent stopped during the idle interval")
    return new


def child_case(directory, esp, args, kind, no_vga=False):
    vm = Qemu(directory, esp, args, no_vga=no_vga)
    try:
        vm.start()
        open_boot(vm, args)
        for round_number in range(1, 3):
            new = observed_round(vm, args, round_number, kind)
            if no_vga:
                require("graphics=absent watchdog=0 before_wait=1" in new,
                        "No-VGA observer unexpectedly found GOP")
            else:
                vm.screenshot(f"{kind.lower()}-returned-{round_number}")
        vm.key("esc")
        wait_front(vm, args, 2)
        vm.key("up")
        wait_front(vm, args, 1)
        vm.key("ret")
        vm.wait_canvas(lambda canvas: "Firmware Vendor:" in canvas.text
                       and (not no_vga or "Graphics unavailable" in canvas.text),
                       args, "Device Manager after child restoration")
        vm.key("esc")
        wait_front(vm, args, 1)
        offset = len(vm.text())
        vm.key("esc")
        new = vm.wait_round(offset, lambda text: "[MENU_BOOT_PROBE] RUNTIME_FINI" in text,
                            args, "parent runtime cleanup after final menu exit")
        require("[MENU_BOOT_PROBE] PARENT_RETURN status=" + SUCCESS in new,
                "Parent did not return success after restored child rounds")
        firmware_return(vm, args, offset)
        print(f"PASS: {kind} two real child entries/returns/finalizers, "
              "independent state before next wait, repeated start and final parent exit"
              + (", no VGA" if no_vga else ""), flush=True)
    finally:
        vm.close()


def load_failure_case(directory, esp, args, missing):
    vm = Qemu(directory, esp, args)
    try:
        vm.start()
        open_boot(vm, args)
        for round_number in range(1, 3):
            offset = len(vm.text())
            vm.key("ret")
            new = vm.wait_round(offset, lambda text: "Load failed" in text
                                and re.search(r"Status: 0x[0-9A-F]{16}", text) is not None,
                                args, "current-round failed LoadImage")
            match = re.search(r"Status: (0x[0-9A-F]{16})", new)
            require(match is not None, "Load failure has no current-round EFI_STATUS")
            allowed = {"0x800000000000000E"} if missing else {
                "0x8000000000000001", "0x8000000000000003",
            }
            require(match.group(1) in allowed, "Unexpected real LoadImage failure status")
            require("[GOP]" not in new and "[BOOT_CHILD] ENTRY" not in new,
                    "Failed load reached a child entry point")
            page = boot_page(vm, args)
            require("Load failed" in page.text and match.group(1) in page.text,
                    "Load failure is absent from the final canvas")
            vm.screenshot(f"load-failed-{round_number}")
        vm.key("esc")
        wait_front(vm, args, 2)
        offset = len(vm.text())
        vm.key("esc")
        firmware_return(vm, args, offset)
        print("PASS: " + ("missing" if missing else "corrupt")
              + " per-VM payload, actual current-round Load status, retry and usable exit", flush=True)
    finally:
        vm.close()


def watchdog_control(directory, esp, args):
    vm = Qemu(directory, esp, args)
    try:
        vm.start()
        vm.wait_for(lambda text: "[BOOT_CHILD] WATCHDOG_ARMED seconds=2" in text,
                    args.timeout, "watchdog control successful arm")
        deadline = time.monotonic() + 8
        while vm.process.poll() is None:
            reply = vm.receive(deadline)
            if reply is None:
                break
        vm.process.wait(timeout=5)
        require(vm.process.returncode == 0, "Watchdog reset exited QEMU abnormally")
        require(any(event.get("event") == "SHUTDOWN"
                    and event.get("data", {}).get("guest") is True
                    and event.get("data", {}).get("reason") == "guest-reset"
                    for event in vm.events), "Successful 2-second watchdog did not actually reset")
        require("[BOOT_CHILD] RUNTIME_FINI" not in vm.text(), "Control unexpectedly returned")
        print("PASS: independent 2-second watchdog control produced actual QMP guest-reset", flush=True)
    finally:
        vm.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core-only", action="store_true")
    parser.add_argument("--timeout", type=float, default=45)
    parser.add_argument("--ovmf-code", type=Path,
                        default=Path("/usr/share/OVMF/OVMF_CODE_4M.fd"))
    parser.add_argument("--ovmf-vars", type=Path,
                        default=Path("/usr/share/OVMF/OVMF_VARS_4M.fd"))
    args = parser.parse_args()
    args.ovmf_code = args.ovmf_code.resolve()
    args.ovmf_vars = args.ovmf_vars.resolve()
    require(args.timeout > 0 and args.ovmf_code.is_file() and args.ovmf_vars.is_file(),
            "Positive timeout and OVMF CODE/VARS files are required")
    artifacts = Path(tempfile.mkdtemp(prefix="uefi-menu-boot-qemu-", dir="/tmp"))
    print(f"Artifacts: {artifacts}", flush=True)
    build = clean_build(artifacts, args)
    tests = build / "output/menu/tests"
    core_case(artifacts / "core", tests / "boot-probe-esp", args)
    if args.core_only:
        return
    production = build / "output/menu/esp"
    observer = tests / "boot-observer-esp"
    product_round_trips(artifacts / "production", production, args)
    control_esp = payload_esp(artifacts, "watchdog-control", production)
    shutil.copyfile(tests / "BOOT_WATCHDOG_HOLD.EFI", control_esp / "EFI/BOOT/BOOTX64.EFI")
    watchdog_control(artifacts / "watchdog-control", control_esp, args)
    for kind in ("DIRTY", "ERROR", "TEXT"):
        esp = payload_esp(artifacts, kind.lower(), observer, tests / f"BOOT_{kind}.EFI")
        child_case(artifacts / kind.lower(), esp, args, kind, no_vga=kind == "TEXT")
    for name, missing in (("missing", True), ("corrupt", False)):
        esp = payload_esp(artifacts, name, production, missing=missing, corrupt=not missing)
        load_failure_case(artifacts / name, esp, args, missing)
    print("Each VM retained its own ESP, VARS, exact command, serial log and QMP events. "
          "Unmodified PNG screenshots still require visual review.", flush=True)


if __name__ == "__main__":
    main()
