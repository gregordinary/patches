<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# rk3576/ — RK3576 support

Patches the RK3576 needs on top of mainline: a kernel-fix series (`rk3576-fixes`)
and out-of-tree u-boot series (recovery/USB-flash, USB host, HDMI display).

## kernel/

Standalone fixes for the RK3576's upstream drivers, applied on top of a mainline
kernel by the `rk3576-fixes` series. Written to upstream standards (mbox format)
and meant to leave this repo once they reach the stable series the series targets.

| patch | what it fixes |
|---|---|
| `kernel/100-pmdomain-rockchip-rk3576-gpu-regulator` | GPU power-domain SError panic. `RK3576_PD_GPU` is fed by `vdd_gpu_s0` but, unlike the RK3588 GPU domain, was never flagged as needing a regulator, so the domain is powered before the rail is enabled. A deferred panfrost probe then races the regulator core's disable-unused pass: with the boot-on rail torn down the domain never acks (`failed to get ack on domain 'gpu'`) and panfrost's soft-reset faults the unpowered block (`SError` → panic). Flags the GPU domain `need_regulator`, as RK3588 already does. |
| `kernel/101-arm64-dts-rk3576-gpu-power-domain-label` | adds a `pd_gpu` label to the RK3576 GPU power-domain node so a board can attach `vdd_gpu_s0` with `domain-supply` (the H96 board DTS does). |
| `kernel/102-arm64-dts-rk3576-cpu-caches` | describes the CPU caches, which the SoC dtsi omits entirely — the kernel reports `cacheinfo: Unable to detect cache hierarchy for CPU 0` and leaves `/sys/devices/system/cpu/cpu*/cache` empty. L1 per core plus a per-cluster unified L2, using the TRM's geometry for the A53 and A72 clusters. |

When upstreaming 100, the in-tree RK3576 boards with a GPU rail (e.g. armsom-sige5)
should also gain `domain-supply = <&vdd_gpu_s0>` in the same series, so they use the
real rail rather than a dummy regulator.

## u-boot

Out-of-tree u-boot patches for the RK3576: recovery paths back into USB flashing, a
USB host stack, and an HDMI display stack, on top of mainline u-boot.

Three series select subsets of one patch set. `drd0` (the USB 3.0 OTG port) is the
rockusb/ums device port in all three; the USB host runs on `drd1` — u-boot has no
runtime OTG role switch on these nodes, so a single controller cannot be both a host
and a gadget, and splitting the roles across the two controllers gives both at once.
The series differ by how far each goes beyond booting:

| series | drd0 | host + display | autoboots | what it is for |
|---|---|---|---|---|
| `rk3576-loader` | device (rockusb/ums) | no | — | flash and dump driven from a laptop |
| `rk3576-display` | device (rockusb/ums) | yes | 10 s | the u-boot an image ships with |
| `rk3576-util` | device (rockusb/ums) | yes | never | recovery/bring-up tool: boot menu, diagnostics, verification |

The list in each `series` file is the authoritative apply order; the numeric filename
prefixes only make the directory read in that order. Two files share prefix `0013` — they
are alternatives at the same point in the apply order, and no series takes both.

Those three are SoC-generic: they patch only the `rk3576-generic` control DTB and
defconfig, so their payload is identical on any RK3576 board. A board that needs
board-specific u-boot support gets its own series that layers a board patch (from
`uboot/boards/<board>/`) on a SoC-generic series:

| series | base | adds |
|---|---|---|
| `h96-max-m9-util` | `rk3576-util` | the H96 MAX M9's GMAC0 ethernet (`dhcp`/`tftp`/`ping`) |

## uboot/

