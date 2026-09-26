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

Both files are unmodified upstream single-header distributions. Nuvora's
freestanding memory hooks and codec configuration live in
`user/media_codecs.c`. The application compiles with SSE2; it checks the
audio/display interfaces at runtime.
