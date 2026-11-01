<p align="center"><img src="docs/assets/logo-dark.svg" alt="Molecular" width="560"></p>

# Molecular

A firmware for the **Sonicake Pocket Master** for the discovery of sound: electroacoustic, acousmatic and
experimental music. Each sound is a molecule, an FM generator bonded to up to five effect atoms, all drawn at
random. You don't set its parameters one by one: you ask for another molecule, rewire this one, draw new values,
set it in motion, and listen to what comes back. It is a conversation with the instrument rather than control of it.

**Website, manual and browser installer: <https://nzimas.github.io/molecular/>**

- **The generator:** two engines, 66 families. Tonitor's six-operator FM (Tonitor's ambient and harsh families, held
  sounds after Norilsk's recipes, struck metal on unstable clocks, recipes from the FM literature) and physical models
  (Karplus-Strong strings and tubes, banded waveguides, modal bars, bells and membranes).
- **The atoms:** 46 effects: Norilsk's 25, Tonitor's delay and reverb, and 19 of its own from the
  electroacoustic studio and beyond (pitch spirals, LPC, envelope inversion, a Buchla low-pass gate, an endless comb,
  friction, a spectral delay, disintegration, a reed, a nonlinear filter, undertones, sparks, spectral contrast and
  blur, Doppler, the GSM codec, Xenakis' GENDY, spectral inversion, squelch). Each atom takes its parent's sound, and
  the outermost atoms are heard where they stand across the screen.
- **Motion:** the molecule moving by itself. This is thermal motion: the atoms drift, the bonds pull like springs.
  It also rearranges itself at a set rate: rewired, atoms grown and pruned, values gliding. Nothing jumps, and the
  sound is a continuum.
- **Patches:** twelve slots in the pedal's flash; a motion rate per patch.
- **Accessible:** few words, very large type, high contrast.

## Installing

Open the [install page](https://nzimas.github.io/molecular/install.html) in a desktop Chrome, Edge or Opera, with
the pedal connected by USB. The page sends only Molecular's application, section `b` of a Sonicake update.
Everything else stays as Sonicake shipped it: the effects processor's firmware, the factory presets and the loader.
Molecular needs the pedal on Sonicake's firmware **1.3.3**, whose effects processor firmware it uses. The same page
reinstalls a Sonicake firmware file that you download from Sonicake.

The pedal's loader (flash_boot) receives every update and is never written by one. An interrupted install can
always be run again.

## Building from source

### The core, the host tests and the simulator

Needs a C compiler, Python 3 and, for the simulator, SDL2 (`brew install sdl2`).

```bash
./build.sh
```

```bash
./build/host
```

```bash
./build/molecular-sim
```

`./build/host` must print `all passed`. It renders 40 molecules and every family, checks the controls' flows, and
checks that MOTION never leaps or fades. It writes every screen as a PNG to `build/screens/`.
`tests/clicks.c` counts clicks in the output, optionally with MOTION running (`build/clicks 30`).

Simulator keys: left and right arrows for the side buttons (hold for their long press), up and down arrows or the
mouse wheel for the encoder, space or return to press it (hold for the menu), escape to quit.

### The device firmware

Needs these, none of them in this tree:
- **MVsilicon's BP10xx SDK** (`bp1048_sdk`, its `MVsB1_Base_SDK`), for the chip's driver library and startup.
- **The Andes `nds32le-elf-mculib-v3f` toolchain** (GCC 4.9.4, ABI2 FP+, as the SDK's libraries). It runs in Docker
  on a Mac:

```bash
docker build --platform linux/amd64 -t sonicake-nds -f tools/toolchain.Dockerfile tools
```

```bash
tools/nds make -C device SDK=/path/to/MVsB1_Base_SDK
```

This builds `build/device/molecular.bin`, the application, linked at 0x10000, where Sonicake's flash_boot runs it.
By default `tools/nds` mounts the folder above this tree, and looks for the toolchain in `toolchain/` there (see the
script).

`device/verify.py` checks an image against the stock application's header (it needs the stock section `b`). To
write the update the web installer sends, as one SysEx a line:

```bash
python3 -I tools/make_update.py build/device/molecular.bin build/device/molecular_update.txt
```

### Installing from the command line (macOS)

`tools/pmmidi/pmmidi.swift` talks to the pedal over CoreMIDI (`swiftc -O tools/pmmidi/pmmidi.swift -o pmmidi`).
To install:

```bash
./pmmidi send F00F0C0001000000020101060FF7
```

That command puts the pedal into its update mode. It comes back on USB as product 0x0285. Then:

```bash
./pmmidi update build/device/molecular_update.txt
```

From a running Molecular, holding both side buttons for 3 seconds also enters update mode. To read Molecular's log:

```bash
./pmmidi send F07D4CF7 3
```

The design is in [docs/DESIGN.md](docs/DESIGN.md). What was learnt about the pedal is in
[docs/hardware/](docs/hardware/): the update protocol, the board, and how the sound reaches the jack through a second
processor.

## Disclaimer

Molecular is an unofficial firmware. It is not made, endorsed or supported by Sonicake. Installing it replaces the
Pocket Master's main program and may void any warranty that applies to your device. The software is provided "as
is", without warranty of any kind, express or implied. In no event shall the authors be liable for any claim,
damages or other liability arising from its use. You install and use it entirely at your own risk. Sonicake and
Pocket Master are trademarks of their owner, used only to say which device Molecular runs on.

## Licence

The firmware, the installer and the tools are GPL-3.0-only (`LICENSE`). The website, the logo and the
documentation's text are MIT. Parts from other projects keep their licences; [LICENSING.md](LICENSING.md) lists them.

Molecular is by Nuno Zimas. If it is useful to you, you can
[support it](https://www.paypal.com/cgi-bin/webscr?cmd=_xclick&business=nzimas.lv%40gmail.com&item_name=Support+Molecular&currency_code=EUR).
