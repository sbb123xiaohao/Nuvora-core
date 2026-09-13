-----BEGIN PGP SIGNED MESSAGE-----
Hash: SHA512

Format: 3.0 (quilt)
Source: grub2
Binary: grub2, grub-linuxbios, grub-efi, grub-common, grub2-common, grub-emu, grub-emu-dbg, grub-pc-bin, grub-pc-dbg, grub-pc, grub-rescue-pc, grub-coreboot-bin, grub-coreboot-dbg, grub-coreboot, grub-efi-ia32-bin, grub-efi-ia32-dbg, grub-efi-ia32, grub-efi-ia32-signed-template, grub-efi-amd64-bin, grub-efi-amd64-dbg, grub-efi-amd64, grub-efi-amd64-signed-template, grub-efi-ia64-bin, grub-efi-ia64-dbg, grub-efi-ia64, grub-efi-arm-bin, grub-efi-arm-dbg, grub-efi-arm, grub-efi-arm64-bin, grub-efi-arm64-dbg, grub-efi-arm64, grub-efi-arm64-signed-template, grub-efi-riscv64-bin, grub-efi-riscv64-dbg, grub-efi-riscv64, grub-ieee1275-bin, grub-ieee1275-dbg, grub-ieee1275, grub-firmware-qemu, grub-uboot-bin, grub-uboot-dbg, grub-uboot, grub-xen-bin, grub-xen-dbg, grub-xen, grub-xen-host, grub-yeeloong-bin, grub-yeeloong-dbg, grub-yeeloong, grub-theme-starfield, grub-mount-udeb
Architecture: any
Version: 2.12-1ubuntu7
Maintainer: Ubuntu Developers <ubuntu-devel-discuss@lists.ubuntu.com>
Uploaders: Felix Zielcke <fzielcke@z-51.de>, Jordi Mallach <jordi@debian.org>, Steve McIntyre <93sam@debian.org>, Julian Andres Klode <jak@debian.org>, Mate Kukri <mate.kukri@canonical.com>
Homepage: https://www.gnu.org/software/grub/
Standards-Version: 3.9.6
Vcs-Browser: https://git.launchpad.net/~ubuntu-core-dev/grub/+git/ubuntu
Vcs-Git: https://git.launchpad.net/~ubuntu-core-dev/grub/+git/ubuntu
Build-Depends: debhelper-compat (= 13), patchutils, python3, python3-apt, python3-pytest, flex, bison, gawk, po-debconf, help2man, texinfo, gcc-multilib [i386 kopensolaris-i386 any-amd64 any-ppc64 any-sparc], xfonts-unifont, libfreetype6-dev, gettext, libdevmapper-dev [linux-any], libgeom-dev (>= 8.2+ds1-1~) [kfreebsd-any] | libgeom-dev (<< 8.2) [kfreebsd-any], libsdl2-dev [!hurd-any], xorriso, qemu-system [i386 kfreebsd-i386 kopensolaris-i386 any-amd64], cpio [i386 kopensolaris-i386 amd64 x32], parted [!hurd-any], libfuse3-dev [linux-any kfreebsd-any], fonts-dejavu-core, liblzma-dev, liblzo2-dev, lzop, dosfstools [any-i386 any-amd64 any-arm64], squashfs-tools [any-i386 any-amd64 any-arm64], wamerican, libparted-dev [any-powerpc any-ppc64 any-ppc64el], pkg-config, bash-completion, libefiboot-dev [i386 amd64 ia64 x32 armel armhf arm64 riscv64], libefivar-dev [i386 amd64 ia64 x32 armel armhf arm64 riscv64]
Build-Conflicts: autoconf2.13, libnvpair-dev, libzfs-dev
Package-List:
 grub-common deb admin optional arch=any
 grub-coreboot deb admin optional arch=any-i386,any-amd64
 grub-coreboot-bin deb admin optional arch=any-i386,any-amd64
 grub-coreboot-dbg deb debug optional arch=any-i386,any-amd64
 grub-efi deb admin optional arch=any-i386,any-amd64,any-arm64,any-ia64,any-arm,any-riscv64
 grub-efi-amd64 deb admin optional arch=kopensolaris-i386,any-amd64
 grub-efi-amd64-bin deb admin optional arch=kopensolaris-i386,any-amd64
 grub-efi-amd64-dbg deb debug optional arch=kopensolaris-i386,any-amd64
 grub-efi-amd64-signed-template deb admin optional arch=amd64
 grub-efi-arm deb admin optional arch=any-arm
 grub-efi-arm-bin deb admin optional arch=any-arm
 grub-efi-arm-dbg deb debug optional arch=any-arm
 grub-efi-arm64 deb admin optional arch=any-arm64
 grub-efi-arm64-bin deb admin optional arch=any-arm64
 grub-efi-arm64-dbg deb debug optional arch=any-arm64
 grub-efi-arm64-signed-template deb admin optional arch=arm64
 grub-efi-ia32 deb admin optional arch=any-i386,any-amd64
 grub-efi-ia32-bin deb admin optional arch=any-i386,any-amd64
 grub-efi-ia32-dbg deb debug optional arch=any-i386,any-amd64
 grub-efi-ia32-signed-template deb admin optional arch=i386
 grub-efi-ia64 deb admin optional arch=any-ia64
 grub-efi-ia64-bin deb admin optional arch=any-ia64
 grub-efi-ia64-dbg deb debug optional arch=any-ia64
 grub-efi-riscv64 deb admin optional arch=any-riscv64
 grub-efi-riscv64-bin deb admin optional arch=any-riscv64
 grub-efi-riscv64-dbg deb debug optional arch=any-riscv64
 grub-emu deb admin optional arch=any-i386,any-amd64,any-powerpc
 grub-emu-dbg deb debug optional arch=any-i386,any-amd64,any-powerpc
 grub-firmware-qemu deb admin optional arch=any-i386,any-amd64
 grub-ieee1275 deb admin optional arch=any-i386,any-amd64,any-powerpc,any-ppc64,any-ppc64el,any-sparc,any-sparc64
 grub-ieee1275-bin deb admin optional arch=any-i386,any-amd64,any-powerpc,any-ppc64,any-ppc64el,any-sparc,any-sparc64
 grub-ieee1275-dbg deb debug optional arch=any-i386,any-amd64,any-powerpc,any-ppc64,any-ppc64el,any-sparc,any-sparc64
 grub-linuxbios deb oldlibs optional arch=any-i386,any-amd64
 grub-mount-udeb udeb debian-installer optional arch=linux-any,kfreebsd-any
 grub-pc deb admin optional arch=any-i386,any-amd64
 grub-pc-bin deb admin optional arch=any-i386,any-amd64
 grub-pc-dbg deb debug optional arch=any-i386,any-amd64
 grub-rescue-pc deb admin optional arch=any-i386,any-amd64
 grub-theme-starfield deb admin optional arch=any-i386,any-amd64,any-powerpc,any-ppc64,any-ppc64el,any-sparc,any-sparc64,any-mipsel,any-ia64,any-arm,any-arm64,any-riscv64
 grub-uboot deb admin optional arch=any-arm
 grub-uboot-bin deb admin optional arch=any-arm
 grub-uboot-dbg deb debug optional arch=any-arm
 grub-xen deb admin optional arch=i386,amd64
 grub-xen-bin deb admin optional arch=i386,amd64
 grub-xen-dbg deb debug optional arch=i386,amd64
 grub-xen-host deb admin optional arch=i386,amd64
 grub-yeeloong deb admin optional arch=any-mipsel
 grub-yeeloong-bin deb admin optional arch=any-mipsel
 grub-yeeloong-dbg deb debug optional arch=any-mipsel
 grub2 deb oldlibs optional arch=any-i386,any-amd64,any-powerpc,any-ppc64,any-ppc64el,any-sparc,any-sparc64
 grub2-common deb admin optional arch=any-i386,any-amd64,any-powerpc,any-ppc64,any-ppc64el,any-sparc,any-sparc64,any-mipsel,any-ia64,any-arm,any-arm64,any-riscv64
