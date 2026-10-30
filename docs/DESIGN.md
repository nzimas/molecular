# Molecular for the Sonicake Pocket Master: design, version 0.1

A self-playing FM generator and up to five effects, drawn and heard as one molecule. Randomizable at three levels
(the generator's modulation, the effects' values, the whole molecule), played with three controls.

## The device, and what it allows

| | Pocket Master | Where the figure comes from |
|---|---|---|
| CPU | one Andes NDS32 core, up to ~288 MHz, DSP instructions, an FPU | BP1048B2 figures (same chip family; the B1's own not published) |
| RAM | ~320 KB, shared with USB, Bluetooth and the audio buffers | BP1048B2 figure |
| Flash | at least 4 MB, code run in place through a 32 KB cache | stock image layout |
| Audio | 44.1 kHz, 24-bit converters, mono guitar in, stereo out | Sonicake's product page |
| Screen | 1.77" colour LCD; almost certainly 128 x 160 | manual (size); resolution to confirm on the board |
| Controls | left button, right button, VALUE/VOL push encoder (held: power on) | manual |

Budget for version 0.1: **at most ~40 % of the CPU and ~140 KB of RAM** for Molecular's audio, which leaves the rest to
USB, the screen and the unknowns of the board. Both figures are guesses until the port measures them.

## The generator (the nucleus): Tonitor's six-operator FM

Tonitor's engine, unchanged (`core/vendor/tonitor`): msfa (Dexed's core) in integer C, 32 algorithms, and Tonitor's
patch generator with its 22 families. Chosen over Bigorna's 4-op because:
- it is the pure DX7-style engine, and its patch generator already draws evolving, non-instrumental sounds from a seed;
- it is cheap: about 5-6 % of a block per voice on the FM-1's ~160 MHz FPU-less core (Tonitor's measurement), so three
  voices should be well inside the budget on the Pocket Master;
- it is small: a voice 336 bytes, a patch 168, the tables in flash;
- its four live macros (BRIGHTNESS, DETUNE, SATURATION, MOTION) are the generator's modulation targets.
Bigorna's 4-op (`fmt.c`) stays the fallback if the port shows the 6-op too costly: it is roughly two-thirds the work.

**Playing with no keys**: the generator is a drone. A molecule draws one patch (path AMBIENT, any family) and a chord of
three notes (root C1..C3, the others from a few interval sets), held for as long as the molecule lives (`fm_hold`: the
carriers sustained), each voice swelling in over a few seconds.

**Its modulation**: four LFOs, one on each macro: rate 0.02 .. 1.5 Hz, depth, centre, shape (sine, triangle, smooth
random). The left button draws them anew.

## The effects (the atoms)

Sonicake's own effects cannot be reused: they are compiled into the closed stock image, with no symbols and tied to
Sonicake's framework. The MVsilicon SDK's precompiled effect library (reverbs, echo, chorus, pitch and frequency
shifters) runs only on the device, so it stays an option for the port, not the simulator. Version 0.1 uses portable
integer effects that run the same on the Mac and on the device:

| Atom | Symbol | What it does | RAM | Where |
|---|---|---|---|---|
| FILTER | Fl | state-variable low / band / high-pass, cutoff, resonance | ~16 B | inner or edge |
| FOLD | Fo | wavefolder: drive, symmetry | ~8 B | inner or edge |
| RING | Rg | ring modulator: carrier frequency, mix | ~8 B | inner or edge |
| CRUSH | Cr | bit depth and sample-rate reduction | ~8 B | inner or edge |
| CHORUS | Ch | a modulated short delay (46 ms line) | 4 KB | inner or edge |
| DELAY | Dl | Tonitor's master delay: 2 s, 8-bit mu-law, damped feedback | 44 KB | edge only, one at most |
| REVERB | Rv | Tonitor's Dattorro plate at half rate | 43 KB | edge only, one at most |

At most ~100 KB of effect memory in the worst molecule. The right button draws every atom's values anew (not its kind
nor its place).

## The molecule

- A **tree rooted at the generator**, with one to five atoms. The generator binds up to three atoms, an atom up to
  three in all (its parent and two more). DELAY and REVERB are heavy atoms: they bind only their parent, at the
  molecule's edge.
- **Signal flow**: each atom takes its parent's output; an atom with two children feeds both (a branch). The edge
  atoms (leaves) are summed into the stereo output, spread across it, and scaled by 1 / sqrt(number of leaves). DELAY
  and REVERB leaves send their dry input on as well as their wet stereo output.
