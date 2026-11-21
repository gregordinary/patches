<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# rk3588/ — RK3588 support

Out-of-tree u-boot patches for RK3588 boards. The RK3588 media and NPU kernel
patches are not here: they ride the kernel axis in the `rk3588-accel` series,
drawn from the `media-accel/` and `rocket/` scopes.

## u-boot

Mainline u-boot already brings up an RK3588 board and everything on it that a
flashing session needs — `turing-rk1-rk3588_defconfig` enables the PCIe
controller and the NVMe-over-PCIe driver, and `CONFIG_CMD_BOOTI` selects
`CONFIG_CMD_UNZIP` on arm64, which is what carries `gzwrite`. So these series
are small: one SoC fix the pinned release lacks, then commands, not drivers.

| series | adds | what it is for |
|---|---|---|
| `rk3588-uboot-fixes` | the SPL clock fix | any RK3588 or RK3588S board whose u-boot is otherwise upstream |
| `turing-rk1-recovery` | the SPL clock fix, `ums`, `setexpr` | the u-boot an RK1 image ships with |
| `turing-rk1-util` | recovery + `bootmenu`, `memtest`, `md5sum`, `sha1sum`, `wget`, `read`, `write` | the RK1's recovery and bring-up tool |

The list in each `series` file is the authoritative apply order; the numeric
filename prefixes only make a directory read in that order. A u-boot build
selects one series, so every RK3588 series lists the SoC fixes under `uboot/`
first and its board patches after them.

The SoC fixes touch only SoC-generic u-boot code, so they live at `uboot/`. The
board series patch `configs/turing-rk1-rk3588_defconfig`, not a SoC-generic
defconfig, so their own patches live under `uboot/boards/` and the series carry
the board's name.

## uboot/

SoC-generic u-boot patches, applied by every RK3588 series.

| patch | what it does |
|---|---|
| `0001-clk-rockchip-rk3588-Always-initialize-clocks-in-SPL` | probes the CRU and the secure SCRU at bind in SPL, so SPLL runs at 702 MHz whatever the boot device. Upstream `514663543ecf`, first in v2027.01-rc2 |

### Why the SPL clock fix

SPLL clocks the RK3588's center interconnect, the fabric between the bus
masters (NPU, video codecs, RGA, GPU) and the DDR controllers. Linux models
SPLL as a fixed 702 MHz clock that it never programs, and parents
`aclk_center_root` on it. SPLL sits in the secure bus CRU, which only SPL can
write, and SPL writes it in exactly one place: the probe of the SCRU clock
device. That device probes on demand, when a consumer asks it for a clock.

On an RK3588 board the SD controller's clock is the SCRU's, and the eMMC's is
the main CRU's. SPL probes only the device it boots from, so an SD boot sets
SPLL and an eMMC boot leaves it at the rate the BootROM chose, while Linux's
clock tree still reports 702 MHz.

The fix flags both clock devices to probe at bind in SPL, so SPLL is set before
BL31 runs on every boot device. On a Turing RK1 booted from eMMC it halves the
NPU's DDR read time, from 1.58 ms to 0.80 ms for a 4×2048×4096 int8 read, and
the read time follows the center clock in inverse proportion. The same change
sets the CPU PLLs at bind, which the eMMC boot already reached through the main
CRU.

## uboot/boards/

Board-specific u-boot patches, one directory per board.

### uboot/boards/turing-rk1/

| patch | what it does |
|---|---|
| `0001-export-block-devices-over-usb-with-ums` | `ums` exports any block device u-boot can reach as USB mass storage, so `ums 0 nvme 0` puts the M.2 disk in front of the carrier's host port; also restores the default-y `setexpr` the board defconfig had turned off |
| `0002-enable-the-util-command-set` | the util command set: `bootmenu`, `memtest`, `md5sum`, `sha1sum`, `wget`, `read`, `write` |

### Why `ums` is the whole point

A compute module has no front-panel USB. `usb_host0_xhci` is its only
OTG-capable controller — USB 2.0 high-speed, per the board's upstream DT — and
what sits on the other end is the carrier board's decision. On a cluster
carrier that is a management controller, and such a controller typically knows
how to write the module's eMMC and nothing else, because the loader it streams
into the module only speaks eMMC.

The disk on the M.2 slot behind `pcie3x4` is therefore one u-boot enumerates
perfectly well and no host can address. `ums` is the one command that closes
the gap, and it costs a single Kconfig symbol: `CONFIG_CMD_USB_MASS_STORAGE`
depends only on `BLK` and `USB_GADGET_DOWNLOAD`, both of which the board
defconfig already sets for `rockusb`.

License: GPL-2.0-only (u-boot code); see `LICENSES/` at the repo root.
