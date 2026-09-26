"""Check Ubuntu OVMF discovery and QEMU disk arguments without a QEMU install."""
import os
import pathlib
import sys
import tempfile
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'scripts'))
import qemu


def main():
    with tempfile.TemporaryDirectory(prefix='nuvora-qemu-options-') as directory:
        root = pathlib.Path(directory)
        firmware = root / 'firmware'
        firmware.mkdir()
        combined = firmware / 'OVMF.fd'
        code = firmware / 'OVMF_CODE_4M.fd'
        template = firmware / 'OVMF_VARS_4M.fd'
        combined.write_bytes(b'C' * 1048576)
        code.write_bytes(b'F' * 1048576)
        template.write_bytes(b'V' * 131072)  # Split flash sizes need not sum to a power of two.
        build = root / 'build'
        with mock.patch.dict(os.environ, {'NV_OVMF': '', 'NV_QEMU': ''}), \
             mock.patch.object(qemu, 'OVMF_DIRS', (firmware,)), \
             mock.patch.object(qemu, 'qemu_binary', return_value=None), \
             mock.patch.object(qemu, 'BUILD', build):
            assert qemu.find_uefi_firmware() == code
            options = qemu.firmware_arguments(code)
            assert '-bios' not in options and options.count('-drive') == 2
            assert 'readonly=on' in options[1] and 'unit=0' in options[1]
            assert 'unit=1' in options[3] and 'snapshot=on' not in options[3]
            variables = build / 'ovmf-vars.fd'
            assert variables.read_bytes() == template.read_bytes()
            variables.write_bytes(b'X' * 131072)
            assert qemu.firmware_arguments(code) == options
            assert variables.read_bytes() == b'X' * 131072
            assert template.read_bytes() == b'V' * 131072
            variables.write_bytes(b'X' * 4096)
            try:
                qemu.firmware_arguments(code)
                raise AssertionError('mismatched flash size accepted')
            except RuntimeError as error:
                assert 'different size' in str(error)
            template.unlink()
            assert qemu.find_uefi_firmware() == combined
            assert qemu.firmware_arguments(combined) == ['-bios', str(combined)]
            try:
                qemu.firmware_arguments(code)
                raise AssertionError('missing variable template accepted')
            except RuntimeError as error:
                assert 'Matching UEFI VARS' in str(error)

            disk = root / 'data.img'
            disk.touch()
            esp = root / 'esp.img'
            esp.touch()
            with mock.patch.object(qemu, 'qemu_binary', return_value='/bin/true'):
                for bus in ('ide', 'ahci', 'nvme'):
                    cmd = qemu.command(disk=disk, esp=esp, disk_bus=bus)
                    assert ('ich9-ahci,id=nuvora_sata' in cmd) == (bus == 'ahci')
                    assert ('nvme,drive=nuvora_disk,serial=nuvora-data' in cmd) == (bus == 'nvme')
    print('PASS QEMU options: split OVMF variables, combined fallback, IDE/AHCI/NVMe')


if __name__ == '__main__':
    main()
