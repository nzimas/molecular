# Changelog

Versions are build numbers, as in the other firmwares (build 64 was first called 0.64; the comments in the code
still name the builds that way).

## Build 75: a hundred patches, the menu remembers, FX MORPH
- PATCHES has 100 slots, 0 to 99 (three rows of four on the screen, scrolling to the chosen one). They fit in the
  same 64 KB of flash, seven to a 4 KB sector; slots 0 to 11 are exactly where builds up to 74 kept their twelve, so
  saved patches stay (numbered from 0 now: the old slot 1 is slot 0). A save rewrites its sector through a spare
  sector, marked and checked, so a save cut short by the power is finished at the next start and never touches the
  other slots (the host tests cut the power at each of a save's 111 steps).
- The menu opens on the item last gone into (PATCHES, for instance), and the slots on the slot last chosen.
- SETTINGS has a third page, FX MORPH, 0 to 30 seconds: the right button's new values glide there from where they
  are over that time (switches change halfway; DELAY's and REVERB's values glide too). Saved with the patch; 0, at
  once, for an older patch. A rewiring or MOTION's growing and pruning keep the glide going; MOTION's own value
  glides give way to it.

## Build 74: hardware loops switched on
- The scrambled screen, sound and log after an install: the compiler turns many loops into the chip's hardware loops
  (zero-overhead loops, part of its audio extension), which work only with PSW.AEN set. Nothing in Molecular set it:
  it was what the update loader left, and after an install it was off, so each such loop ran its body once. It is now
  set at the start; the log reports the status word as found and a 16-byte loop tried before and after.

## Build 73: no double precision on the pedal
- The crash on a new physical-model molecule since build 64 (the HARP .. SKIN families): the pedal's FPU is single
  precision only, and the physical models' set-up (core/pm.c) computed in double, a coprocessor exception at its
  first double instruction. It computes in float on the pedal (double on the host), and the build fails if a
  double-precision instruction ever gets into the image again. (Build 71's FPU traps were not the cause.)

## Build 72: USB never stops the pedal
- The freeze when the USB cable goes in (and the scrambled sound after an install): each log packet was sent with
  interrupts masked and a timeout that only an interrupt advances, so while the Mac did not read, the pedal waited
  for good. A packet now goes out only when the last has been taken (a few ms at most, else the line is dropped),
  with interrupts masked only while it is loaded.

## Build 71: no floating-point traps
- A crash on a new molecule (build 70's crash report: a coprocessor exception in the physical models' set-up,
  core/pm.c): the FPU's control register is now set whole at the start (every exception trap off) instead of one bit
  added to what the update loader left. The banded waveguides' modes check their frequency before dividing by it.

## Build 70: the data back below 0x2003C000
- The cause of the scrambled screen, the garbled USB log and the crashes since build 64: the data had grown past
  0x2003C000 (build 63 ended at 0x2003BF34, build 64 at 0x2003DF10), into RAM that is not ours, where the USB, the
  effects link's buffers and the flash driver's state then lay. The atoms' arena is 51 KB instead of 64 (a molecule
  is still drawn to fit; an older patch whose atoms no longer fit has its last ones made the lightest atom), and the
  link fails if the data ever reach that address again.

## Build 69: the stack, and crashes reported live (installed once, with the old layout)
- Two buffers of the build 64 sounds (the physical models' strike, 2 KB, and an effect's analysis window, 1 KB) moved
  off the stack, which on the device is 4 KB with the hardware's bound below it: a strike could go past it and crash.
- At the start the DMA channels the update loader left running are stopped (and listed in the log).
- A crash, or a main loop stopped for 5 s, is reported over USB for good (where, in hex) instead of a hand-over to the
  update loader after 50 s; the log shows the stack's low-water mark.

## Build 65: crash records
- A crash leaves its exception and address in memory that survives the restart, and the next start logs them.

## Build 64: a wider palette
- SETTINGS has a second page, MIX: the effects' share of the sound, 0 .. 100 % (50 to begin with), saved with the
  patch.
- A second synthesis engine beside the FM: physical models (Karplus-Strong strings and tubes, banded waveguides,
  modal bars, bells and membranes) in ten families: HARP, WIRE, SNARE, PIPE, SITAR, GONG, BOWL, BAR, BELL, SKIN,
  struck on the clocks or held by a dust of small strikes.
- Eight more FM families from the literature: TRUAX, HAMMER, TAM, CHIME, FORMANT, GROWL, GLOW, ALLURE (66 families in
  all).
- Effect values drawn towards their bounds' ends two times in five; LFOs now and then at an allure's 3 .. 12 Hz; a new
  molecule never of the family of the last three, and often of another kind; family weights moved towards the rarer
  regions of timbre.
- A diversity lab on the host (tests/lab.c, tools/lab_compare.py): 12 timbre descriptors after IRCAM's Timbre Toolbox
  and the Vendi score. Against build 63: generators alone 35 % more distinct sounds, whole molecules 12 %.

## Build 63: eleven more atoms
- REED (the sound blowing a clarinet), NLF (Dobson's nonlinear filter), SUBHARM (undertones), SPARK (sparks on the
  sound's zero crossings), CONTRAST (spectral contrast and trace), BLUR (the sound's slow spectral shadow), DOPPLER (a
  source on an orbit), GSM (the phone codec), GENDY (Xenakis' stochastic redrawing), INVERT (the spectrum mirrored),
  SQUELCH (undersampled and redrawn). 46 atoms in all.
- The log reports the audio's load (% of real time) and its longest block.

## Build 62: eight new atoms
- SPIRAL (a pitch shifter in a feedback delay), SOURCE (LPC: whisper, buzz, held body), ENVEL (attacks removed or
  sharpened, the envelope inverted), LPG (a Buchla low-pass gate struck by onsets, with uncertainty), BARBER (an endless
  comb), STICK (stick-slip friction), MORPHO (a spectral delay after the Morphophone), DISINTEGRATE (level-driven read
  heads). Kept after the others' numbers, so saved patches load as before.

## Build 61 (2026-11-01): the first public release
- The website, the browser installer (only Molecular's application section is sent) and the logo.

## Build 60: motion as a continuum
- Rewiring, growing and pruning no longer fade the sound: the molecule morphs. Atoms keep their state, a rewired
  atom's input crossfades to its new parent, a grown atom comes in dry, a pruned one leaves the mix before it goes.
- MOTION's new values glide over the time between events.

## Build 59: fluid motion
- Atoms never jump: events keep every atom where it stands; bonds pull like springs, atoms push apart, a gentle pull
  to the middle; grown atoms swell in, pruned ones shrink away.

## Build 58: MOTION and SETTINGS
- Right button held: MOTION, after Vascular's angiogenesis. Left button held: new LFOs.
- The menu: SETTINGS (MOTION RATE, saved with the patch), PATCHES, POWER.

## Build 57
- POWER OFF on USB power keeps the effects processor powered: the sound comes back after powering on again.

## Build 56
- The audio link to the effects processor restarts by itself if it stops.

## Build 55
- PATCHES: a press loads, a hold saves. 40 families of sounds, among them struck metal on unstable clocks.

## Build 54
- The menu (LFO, PATCHES, POWER), patches in flash, the left button rewires; the atoms' own LFOs.

## Build 53
- The effects mixed half wet.

## Build 52
- Norilsk's 25 effects as atoms, with Tonitor's delay and reverb.

## Builds 44 .. 48
- The sound reaches the jack (through the pedal's effects processor); clicks fixed.
