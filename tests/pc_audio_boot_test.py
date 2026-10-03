#!/usr/bin/env python3
"""Boot HDA controllers and check the amplitude of captured stereo test tones.

Only temporary WAV captures, ESP images and private firmware variables change.
The sample check detects a working DMA stream whose codec gain is too quiet.
"""
import argparse
import json
import pathlib
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'scripts'))
import qemu
import test as suite

CONFIGURATIONS = [('pc', 'intel-hda', 'hda-output', False),
                  ('q35', 'ich9-intel-hda', 'hda-duplex', True)]


def captured_samples(path):
    data = path.read_bytes()
    assert len(data) >= 44 and (len(data) - 44) % 4 == 0, 'Truncated stereo PCM capture'
    assert data[:4] == b'RIFF' and data[8:16] == b'WAVEfmt ' and data[36:40] == b'data'
    assert struct.unpack_from('<IHHIIHH', data, 16) == (16, 1, 2, 48000, 192000, 4, 16)
    # QEMU's WAV backend may leave size placeholders zero on emulator poweroff.
    # Validate its fixed PCM header and all actual samples, including that case.
    assert struct.unpack_from('<I', data, 4)[0] in (0, len(data) - 8)
    assert struct.unpack_from('<I', data, 40)[0] in (0, len(data) - 44)
    return list(struct.iter_unpack('<hh', data[44:]))


def run(configuration, directory, memory):
    machine, controller, codec, uefi = configuration
    name = f'{machine}-{controller}-{codec}'
    capture = directory / 'output.wav'
    capture_spec = str(capture).replace(',', ',,')
    hardware = ['-audiodev', f'wav,id=sound,path={capture_spec},out.frequency=48000,'
                'out.channels=2,out.format=s16', '-device', f'{controller},id=hda',
                '-device', f'{codec},bus=hda.0,audiodev=sound']
    original_firmware = suite.firmware_arguments

    def private_firmware(firmware):
        original_build = qemu.BUILD
        qemu.BUILD = directory
        try:
            return qemu.firmware_arguments(firmware)
        finally:
            qemu.BUILD = original_build

    suite.firmware_arguments = private_firmware
    try:
        vm = suite.VM(name, memory=memory, machine=machine, uefi=uefi,
                      hardware=hardware, boot_timeout=90)
        try:
            assert '[audio] HDA analog output:' in vm.text(), vm.text()
            output = vm.send('wave --test', timeout=60)
            assert not any(error in output for error in
                           ('Audio:', 'Playback:', 'No supported', 'Expected PCM')), output
            vm.pump(0.2)
        finally:
            vm.close()
    finally:
        suite.firmware_arguments = original_firmware
    samples = captured_samples(capture)
    assert samples, 'Empty HDA capture'
    peaks = [max(abs(frame[channel]) for frame in samples) for channel in range(2)]
    active = sum(any(frame) for frame in samples)
    # wave --test produces a 440 Hz triangle with a 3000 sample peak at 0 dB.
    assert all(2500 <= peak <= 3100 for peak in peaks), (name, peaks)
    assert active >= 12000, (name, active)
    result = {'configuration': name, 'result': 'PASS', 'frames': len(samples),
              'active_frames': active, 'left_peak': peaks[0], 'right_peak': peaks[1]}
    print(f'PASS {name}: 48 kHz stereo PCM, {active} active frames, peaks {peaks}', flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--memory', type=int, default=64, metavar='MIB')
    args = parser.parse_args()
    if args.memory < 32:
        parser.error('--memory must be at least 32 MiB')
    subprocess.run(['make', '-s', 'diagnostics', 'esp'], cwd=ROOT, check=True)
    if not qemu.find_uefi_firmware():
        raise SystemExit('Install OVMF or set NV_OVMF')
    suite.REPORT = qemu.BUILD / 'pc-audio-results'
    suite.REPORT.mkdir(parents=True, exist_ok=True)
    results = []
    with tempfile.TemporaryDirectory(prefix='nuvora-pc-audio-') as temporary:
        for index, configuration in enumerate(CONFIGURATIONS):
            directory = pathlib.Path(temporary) / str(index)
            directory.mkdir()
            results.append(run(configuration, directory, args.memory))
    (suite.REPORT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
