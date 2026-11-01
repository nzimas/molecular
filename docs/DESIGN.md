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

**Playing with no keys**: a molecule draws one family, its patch and a chord of three notes (root E1..E3, the others
from a few interval sets). A held family's chord sounds for as long as the molecule lives (`fm_hold`: the carriers
sustained), each voice swelling in over a few seconds; a struck family's notes are struck on a clock.

**The families** (0.55, `core/families.c`): 40, by weight:
- Tonitor's 19 gentle families on its AMBIENT path, as before (about 37 % of molecules);
- Tonitor's harsher five on its open path, no longer softened: BRUIT, FRICTION, SHARDS, STATIC, GLITCH (9 %);
- eight held ones after Norilsk (24 %): HUM (carriers nailed to 46 .. 63 Hz and 100 .. 158 Hz under bell partials),
  SWARM (high detuned harmonics, buzzing), ORGAN (drawbars), MODES (a bar's, plate's, bell's or hull's modes, each
  smeared by a noise operator), STEAM (a closed pipe's odd partials, hissing), SCRAPE (high bell ratios chopped by a
  sample-and-hold), CORONA (a high shimmer), ASH (a drone an octave down under a dusty crackle);
- eight struck ones after Norilsk's "metal struck, never a drum" (29 %): ANVIL, STAMP, RIVET, SHEET on the SCATTER
  clock; GEARS, PISTON, RATCHET, FORGE on the MACHINE clock.
Norilsk's waves, grains and resonant bodies are not in this engine: its families that are nothing else (CLOUD,
PULSAR, FREEZE, the found sounds ..) are left out; the others stand in operators for them (a body's modes as carriers
on its ratios, noise as an operator fed back hard, a square as a modulator at twice the carrier).

**The clocks** (0.55, `gen.c`, after Norilsk's `mo_strikes`): each strike takes the next voice and a note of the chord,
restarts its envelopes from where they are (no drop to silence first) and lets it go after 20 .. 60 ms; a burst of
coloured noise goes with it (60 dB down in 1 .. 40 ms, dark to bright).
- MACHINE: a pulse at 0.25 .. 18 a second, each strike early or late, the pace wandering; an accent pattern; now and then
  a bounce; PISTON a hiss of steam halfway between strikes.
- SCATTER: no pulse to count: each interval the wandering mean up or down by up to two octaves; clusters of quick
  strikes; long pauses; soft strikes more often than hard ones, and darker.

**Its modulation**: four LFOs, one on each macro: rate 0.02 .. 1.5 Hz, depth, centre, shape (sine, triangle, smooth
random). The menu's LFO draws them anew, with the atoms' LFOs.

## The effects (the atoms)

Since 0.52 the atoms are **Norilsk's 25 effects** (core/vendor/norilsk/effects.c, copied unchanged from Norilsk's
rack.c by tools/vendor_norilsk.py) and **Tonitor's DELAY and REVERB**. Each has four values, 0 .. 100, drawn within
Norilsk's bounds for it; each atom's output passes Norilsk's guard (a peak rider to half scale). MODAL, STRING and
BANK are tuned to the generator's root.

| Family (colour) | Atoms (symbol) |
|---|---|
| filters, formants (cyan) | FILTER Fl, VOWEL Vo |
| drive, wave shaping (orange) | FOLD Fo, MULAW Mu, SCREAM Sc, STARVE Sv, CHEB Cb, SLEW Sw, XOR Xo, DUST Ds, WAVESET Ws |
| shifting, cross-modulation (pink) | SHIFT Sh, RING Ri, XMOD Xm, CHAOS Cs |
| resonators (green) | MODAL Mo, STRING St, BANK Bk |
| time, texture (violet) | TAPE Ta, REVERSE Rs, GLITCH Gl, CLOUD Cl, SMEAR Sm, DIFFUSE Df |
| space (pale blue) | SPRING Sp, DELAY Dl (Tonitor's, 44 KB), REVERB Rv (Tonitor's plate, 43 KB) |

