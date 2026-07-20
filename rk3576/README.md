<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# rk3576/ — RK3576 u-boot support

Out-of-tree u-boot patches for the RK3576: recovery paths back into USB flashing, a
USB host stack, and an HDMI display stack, on top of mainline u-boot.

Three profiles select subsets of one series. They differ by what the first USB
controller (`drd0`) does and whether the build autoboots — u-boot has no runtime OTG
role switch on these nodes, so one build cannot be both a device gadget and a host:

| profile | drd0 | display | autoboots | what it is for |
|---|---|---|---|---|
| `rk3576-loader` | device (rockusb/ums) | no | — | flash and dump driven from a laptop |
| `rk3576-console` | host | yes | never | bring-up tool, streamed into RAM over maskrom |
| `rk3576-display` | host | yes | 10 s | the u-boot an image ships with |

The list in each `profile.toml` is the authoritative apply order; the numeric filename
prefixes only make the directory read in that order. Two files share prefix `0013` — they
are alternatives at the same point in the series, and no profile takes both.

## uboot/

| patch | what it does |
|---|---|
| `0001-rockchip-enter-rockusb-loader-mode-on-BOOT_LOADER-re` | `reboot loader` from Linux comes back up in rockusb mode — the software path back to USB flashing |
| `0002-rockchip-rk3576-generic-enable-SARADC-download-key` | grounding the recovery button at boot enters BootROM download mode — the hardware path, works when Linux will not boot |
| `0003-rockchip-match-the-SARADC-by-driver-not-by-node-name` | the download key's ADC lookup finds the RK3576 SARADC at all; upstream matches by DT node name, which no SoC since rk3568 uses |
| `0004-rockchip-rk3576-generic-build-the-maskrom-USB-boot-i` | binman emits the CODE471/CODE472 payloads, so this u-boot can run from RAM with nothing written to storage |
| `0005-arm64-emit-the-current-phase-s-text-base-in-_TEXT_BA` | each phase advertises the text base it is linked at, which the BootROM honours when placing the CODE472 download |
| `0006-rockchip-rk3576-generic-enable-USB-host-storage-and-` | USB host, mass storage, and keyboard; drd0 forced to `dr_mode = "host"` |
| `0007-clk-rockchip-rk3576-handle-the-HDMITX-VO0-and-HDPTX-` | HDMITX/VO0/HDPTX clock get/set/parent in the CRU driver |
| `0008-power-domain-add-a-Rockchip-RK3576-PMU-power-domain-` | the PMU power-domain driver, for PD_VO0 |
| `0009-phy-rockchip-add-Samsung-HDPTX-HDMI-TMDS-PHY-driver` | the Samsung HDPTX TMDS PHY (ROPLL config, PLL_LOCK_DONE poll) |
| `0010-video-rockchip-add-DW-HDMI-QP-bridge-for-RK3576` | the DW-HDMI-QP bridge, as UCLASS_DISPLAY |
| `0011-video-rockchip-add-VOP2-display-controller-for-RK357` | the VOP2 controller (VP0 + Esmart0), which also quiesces itself at OS handoff |
| `0012-rockchip-rk3576-generic-enable-the-HDMI-display-stac` | display-stack defconfig, the vop/hdmi/hdmiphy DT nodes and port graph |
| `0013-rockchip-rk3576-generic-drop-to-the-u-boot-prompt-by` | never autoboot (`bootdelay -1`) — the console profile's alternative at 0013 |
| `0013-rockchip-rk3576-generic-give-autoboot-an-interruptib` | autoboot after an interruptible delay — the display profile's alternative at 0013 |
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

License: GPL-2.0-only (u-boot code); see `LICENSES/` at the repo root.
