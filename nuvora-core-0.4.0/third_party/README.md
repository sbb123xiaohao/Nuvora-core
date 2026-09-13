# Third-party bootloader materials

The BIOS ISO images contain GNU GRUB 2.12, packaged by Ubuntu as
`grub2` / `2.12-1ubuntu7`. GRUB remains under its own license; it is not
relicensed under the Nuvora source's MIT license.

`GRUB-COPYING` contains the GPL text from the matching GRUB source release.
`grub-source/` provides the corresponding source package used for the Ubuntu
bootloader binaries, including upstream source, Debian/Ubuntu packaging and
patches, and the `.dsc` source manifest:

- `grub2_2.12.orig.tar.xz`
- `grub2_2.12-1ubuntu7.debian.tar.xz`
- `grub2_2.12-1ubuntu7.dsc`

These files were obtained from the Ubuntu Noble source archive under
`pool/main/g/grub2/`, and their SHA-256 digests were checked against the matching
source index. Build instructions and packaging rules are included in that
source package. Nuvora does not modify the GRUB source.

To unpack the complete source package using Debian tooling:

```sh
cd third_party/grub-source
dpkg-source -x grub2_2.12-1ubuntu7.dsc
```

No QEMU binaries, compiler binaries, or Linux kernel sources are included in
this distribution.
