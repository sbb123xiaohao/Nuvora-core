#!/usr/bin/env python3
"""Capture the production UEFI desktop through QEMU's real framebuffer.

Requires make all esp, QEMU, OVMF and mtools. Boot, firmware variables
and data are private copies. PNG conversion preserves every framebuffer pixel.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import struct
import subprocess
import tempfile
import time
import zlib
import wave

from desktop_boot_test import Monitor, first_account, desktop_visible, ui_scale, qemu, create
from mkmedia import create as create_media
from session_boot_test import wait_saved_file, settings_page, drag_volume
from import_media import import_files
from pc_audio_boot_test import captured_samples


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def create_audio(path,period):
    """Create an original 20-second stereo PCM fixture without external tools."""
    samples=bytearray()
    for frame in range(48000):
        left=abs(2*(frame%period)-period)*12000//period-6000
        right=abs(2*((frame+period//4)%period)-period)*12000//period-6000
        samples.extend(struct.pack('<hh',left,right))
    with wave.open(str(path),'wb') as stream:
        stream.setnchannels(2);stream.setsampwidth(2);stream.setframerate(48000)
        for _ in range(20): stream.writeframesraw(samples)


def music_content_rect(width,height):
    """Expected portrait client rectangle, including native frame placement."""
    scale=ui_scale(width,height);sw=width//scale;sh=height//scale
    ww=min(sw-16,(min(620,width-40)+scale-1)//scale+2)
    hh=min(sh-72,(min(760,height-100)+scale-1)//scale+29)
    x=min(sw-ww,(sw-ww)//2+36);y=min(sh-56-hh,(sh-56-hh)//2+36)
    return (x+1)*scale,(y+28)*scale,(ww-2)*scale,(hh-29)*scale


def music_pixels(pixels,width,height):
    x,y,w,h=music_content_rect(width,height)
    return b''.join(pixels[((y+row)*width+x)*3:((y+row)*width+x+w)*3] for row in range(h))


def assert_music_volume(pixels,width,height,percent):
    x,y,w,h=music_content_rect(width,height)
    scale=3 if w>=780 and h>=900 else 2 if w>=450 and h>=580 else 1
    body=min(w,320*scale);rows=3 if h>=580 else 2 if h>=360 else 1
    library=h-rows*22*scale-16*scale
    span=min(70*scale,body//3);start=w//2-span//2
    def color(at):
        offset=((y+library-7*scale+1)*width+x+start+span*at//100)*3
        return int.from_bytes(pixels[offset:offset+3],'big')
    assert color(percent-5)==0x1761f5 and color(percent+5)==0xdde3ed


def wait_music(monitor,output,width,height,paused,different=None,timeout=10):
    """Wait for the real client to publish its play/pause button state."""
    x,y,w,h=music_content_rect(width,height)
    scale=3 if w>=780 and h>=900 else 2 if w>=450 and h>=580 else 1
    rows=3 if h>=580 else 2 if h>=360 else 1
    control_y=h-rows*22*scale-16*scale-46*scale
    deadline=time.monotonic()+timeout
    while True:
        pixels=monitor.shot(output,'music-state')[2]
        center=((y+control_y+16*scale)*width+x+w//2)*3
        rim=center-10*scale*3
        frame=music_pixels(pixels,width,height)
        expected=0xffffff if paused else 0x1761f5
        if (int.from_bytes(pixels[center:center+3],'big')==expected and
            int.from_bytes(pixels[rim:rim+3],'big')==0x1761f5 and
            (different is None or frame!=different)):
            return pixels
        if time.monotonic()>=deadline:
            raise AssertionError(f'Music did not publish paused={paused}')
        time.sleep(.1)


def write_png(path, width, height, pixels):
    """Store unmodified RGB framebuffer rows in a standard lossless PNG."""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    stride = width * 3
    rows = b''.join(b'\0' + pixels[y * stride:(y + 1) * stride] for y in range(height))
    header = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) +
                     chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path, default=qemu.BUILD / 'screenshots')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    firmware = qemu.find_uefi_firmware()
    if not firmware:
        raise SystemExit('OVMF not found')
    production = qemu.BUILD / 'nuvora-uefi.elf'
    source_esp = qemu.BUILD / 'esp.img'
    if not production.is_file() or not source_esp.is_file():
        raise SystemExit('Build the production image first: make all esp')
    pictures = []
    serial = output / 'serial.log'
    with tempfile.TemporaryDirectory(prefix='nuvora-screenshots-') as directory:
        private = pathlib.Path(directory)
        disk, esp, media = private / 'data.img', private / 'esp.img', private / 'boot.img'
        tracks=[private/name for name in ('Nightfall.wav','Rainwalk.wav','Coastline.wav')]
        for track,period in zip(tracks,(110,146,196)): create_audio(track,period)
        create(disk, size_mib=128, partitions=1)
        import_files(disk,[qemu.ROOT / 'tests/fixtures/clip.mpg',qemu.ROOT / 'tests/fixtures/tone.mp3',*tracks])
        audio_path=output/'hda-output.wav'
        shutil.copyfile(source_esp, esp)
        create_media(esp, media)
        original = qemu.BUILD
        qemu.BUILD = private
        try:
            command = qemu.command(memory=256, disk=disk, machine='q35', kernel=False, disk_bus='ahci')
            command += qemu.firmware_arguments(firmware)
        finally:
            qemu.BUILD = original
        command += ['-display', 'none', '-qmp', 'stdio', '-serial', f'file:{serial}',
                    '-device', 'qemu-xhci,id=xhci', '-device', 'usb-kbd,bus=xhci.0',
                    '-device', 'usb-tablet,bus=xhci.0',
                    '-netdev', 'user,id=net0', '-device', 'e1000e,netdev=net0',
                    '-audiodev', f'wav,id=audio0,path={audio_path},out.frequency=48000,out.channels=2,out.format=s16',
                    '-device', 'intel-hda',
                    '-device', 'hda-duplex,audiodev=audio0',
                    '-drive', f'file={media},format=raw,if=none,id=boot_media',
                    '-device', 'usb-storage,bus=xhci.0,drive=boot_media,bootindex=1']
        with (output / 'qemu.log').open('w') as log:
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log)
            try:
                monitor = Monitor(process)
                deadline = time.monotonic() + 60
                while 'Ring 3: /apps/session' not in serial.read_text(errors='replace'):
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError('Production graphical session did not boot')
                    time.sleep(.2)
                first_account(monitor, output)

                def capture(name):
                    width, height, pixels = monitor.shot(output, name)
                    assert desktop_visible(pixels, ui_scale(width, height)), name
                    target = output / (name + '.png')
                    write_png(target, width, height, pixels)
                    pictures.append({'file': target.name, 'width': width, 'height': height,
                                     'sha256': digest(target)})
                    print(f'CAPTURE {target.name}: {width}x{height}', flush=True)
                    return width,height

                width,height=capture('desktop')
                monitor.start(1)
                monitor.key('meta_l', 'right')
                paragraph = ('Native desktop with files and terminal and a saved document on the private AHCI data volume. '
                             'This text wraps across visual rows.')
                prefix = 'Nuvora Core\n\n'
                document = prefix + paragraph + '\n'
                monitor.type(prefix + paragraph)
                # The paragraph wraps in this tiled editor. Up then Down must
                # return to its final visual row before inserting the newline.
                monitor.key('up')
                monitor.key('down')
                monitor.key('ret')
                monitor.key('ctrl', 's')
                monitor.key('ret')
                monitor.shot(output,'editor-saved')
                wait_saved_file(monitor,disk,'/home/users/tester/Untitled.txt',document.encode())
                monitor.key('alt', 'f9')
                monitor.start(2)
                monitor.type('nest Projects\nweave readme.md Nuvora\nweave ideas.txt Ideas\n'
                             'weave .hidden-note private\nmirror /home/tone.mp3 sound.mp3\n'
                             'mirror /home/clip.mpg clip.mpg\nanchor\n')
                wait_saved_file(monitor,disk,'/home/users/tester/readme.md',b'Nuvora\n')
                for track in tracks:
                    monitor.type('mirror /home/'+track.name+' '+track.name+'\nanchor\n')
                    monitor.shot(output,'terminal-importing')
                    wait_saved_file(monitor,disk,'/home/users/tester/'+track.name,track.read_bytes(),timeout=30)
                monitor.shot(output,'terminal-imported')
                monitor.key('alt', 'f9')
                monitor.start(0)
                monitor.key('ctrl','f');monitor.type('ReAdMe');monitor.key('ret');monitor.key('ret')
                monitor.key('end');monitor.type(' Core');monitor.key('ctrl','s')
                wait_saved_file(monitor,disk,'/home/users/tester/readme.md',b'Nuvora Core\n')
                monitor.key('alt','f9');monitor.start(0)
                monitor.key('ctrl','f');monitor.key('esc')
                capture('files')
                # Open through the real volume-qualified Files launch path.
                monitor.key('ctrl','f');monitor.type('Nightfall');monitor.key('ret');monitor.key('ret')
                wait_music(monitor,output,width,height,False)
                time.sleep(1.2);monitor.key('spc')
                wait_music(monitor,output,width,height,True);monitor.start(3,mouse=True)
                paused=music_pixels(wait_music(monitor,output,width,height,True),width,height)
                time.sleep(.7)
                assert music_pixels(monitor.shot(output,'music-pause-stable')[2],width,height)==paused
                # Switching while paused releases the old decoder; minimizing
                # the new decoder must allow playback and later restoration.
                monitor.key('right');wait_music(monitor,output,width,height,False,different=paused)
                monitor.key('alt','f9');time.sleep(.5);monitor.start(3,mouse=True)
                wait_music(monitor,output,width,height,False);monitor.key('spc')
                next_track=music_pixels(wait_music(monitor,output,width,height,True),width,height)
                assert next_track!=paused
                monitor.key('left');wait_music(monitor,output,width,height,False,different=next_track)
                monitor.key('spc');wait_music(monitor,output,width,height,True);monitor.key('alt','f9')
                monitor.start(0);monitor.key('ctrl','f');monitor.key('esc')
                monitor.start(2)
                monitor.type('origin\nhorizon\nvolumes\n')
                monitor.key('meta_l', 'right')
                monitor.start(0)
                monitor.key('meta_l', 'left')
                monitor.key('f5')
                capture('files-terminal')
                monitor.start(1)
                capture('files-editor')
                for app in (0, 1, 2):
                    monitor.start(app)
                    monitor.key('alt', 'f9')
                settings_page(monitor,0)
                capture('settings')
                settings_page(monitor,1)
                drag_volume(monitor,width,height,80,output)
                capture('settings-sound')
                settings_page(monitor,2)
                monitor.key('tab');monitor.key('ret')
                deadline=time.monotonic()+20
                while '[net] DHCP address 10.0.2.15' not in serial.read_text(errors='replace'):
                    if time.monotonic()>deadline:
                        raise RuntimeError('Settings DHCP action did not configure IPv4')
                    time.sleep(.2)
                time.sleep(5.2)
                capture('settings-network')
                monitor.key('alt','f9')
                monitor.pointer(width,height,width-30,height-100,False)
                monitor.start(3,mouse=True)
                restored=wait_music(monitor,output,width,height,True)
                deadline=time.monotonic()+10
                while True:
                    try:
                        assert_music_volume(restored,width,height,80)
                        break
                    except AssertionError:
                        if time.monotonic()>=deadline: raise
                        restored=monitor.shot(output,'music-settings-volume-restored')[2]
                time.sleep(.7)
                assert music_pixels(monitor.shot(output,'music-restored-pause-stable')[2],width,height)==music_pixels(restored,width,height)
                capture('music')
                monitor.key('alt','f4');time.sleep(.5)  # Close a paused decoder cleanly.
                text = serial.read_text(errors='replace')
                assert 'PANIC' not in text and '[trap]' not in text
                assert 'PROBE RESULT:' not in text and 'Loom / Nuvora command environment' not in text
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
    # The QEMU WAV backend can leave RIFF/data lengths zero on termination.
    # Reuse the hardware fixture's strict PCM header and sample validation.
    pcm=captured_samples(audio_path)
    assert pcm,'Empty Music HDA capture'
    peaks=[max(abs(frame[channel]) for frame in pcm) for channel in range(2)]
    assert all(4000<=peak<=6500 for peak in peaks),peaks
    manifest = {'capture': 'QMP screendump; lossless P6 to RGB PNG',
                'boot': 'production UEFI USB; q35; 256 MiB; AHCI; USB keyboard/tablet; e1000e; HDA',
                'qemu': subprocess.check_output([qemu.qemu_binary(), '--version'], text=True).splitlines()[0],
                'kernel_sha256': digest(production), 'esp_sha256': digest(source_esp),
                'firmware_sha256': digest(firmware), 'screenshots': pictures,
                'audio_capture_sha256':digest(audio_path),'audio_peaks':peaks}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('PASS production captures: Files search/open/save, Music pause/next/previous/minimize/restore/close, live HDA waveform and shared volume, Settings DHCP', flush=True)


if __name__ == '__main__':
    main()