DELAY and REVERB are heavy atoms: edge only, one each at most. The others share one 64 KB arena (fx.c FX_ARENA); a
molecule is drawn so that its atoms fit (TAPE and REVERSE, the largest, take ~20 KB). The right button draws every
atom's values anew (not its kind nor its place).

**The atoms' LFOs** (0.54): each atom but DELAY and REVERB has one LFO on one of its values, at the generator's rates
and shapes, swinging it up to half its bounds' span either way and kept within those bounds (Norilsk's modulation
does the same, mod.c). Switches and coarse steps are never moved (FILTER's type, BANK's set, XOR's operation ..:
fx.c FX_LFO_OK): BANK's set, moved, clicked 2000 times in 10 s. A quarter of the LFOs are still.

## The molecule

- A **tree rooted at the generator**, with one to five atoms. The generator binds up to three atoms, an atom up to
  three in all (its parent and two more). DELAY and REVERB are heavy atoms: they bind only their parent, at the
  molecule's edge.
- **Signal flow**: each atom takes its parent's output; an atom with two children feeds both (a branch). The edge
  atoms (leaves) are summed into the stereo output, spread across it, and scaled by 1 / sqrt(number of leaves). DELAY
  and REVERB leaves send their dry input on as well as their wet stereo output. The atoms' tree is half the output: the generator's own sound, centred, is the other half (MOL_WET in mol.c).
- **A molecule is a seed**: the topology, the atoms' kinds and values, the patch, the chord and the LFOs are all drawn
  from it, so a molecule can be drawn again bit for bit (and saved as one number later).
- **Drawn as a skeletal formula**: the generator a large circle near the centre, atoms smaller circles with two-letter
  symbols, bonds 44 px long as thick lines at chemistry's angles (an atom's children at 60 degrees either side of its bond, the generator's spread round it). A layout that does not fit the
  screen or overlaps is redrawn from the next seed. Dots travel along the bonds from parent to child, larger when louder, and a
  thin ring circles each atom that is sounding: the signal seen circulating.

## Controls

| Control | On the molecule screen | In the menu | On POWER ON? / POWER OFF? |
|---|---|---|---|
| Encoder turn | volume (the overlay shows it, large) | the next item; in PATCHES, the next slot; in SETTINGS, the value | chooses NO or YES |
| Encoder press (short) | a new molecule | does the item; in PATCHES, loads the slot | does the choice |
| Encoder press held 0.8 s | opens the menu | back to the molecule; in PATCHES, saves to the slot | - |
| Left button | the molecule rewired; held 0.8 s: the LFOs anew (0.58) | in PATCHES, SETTINGS: back | - |
| Right button | the atoms' values drawn anew; held 0.8 s: MOTION on or off (0.58) | in PATCHES, SETTINGS: back | - |

The side buttons act when let go, or at 0.8 s held; both held together, neither (both held 3 s: update mode).