- **A molecule is a seed**: the topology, the atoms' kinds and values, the patch, the chord and the LFOs are all drawn
  from it, so a molecule can be drawn again bit for bit (and saved as one number later).
- **Drawn as a skeletal formula**: the generator a large circle near the centre, atoms smaller circles with two-letter
  symbols, bonds 44 px long as thick lines at chemistry's angles (an atom's children at 60 degrees either side of its bond, the generator's spread round it). A layout that does not fit the
  screen or overlaps is redrawn from the next seed. Dots travel along the bonds from parent to child, larger when louder, and a
  thin ring circles each atom that is sounding: the signal seen circulating.

## Controls

| Control | On the molecule screen | In the menu | On POWER ON? / POWER OFF? |
|---|---|---|---|
| Encoder turn | volume (the overlay shows it, large) | - | chooses NO or YES |
| Encoder press (short) | a new molecule | opens POWER OFF? | does the choice |
| Encoder press held 0.8 s | opens the menu | closes the menu (back) | - |
| Left button | the generator's LFOs drawn anew | - | - |
| Right button | the atoms' values drawn anew | - | - |

**Powering on.** Off, nothing runs: holding the encoder powers the chip (as the stock firmware's power-on does), and
Molecular boots into **POWER ON?** with NO and YES. The firmware must hold the power on itself from the first moment
(the board's power latch, to be found in the bring-up). YES starts the first molecule; NO, or no answer for 10 s, cuts
the power again. Letting go of the hold that booted the chip answers nothing.

**The menu** (version 0.1): one tile, POWER. A short press on it opens **POWER OFF?** with NO and YES. YES fades the
sound out, shows OFF and cuts the power; NO returns to the molecule, still sounding.

On both questions NO is chosen first, so a stray press is safe. The chosen answer is a filled white box with black
type, the other a white outline with white type (both legible). Closing the menu with a held press, NO first, and the
10 s timeout are my defaults: say if you want something else.

**Readability**: large type only (a 5 x 7 pixel font drawn three times its size: 15 x 21 px characters), white or one
bright colour on black, few words.

## Levels

Random molecules differ by tens of decibels (Tonitor's notes say the same of its families), so the output has a
leveller: the block's peak is measured before its gain is applied, so a loud block is brought down in that block (no
overshoot), and a quiet molecule is lifted over ~0.7 s, by +6 dB at most. Then the volume, then a soft limit above
-2.5 dBFS as a last guard.

## A new molecule without clicks

The audio fades out over ~30 ms; while it is silent the main loop draws the new molecule (the patch is measured
offline, `fm_measure`), clears the delay and reverb lines, and hands the result over; the audio fades in and the
voices swell. The LFOs and atom values change in place (the effects' parameters glide or change between blocks).

## Code layout

```
core/            portable C, integer only, no allocation (the device's code)
  vendor/tonitor  Tonitor's engine, reverb and delay, unchanged
  mol.h / mol.c   the molecule: drawing it from a seed, its layout, the audio through it
  gen.c           the generator: patch, chord, voices, LFOs on the macros
  fx.c            the atoms' effects
  app.c           the controls and screens (a state machine fed by events)
  gfx.c           128 x 160 RGB565 drawing and the large font
sim/sim.c        the Mac simulator: SDL2 window x4, audio, keyboard as the controls
tests/host.c     headless: renders molecules to WAV and screens to PNG, checks levels and budgets
```

## Not in 0.1, and the device port

- No saving, no presets, no MIDI, no guitar input in the sound (the input could later feed the molecule).
- The port needs the board brought up first: the LCD's controller and pins, the buttons' and encoder's pins, the power
  latch (power-off is the firmware's job), the audio codec's path, and a recovery route before the first custom flash.
  The stock image (section `b`) and the MVsilicon SDK are where those come from. Then: measure the CPU per voice and per
  atom on the device, and fix the budget.

## Status (2026-10-30)

Runs in the simulator; the device port has not started. `tests/host.c` passes: 3000 shapes against every rule of the
molecule, 40 molecules for 10 s each (none silent, none at the rails, none running away, half of them shaken halfway
through), every control flow above, and each screen saved for a look (`build/screens`). On the Mac a block takes ~1.4 us
(0.2 % of real time): a relative figure only; the device's must be measured there.
