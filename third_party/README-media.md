# Media decoders

- `minimp3.h`: lieff/minimp3, commit
  `ea99364f61c14656440e8d77e9c233ccf3124633`, CC0 1.0.
  Source: https://github.com/lieff/minimp3
  License: `LICENSE-minimp3.txt`.
- `pl_mpeg.h`: phoboslab/pl_mpeg, commit
  `c871f2be022ece7ef4f64230b4fb8e1fb9eb6023`, MIT.
  Source: https://github.com/phoboslab/pl_mpeg
  SPDX identifier in the upstream header; license text in
  `LICENSE-pl_mpeg.txt`.

The decoder headers are unmodified upstream single-header distributions. Nuvora's
freestanding memory hooks and codec configuration live in
`user/media_codecs.c`. The application compiles with SSE2; it checks the
audio/display interfaces at runtime.

- `dr_flac.h`: mackron/dr_libs, revision
  `dfe8377631000664666519fdb83da193fd8037f4`, v0.13.4 development header.
  Source: https://github.com/mackron/dr_libs/blob/dfe8377631000664666519fdb83da193fd8037f4/dr_flac.h
  Dual public-domain/MIT-0 license is included at the end of the header.
  Guest builds use scalar decoding, CRC checking and custom allocation/file callbacks.

Synthetic fixtures contain no third-party recordings: `tone.flac` is a 24-bit
FLAC re-encoding of the existing synthetic `tone.mp3`; `hd.mpg` is a 0.44-second
FFmpeg testsrc2 1280x720/25fps video with a 440 Hz stereo MP2 test tone.