| patch | what it does |
|---|---|
| `0001-rockchip-enter-rockusb-loader-mode-on-BOOT_LOADER-re` | `reboot loader` from Linux comes back up in rockusb mode — the software path back to USB flashing |
| `0002-rockchip-rk3576-generic-enable-SARADC-download-key` | grounding the recovery button at boot enters BootROM download mode — the hardware path, works when Linux will not boot |
| `0003-rockchip-match-the-SARADC-by-driver-not-by-node-name` | the download key's ADC lookup finds the RK3576 SARADC at all; upstream matches by DT node name, which no SoC since rk3568 uses |
| `0025-rockchip-widen-the-download-key-ADC-window-for-12-bi` | the pressed key actually registers: the raw 0..30 window was sized for 10-bit SARADCs, but RK3576's 12-bit ADC reads a press at ~41, so the window is widened to 100 |
| `0004-rockchip-rk3576-generic-build-the-maskrom-USB-boot-i` | binman emits the CODE471/CODE472 payloads, so this u-boot can run from RAM with nothing written to storage |
| `0005-arm64-emit-the-current-phase-s-text-base-in-_TEXT_BA` | each phase advertises the text base it is linked at, which the BootROM honours when placing the CODE472 download |
| `0006-rockchip-rk3576-generic-enable-USB-host-storage-and-` | USB host, mass storage, and keyboard; host runs on drd1, drd0 stays the rockusb/ums device port; `USE_PREBOOT` auto-runs `usb start` before the prompt, so the keyboard works with no UART |
| `0007-clk-rockchip-rk3576-handle-the-HDMITX-VO0-and-HDPTX-` | HDMITX/VO0/HDPTX clock get/set/parent in the CRU driver |
| `0008-power-domain-add-a-Rockchip-RK3576-PMU-power-domain-` | the PMU power-domain driver, for PD_VO0 |
| `0009-phy-rockchip-add-Samsung-HDPTX-HDMI-TMDS-PHY-driver` | the Samsung HDPTX TMDS PHY (ROPLL config, PLL_LOCK_DONE poll) |
| `0010-video-rockchip-add-DW-HDMI-QP-bridge-for-RK3576` | the DW-HDMI-QP bridge, as UCLASS_DISPLAY |
| `0011-video-rockchip-add-VOP2-display-controller-for-RK357` | the VOP2 controller (VP0 + Esmart0), which also quiesces itself at OS handoff |
| `0012-rockchip-rk3576-generic-enable-the-HDMI-display-stac` | display-stack defconfig, the vop/hdmi/hdmiphy DT nodes and port graph |
| `0013-rockchip-rk3576-generic-drop-to-the-u-boot-prompt-by` | never autoboot (`bootdelay -1`) — the util series' alternative at 0013 |
| `0013-rockchip-rk3576-generic-give-autoboot-an-interruptib` | autoboot after an interruptible delay — the display series' alternative at 0013 |
| `0014-rockchip-rk3576-generic-enable-the-smc-command` | the `smc` developer command |
| `0015-rockchip-rk3576-generic-enable-the-cache-commands` | the cache developer commands |
| `0016-rockchip-rk3576-mux-the-console-over-serial-and-vidc` | the default console muxed over serial + vidconsole, so the panel shows it with no `setenv` |
| `0017-phy-rockchip-inno-usb2-mirror-the-kernel-s-RK3576-PH` | kernel-mirrored inno-usb2 bring-up; its CRU reset clears the BootROM's device-mode session state, otherwise read as a phantom host-port connect |
| `0018-rockchip-rk3576-generic-run-USB-host-on-the-second-c` | host runs on drd1, the upstream-proven path, leaving the USB 3.0 port free for the maskrom cable |
| `0019-clk-rockchip-rk3576-report-the-USB3-OTG-reference-cl` | CRU reports CLK_REF_USB3OTG0/1 as 24 MHz; without it dwc3 computes a garbage reference period and every controller timer runs ~10x slow |
| `0020-usb-hub-poll-for-port-reset-completion-instead-of-re` | the port-reset loop arms once and polls, instead of re-arming a nearly-enabled port back into reset forever |
| `0021-usb-dwc3-mark-the-generic-host-as-DMA-active-for-OS-` | `DM_FLAG_ACTIVE_DMA` so bootm halts the xhci before the OS jump; a live event ring otherwise corrupts the loaded initrd and FDT |
| `0022-clk-rockchip-rk3576-run-the-SoC-clock-bring-up-at-SP` | the SoC clock bring-up runs at SPL bind under `CONFIG_XPL_BUILD`; the old `CONFIG_SPL_BUILD` guard was dead code after the xPL rename |
| `0023-clk-rockchip-rk3576-keep-the-CNTPCT-source-on-the-24` | pins the ARM generic counter to the 24 MHz oscillator; its HP-timer clock muxes CPLL, which 0022 puts in normal mode |
| `0024-rockchip-rk3576-generic-enable-the-util-command-set` | the util command set: `bootmenu`, `clk`, `memtest`, `md5sum`, `sha1sum` |

## uboot/boards/

Board-specific u-boot patches, one directory per board. Unlike the SoC-generic series
above, these carry values true only of one board (PHY address, reset GPIO, RGMII delay),
so they ship only in that board's series, never in a SoC-generic one.

### uboot/boards/h96-max-m9/

| patch | what it does |
|---|---|
| `0001-enable-gmac0-ethernet` | the H96 MAX M9's GMAC0 RGMII ethernet: enables the MAC/PHY DT nodes (RTL8211F at MDIO 1, reset gpio2 PB3, `tx_delay 0x1b`, `rgmii-rxid`) and the driver, PHY, and net commands (`dhcp`, `ping`, `mii`), dropping the generic defconfig's `CONFIG_NO_NET` |

License: GPL-2.0-only (kernel and u-boot code); see `LICENSES/` at the repo root.
