<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# uboot-host/ — u-boot host-build fixes

Fixes to u-boot's **host-side** build tooling — the programs and bindings that run
on the build machine, not on the board. They are SoC-independent, so every series
that builds u-boot carries them ahead of its own patches.

That is what separates this scope from `rk3576/uboot` and `rk3588/uboot`: those
describe a SoC or a board, and a patch in one has no meaning for the other. A host
tool that will not compile stops every build equally, whatever the target.

## u-boot

| patch | what it fixes |
|---|---|
| `uboot/0001-scripts-dtc-pylibfdt-use-the-Python-3-C-API-in-the-t` | `scripts/dtc/pylibfdt` will not compile against SWIG 4.4 or newer. Two typemaps in `libfdt.i` call `PyString_FromString()` and `PyInt_AsLong()`, which exist only in Python 2; the build worked because SWIG's `pyhead.swg` defined them as macros onto the Python 3 spellings, and SWIG 4.4 removed that compatibility layer. The generated `libfdt_wrap.c` then fails with implicit declarations and the `-Wint-conversion` errors that follow, taking out `scripts_dtc` before the build reaches a board. Calls the Python 3 names directly, unguarded, since pylibfdt is Python 3 only. |

Debian `forky` ships SWIG 4.5.0, so this is the state of a current build host rather
than a future one. The fix is upstream-shaped and belongs in u-boot; carried here
until it lands there.
