# Changelog

Versions are build numbers, as in the other firmwares (build 64 was first called 0.64; the comments in the code
still name the builds that way).

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
