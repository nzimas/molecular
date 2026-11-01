# Molecular licensing

The Molecular **firmware**, the **browser installer** and the **tools** are free software, licensed under the GNU
General Public License, version 3 only (`GPL-3.0-only`, full text in `LICENSE`). If you distribute them, or firmware
built from them, give your recipients the complete corresponding source under the same licence.

The **website, logo and documentation** written for Molecular are under the MIT licence (`LICENSES/MIT.txt`).

| What | Licence | Where |
| --- | --- | --- |
| Molecular's own code: `core/{app,atoms,families,fx,gen,gfx,mol,motion,patch,pm,gsm,molecular}.c` (atoms.c written from published ideas -- Airwindows' Disintegrate, Nikola and ZAcidLowpass, Dan Stowell's Friction, the Synthesis ToolKit's clarinet, Dobson's nonlinear filter, the Eventide H910, the Buchla 292, the Morphophone, GRM Contrast, CDP's blur, Xenakis' GENDY -- no code copied), `core/mol.h`, `device/{main,hal,armlink,usbmidi}.c` and their headers, `device/{pack,verify}.py`, `sim/`, `tests/`, `tools/` (except `make_logo.py`, `make_site.py`) | GPL-3.0-only | |
| The physical models (`core/pm.c`): written from Karplus & Strong, Jaffe & Smith, J. O. Smith's PASP, with the published mode tables of the Synthesis ToolKit's ModalBar and BandedWG (MIT-style) and Faust's physmodels.lib (bell, membrane) as data; no code copied | GPL-3.0-only | `core/pm.c` |
| The diversity lab | GPL-3.0-only | `tests/lab.c`, `tools/lab_compare.py`, `tools/lab_weights.py` |
| The browser installer | GPL-3.0-only | `docs/js/pmota.js`, `docs/js/install.js` |
| The firmware the installer writes, built from this tree with the MVsilicon SDK's libraries (below) | GPL-3.0-only | `docs/firmware/` |
| From **Tonitor** (Nuno Zimas), unchanged: the FM engine, delay, reverb, table generator | GPL-3.0-only | `core/vendor/tonitor/` (`PROVENANCE.md`), `tools/gen_tables.py` |
| msfa (Google Inc., Pascal Gauthier), from Dexed, ported to integer C by Leo Kuroshita for Felucca | Apache-2.0 | `core/vendor/tonitor/fm6_core.c`, `LICENSES/Apache-2.0-msfa.txt` |
| From **Norilsk** (Nuno Zimas), unchanged: its 25 effects. Some follow published designs or the ideas of other projects (Mutable Instruments, Airwindows: MIT, ideas only, no code copied; Noisferatu, Trevor Wishart, Julian Parker and others, credited in the file) | GPL-3.0-only | `core/vendor/norilsk/` (`PROVENANCE.md`), `LICENSES/MIT-rings.txt` |
| The update protocol and the board, as learnt by Molecular's author from the pedal and Sonicake's Manager | GPL-3.0-only (the tools), MIT (the notes) | `tools/{htfw,sonicake_proto}.py`, `docs/hardware/` |
| From the **MVsilicon BP10xx SDK**: the linker script (`device/app.ld`) and the start-up code (`device/startup/init.c`), changed where marked; the SDK's driver library and its other start-up files are linked into the firmware at build time, and are not in this tree | MVsilicon's; the SDK states no licence | `device/app.ld`, `device/startup/init.c` |
| A factory preset's settings for the effects processor (`device/preset_sweet_od.h`), read from Sonicake's 1.3.3 firmware: what the pedal's effects processor needs to be sent before it passes sound | Sonicake's (data, kept for interoperability) | `device/preset_sweet_od.h` |
| The GSM 06.10 codec (Jutta Degener, Carsten Bormann, TU Berlin), from Vascular's vendored copy, unchanged | permissive: keep its notice | `core/vendor/gsm/` (`COPYRIGHT`), wrapped by `core/gsm.c` |
| The website, the logo, the documentation's text | MIT | `docs/` (except the rows above), `site/`, `tools/{make_logo,make_site}.py`, `README.md`, `CHANGELOG.md`, `LICENSES/MIT.txt` |
| `LICENSES/BSD-LIRA-8.txt` | kept with Tonitor's licence files; nothing from LIRA-8 is in Molecular | |

No Sonicake firmware is distributed, apart from the one preset's settings above: the installer sends Molecular's
application only, and Sonicake's own firmware is reinstalled from a file the user downloads from Sonicake.
