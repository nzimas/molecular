# Molecular (Sonicake Pocket Master)

A self-playing FM generator and up to five effects, drawn and heard as one molecule, for the Sonicake Pocket Master.
Its generator is Tonitor's six-operator FM engine (msfa, as Felucca ported it to integer C). Version 0.1 runs in a
Mac simulator; the device port waits on the board's bring-up. The design is in [docs/DESIGN.md](docs/DESIGN.md).

## Build and run

Needs a C compiler, Python 3 and SDL2 (`brew install sdl2`).

```bash
./build.sh
```

```bash
./build/host
```

```bash
./build/molecular-sim
```

The host tests write WAVs to `build/` and every screen as a PNG to `build/screens/`.

## The simulator's keys

| Key | The Pocket Master |
|---|---|
| left arrow | left button: the generator's LFOs anew |
| right arrow | right button: the atoms' values anew |
| up / down arrows, mouse wheel | the encoder turned: volume, or NO / YES |
| space or return | the encoder pressed: a new molecule; held 0.8 s: the menu |
| escape | quit |

It starts at POWER ON? (as the device would once the encoder has been held). Powered off, the window goes dark; hold
space to be asked again.

## Licence

GPL-3.0-only, as Tonitor; `core/vendor/tonitor/fm6_core.c` is msfa (Apache-2.0). See LICENSE and LICENSES/.