**Powering on.** Off, nothing runs: holding the encoder powers the chip (as the stock firmware's power-on does), and
Molecular boots into **POWER ON?** with NO and YES. The firmware must hold the power on itself from the first moment
(the board's power latch, to be found in the bring-up). YES starts the first molecule; NO, or no answer for 10 s, cuts
the power again. Letting go of the hold that booted the chip answers nothing.

**The menu** (0.54): one item at a time, its colour round the screen, a picture and its word, three dots below for
where it is: **SETTINGS**, **PATCHES**, **POWER** (it opens at SETTINGS; turning stops at either end). (0.54 .. 0.57
had LFO first; 0.58 moved it to the left button held.)
- **SETTINGS** (0.58): the patch's own settings, saved with it. The first: **MOTION RATE**, MOTION's events a minute,
  Vascular's steps: 6, 10, 15, 20, 30, 45, 67, 90, 120 (20 to begin with; a new molecule keeps the rate). Turn: the
  value; a press, a held press or a side button: back.
- **PATCHES**: twelve numbered slots, four a row; an empty slot is outlined, a filled one solid white, the chosen one
  ringed in yellow. A short press loads the slot's patch (LOADED 6; an empty slot says EMPTY 6 and stays). Held, the
  slot saves the molecule as it sounds now (shape, values, LFOs, the generator's patch, chord and clock), a filled slot
  written over (SAVED 6, or FAILED 6). Either side button leaves.
- **POWER**: **POWER OFF?** with NO and YES. YES fades the sound out, shows OFF and cuts the power; NO returns to the
  molecule, still sounding.

**The left button rewires** (0.54): the same atoms, with their values and LFOs, bound anew by the molecule's rules
(DELAY and REVERB still at the edge) and laid out again, through a short fade; the generator, DELAY and REVERB carry
on. A molecule whose atoms allow one wiring only (one atom; or only DELAY and REVERB) says NO REWIRE.

**MOTION** (0.58, fluid since 0.59; core/motion.c), after Vascular's angiogenesis: the right button held starts it,
held again stops it (STILL). Nothing is ever placed, everything moves there: every disc wanders on a smoothly turning
heading; the bonds pull like springs towards their length, crowded discs push apart, the edges turn them back, and a
gentle pull keeps the molecule off the corners. Each edge atom's stereo place follows it across (its gains glide).
MOTION RATE times a minute one event, drawn as Vascular draws them: rewired (2 in 8: the same atoms, each where it
stands; the new bonds draw them together over a second or two), an atom grown (2 in 8: it swells from its parent's
side; else pruned or rewired), an atom pruned (1 in 8: it shrinks away first; never the last), one atom's values anew
(3 in 8). The generator is never touched. The sound moves with the picture (0.60): nothing is faded or cleared. A changed shape morphs: each atom keeps its
state; one bound to another parent crossfades its input from the old to the new over a second; a grown atom comes in
dry and turns wet; a pruned one turns dry and leaves the mix before it goes; every node's share of the output glides
to what the new shape gives it. One atom's values anew glide there from where they are, over a whole interval
between events (at least 1.5 s), so the values are always on their way; only the values an LFO may move (a switch or
a coarse step would jump), and DELAY's and REVERB's not at all. The left button's rewiring morphs too. The
drift's pace follows the rate: 0.4 px a second per event a minute (8 px/s at 20), each disc 0.6 .. 1.4 of it.
(0.58 laid the molecule out anew at each event: it leapt.)

**Patches in flash**: a 4 KB sector a slot from 0x170000 (room for 16), past the application (device/verify.py keeps
the image below it) and outside every range the stock firmware erases. A record holds a version, so a slot written by
a later layout reads as empty; an older one, shorter (a version only adds to the end of the record), loads with what
it lacks zero (a 0.54 patch: held, no clock).

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
  families.c      the generator's 40 families: Tonitor's, and sixteen after Norilsk
  gen.c           the generator: patch, chord, voices, the clocks, LFOs on the macros
  fx.c            the atoms' effects
  patch.c         the patches: the molecule saved to a slot (the platform keeps them)
  app.c           the controls and screens (a state machine fed by events)
  gfx.c           128 x 160 RGB565 drawing and the large font
sim/sim.c        the Mac simulator: SDL2 window x4, audio, keyboard as the controls
tests/host.c     headless: renders molecules to WAV and screens to PNG, checks levels and budgets
```

## Not in 0.1, and the device port

- No saving, no presets, no MIDI, no guitar input in the sound (the input could later feed the molecule).
- The board is mapped from the stock firmware (docs/hardware/BOARD.md): ST7735S 160 x 128 landscape on SPI, keys B6 A15 A31,
  encoder A29 A30, backlight PWM A10, DAC0 for the headphones. The screens were relaid out for landscape.
- Still to do on the device: measure the CPU per voice and per atom, then move the audio code into RAM (Tonitor's
  FAST) if needed; USB-MIDI (so the Manager reaches Molecular directly, and `11 6F` works as in the stock firmware).

## Status (2026-10-30)

Runs in the simulator; the device firmware builds (`device/`, notes/BOARD.md one level up for the board) and has not been installed. `tests/host.c` passes: 3000 shapes against every rule of the
molecule, 40 molecules for 10 s each (none silent, none at the rails, none running away, half of them shaken halfway
through), every control flow above, and each screen saved for a look (`build/screens`). On the Mac a block takes ~1.4 us
(0.2 % of real time): a relative figure only; the device's must be measured there.
