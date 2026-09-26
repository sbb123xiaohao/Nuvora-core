#!/usr/bin/env python3
"""Convert host media to a format playable by Nuvora Media, optionally import it.

Requires FFmpeg and ffprobe on the host. MP4/MKV/WebM/AAC/Vorbis are converted
here; this does not claim those codecs are decoded by the guest kernel.
"""
import argparse
import json
import pathlib
import shutil
import subprocess
import tempfile
from import_media import import_files


def prepare(source, output, height=1080, audio_only=False):
    source, output = pathlib.Path(source).resolve(), pathlib.Path(output).resolve()
    if not source.is_file(): raise ValueError('Input must be a regular file')
    if output.exists(): raise ValueError(f'Refusing to overwrite {output}')
    if not 16 <= height <= 4094: raise ValueError('Video height must be 16..4094 (MPEG-1 syntax)')
    if not shutil.which('ffmpeg') or not shutil.which('ffprobe'):
        raise ValueError('Install FFmpeg and ffprobe on the host first')
    info = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_streams', '-of', 'json', str(source)]))
    video = not audio_only and any(s.get('codec_type') == 'video' and not s.get('disposition', {}).get('attached_pic') for s in info['streams'])
    audio = any(s.get('codec_type') == 'audio' for s in info['streams'])
    if not video and not audio: raise ValueError('No playable audio or video stream found')
    extension = '.mpg' if video else '.flac'
    if output.suffix.lower() != extension: raise ValueError(f'Output must end in {extension}')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.nuvora-media-', dir=output.parent) as directory:
        temp = pathlib.Path(directory) / ('output'+extension)
        command = ['ffmpeg', '-nostdin', '-v', 'error', '-i', str(source)]
        if video:
            command += ['-map', '0:V:0', '-map', '0:a:0?', '-vf',
                        f"scale=w='min(iw,4094)':h='min(ih,{height})':force_original_aspect_ratio=decrease:force_divisible_by=2,setsar=1",
                        '-r', '25', '-c:v', 'mpeg1video', '-q:v', '3', '-pix_fmt', 'yuv420p',
                        '-c:a', 'mp2', '-b:a', '192k', '-ar', '48000', '-ac', '2', '-f', 'mpeg']
        else:
            command += ['-map', '0:a:0', '-vn', '-c:a', 'flac', '-ar', '48000', '-ac', '2']
        subprocess.run(command + [str(temp)], check=True)
        # Decode the completed output once: successful encoding alone is not validation.
        subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-xerror', '-i', str(temp), '-f', 'null', '-'], check=True)
        # Exclusive creation also protects against a destination appearing mid-conversion.
        with temp.open('rb') as inp, output.open('xb') as out:
            shutil.copyfileobj(inp, out, 1024*1024)
    return output


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=pathlib.Path)
    p.add_argument('output', type=pathlib.Path)
    p.add_argument('--height', type=int, default=1080, help='maximum video height; keeps smaller sources unchanged')
    p.add_argument('--audio-only', action='store_true')
    p.add_argument('--disk', type=pathlib.Path, help='import the converted file into a powered-off data image')
    p.add_argument('--replace', action='store_true', help='replace the matching filename on C:')
    a = p.parse_args()
    try:
        result = prepare(a.source, a.output, a.height, a.audio_only)
        if a.disk: import_files(a.disk, [result], a.replace)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        p.exit(1, f'Media preparation failed: {error}\n')
    print(f'Prepared {result}' + (' and imported into C:' if a.disk else ''))

if __name__ == '__main__': main()