Checksums-Sha1:
 9a5cd9860a02d479ff65461b710a4d85ea46b9f4 6675608 grub2_2.12.orig.tar.xz
 1aebf0b8b858baf17aada16cafd0406dcb148766 1145160 grub2_2.12-1ubuntu7.debian.tar.xz
Checksums-Sha256:
 f3c97391f7c4eaa677a78e090c7e97e6dc47b16f655f04683ebd37bef7fe0faa 6675608 grub2_2.12.orig.tar.xz
 5abd39c37eaa0dc08ca5e4ccbaa25e3462e0fb13ed7156b375f7ba805f16e09d 1145160 grub2_2.12-1ubuntu7.debian.tar.xz
Files:
 60c564b1bdc39d8e43b3aab4bc0fb140 6675608 grub2_2.12.orig.tar.xz
 925f96cf93579f65e821343a6ec1eaef 1145160 grub2_2.12-1ubuntu7.debian.tar.xz
Original-Maintainer: GRUB Maintainers <pkg-grub-devel@alioth-lists.debian.net>

-----BEGIN PGP SIGNATURE-----

iQJNBAEBCgA3FiEElmwl2ANNHob/r9FalkCpSGVxka4FAmYOfaIZHG1hdGUua3Vr
cmlAY2Fub25pY2FsLmNvbQAKCRCWQKlIZXGRrhjaD/wMBfXbRfimEJB9Y28xI+L7
6hEh+dNe0PPwEHxU8zahapHquYfxTWmW7Oc5CFN0ZXbzqrqg9igmV7/1KREoeJ0l
HEEZp6nhfVtw3NioanMVdI0CRb4OEcz2qc0xKIBe/LsC2Uu4KNG/TjC/Ykc/hyMe
lsKg9XZEIgCXJvEXCQ0iQMv2YdjxqDQJKc+cq9ZcMnx+KTjKuyT5SIyIABlGArMA
Zty4GGsm5rnsnEPcntdPSVJr61xckPvSemJUm4wZjbHoJV4gXKj44xOA4hdBlcl6
G2siAD8ArZFoZXI0SEyDjNsi6ke5911pTZwmHY8xwJXYzWCnKB7Rfx66993t6uB7
sltFd/dP9tBrwKigPacLXQ0huNQh/4OEyhyaasGLhnMs6rvuzhwHWZVj+9Udmo2a
kJ3ll1vJsBZRphHFGF0zOFWqAXVOSaaAGSCxYfXhs+q+qQnRsXpasXh5aPNqSBvp
LRllPlYkmDiLm7pQg0XcP7VM4SbQeq3uFR5H6/Md5SwIz7BxwlgO2lmfDHPYNa0x
bIO8j5eaTe2tBFdqbFGa/Lu6nSG3nJ1WsaUMLcqhmhmZRsMAS7T2aFJpV6J0f4AI
Omf8xZhomuzeJ/rd28hZ+Lx5bb/3YWPu2TuZ+b36u4Q4rGMq8EWvyoyF0x16gtHV
wX+blFSbyr/QHGv+XSCmFw==
=kMJF
-----END PGP SIGNATURE-----
