from pathlib import Path
import importlib.util
from types import SimpleNamespace
import time
repo = Path('/home/paulboul/UEFI_scratch')
spec = importlib.util.spec_from_file_location('menu_qemu', repo / 'tests/menu_qemu.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
scratch = Path('/tmp/menu-independent-watchdog-5lcfsncx')
args = SimpleNamespace(ovmf_code='/usr/share/OVMF/OVMF_CODE_4M.fd', ovmf_vars='/usr/share/OVMF/OVMF_VARS_4M.fd', timeout=20)
vm = module.Qemu(scratch / 'vm-fixed', scratch / 'output/menu/esp', args)
try:
    vm.start()
    module.wait_front(vm, args)
    time.sleep(4)
    assert vm.process.poll() is None, 'Watchdog unexpectedly reset the fixed menu'
    assert module.front(vm.terminal()), 'Fixed menu disappeared while idle'
    offset = len(vm.text())
    vm.key('esc')
    module.firmware_return(vm, args, offset)
    print('PASS: fixed menu cancels an armed 2-second OVMF watchdog; survives 4 idle seconds; Esc returns to firmware.')
    print('Artifacts:', scratch / 'vm-fixed')
finally:
    vm.close()
