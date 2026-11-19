# Mono SRR Chorus

Two parts of the Elektron Monomachine in a row, as a CLAP effect for Bitwig
(macOS, universal binary): a track's sample-rate reduction (SRR), then the
chorus of the SWAVE ENS machine.

The DSP is a fixed-point C port of both, from Monomachine OS 1.32B, written
from its DSP56300 code and checked sample for sample against that code
running in an emulator. No firmware code or data is in this repository: its
tables are generated from formulas at start-up.

## Controls

| Parameter    | Range | On the Monomachine | What it does |
|--------------|-------|--------------------|--------------|
| SRR          | 0–127 | EFX page, SRR | Takes the signal down to 44.1 kHz / (1 + 0.1172 × SRR) and back: 39.47 kHz at 1, 22.8 kHz at 8, 5.19 kHz at 64, 2.78 kHz at 127. 0 is off. Whole steps only. |
| Chorus Level | 0–127 | ENS's SYN page, CHRL | The chorus's wet level; the dry signal falls by the same amount (about −42 dB at 127). |
| Chorus Width | 0–127 | ENS's SYN page, CHRW | How much of each side's chorus voice goes to the other side: all of it at 0 (the wet is the same on both sides), half at 127. |

The defaults are the hardware's: SRR 0, Chorus Level 0 and Chorus Width
127. So a new instance passes the signal through unchanged until you raise
SRR or Chorus Level. The display shows SRR's rate in kHz. All parameters can
be automated and modulated in Bitwig.

## How it behaves

This is what the firmware does; the plug-in keeps all of it.

- **SRR is interpolation, not sample-and-hold.** Each 16-sample block, it
  reads the input every `step` samples (1 + 0.1172 × SRR), between samples
  by linear interpolation, starting from a phase carried over from the last
  block. It then reads those samples back at 1/`step` intervals, again by
  linear interpolation, to make 16 samples. Nothing filters the signal
  before it is reduced, so it aliases, but the reconstruction is a ramp
  between samples rather than a staircase.
- **SRR delays the signal** by `step` + 1 samples: 2 at SRR 0 (where it is
  just that delay), 9.5 at 64, 16.9 at 127.
- **Two firmware details are kept.** The phase carried to the next block
  keeps only half of its fraction, so the reduced samples do not fall on an
  even grid across blocks. At SRR 0 the phase halves every block.
- **Lowering SRR can glitch.** When SRR drops, the carried phase can be
  further on than the new, shorter step. The next block then starts reading
  below its down-rate buffer: a few samples after a small drop, up to the
  whole block after a large one. Some of those reads land in the input
  buffers (the left side can pick up a right-hand sample), some outside
  SRR's memory altogether: on the unit, in memory the track's other stages
  use; here, silence (see Differences).
- **The chorus.** Left plus right (half each) goes into a delay of 1,024
  samples. Two taps read it at 400 ± 340 samples (1.4 to 16.8 ms), swept by
  a 0.2 Hz LFO, the two a quarter turn apart. Left gets the first tap and
  right the second, each at Chorus Level. Each side also gets the other's tap
  at 1 − Chorus Width / 2. Chorus Level is smoothed, 5 % of the way per
  16-sample block.
- **PAN at its centre.** On the unit the chorus's wet also follows the
  track's PAN. The plug-in holds PAN at 64, whose quarter-sine is a hair off
  centre: the right side's wet is 0.11 dB louder than the left's (0.7114
  against 0.7022).
- **Knobs.** Parameter changes reach the DSP the way the Monomachine's CPU
  sends a knob: every 384 samples the value moves a quarter of the way to
  where you set it. Modulation is added on top, unsmoothed, every 48 samples,
  as the unit's LFOs are.
- **Clipping.** Every stage stores its result as a 24-bit word, so anything
  above full scale clips hard. The Monomachine itself has no other clipper:
  its track outputs and its output buses are clipped the same way.

## Differences from the hardware

- **Not a single place on the unit.** SRR sits on every track, after the
  amp (VOL, PAN, envelope). The chorus exists only on SWAVE ENS tracks, as a
  hook the machine runs right after SRR. The plug-in is these two stages
  alone: the host's audio goes in where the amp section's output is, and the
  chorus's output comes out. There is no machine, EQ, filter, amp or delay.
- **What SRR reads after it drops.** In the case above, the firmware reads
  the working memory of the track's filter, EQ and delay. The plug-in has
  none of those stages, so it reads silence there. Only the block right
  after SRR drops can be affected (dropping to 0 included); while SRR holds
  or rises, it never is.
- **Start.** The chorus mutes its wet for the first 64 blocks (23 ms) after
  ENS is assigned to a track. The plug-in runs those blocks on silence
  whenever it starts or is reset, so it is ready at once. Its LFO therefore
  starts 64 blocks into its cycle.
- **Other sample rates.** Both stages run at 44.1 kHz, the unit's rate. At
  other rates the plug-in resamples in and out (windowed sinc), so the output
  is no longer bit-exact and is limited to 20.9 kHz.
- **Levels.** 0 dBFS in the plug-in is full scale at the amp section's
  output.

Latency is 18 samples at 44.1 kHz (a 16-sample block, plus SRR's 2 samples
at SRR 0), 110 at 48 kHz and 218 at 96 kHz. It is reported to the host, and
at the other rates the resampling is shifted so that the reported latency is
exact to the sample. SRR above 0 adds up to 15 more samples of its own
(`step` − 1), which the plug-in does not report, since they change with SRR.

## Accuracy

The reference is Monomachine OS 1.32B running in the DSP56300 emulator of
[Monomodule](https://github.com/shnolk/monomodule), with the controls built
by Monomodule's model of the Monomachine's CPU. An ENS track ran in the
emulator, and its amp section's output was overwritten with the test input
before SRR read it. It has not been compared with a physical Monomachine.

| Check | Result |
|-------|--------|
| Core vs. firmware: 400,000 blocks over 8 seeds. Knob moves through the CPU model, and raw control words (SRR anywhere from 0 to $7F7FFF, Chorus Level and Width from 0 to $7FFFFF). Noise at random levels, random samples at the two rails, DC at both rails, sines, impulses, silence | SRR's output, SRR's state (phase and last two samples) and the chorus's output identical after every block |
| The 99 of those blocks in which SRR read outside its buffers after a drop | identical when the emulator's memory there is cleared, as the plug-in models it; with that memory left as it was, 40 of them differ in their first samples |
| Built plug-in code vs. firmware: knob moves through the CPU model and modulation (7,756 moves, 1,884 of them modulation, in 240,000 blocks after the start; the other stages' memory cleared, as above) | every output sample identical |
| SRR's two tables (256 entries) and the two pan words vs. firmware memory | identical |

`tests/test_core.c` pins this without the firmware. Its expected hashes were
produced by feeding the same stimulus into the firmware, which the core
matched.

## Build and install

```bash
./build_macos.sh
```

```bash
cp -R "build/Mono SRR Chorus.clap" ~/Library/Audio/Plug-Ins/CLAP/
```

The build script needs Xcode's command-line tools.

## Tests

```bash
cc -std=c99 -O2 -Wno-unused-function tests/test_core.c -lm -o tests/test_core && tests/test_core
```

```bash
xcrun clang -std=c11 -O2 -Wno-unused-function -Wno-missing-field-initializers -I vendor/clap/include tests/clap_test.c -o tests/clap_test && tests/clap_test "build/Mono SRR Chorus.clap"
```

`test_core` checks the tables and 12,000 blocks of output against the
firmware-derived hashes; its stimulus also lowers SRR far enough to read
outside its buffers. `clap_test` loads the built plug-in and checks:

- at 44.1 kHz, output equal to the core's after its 64 silent blocks;
- with SRR 0 and Chorus Level 0, the input arrives after the reported
  latency at 44.1, 48 and 96 kHz;
- the tail, state and the parameter text.

The plug-in also passes clap-trap's `validate` (at 44.1, 48 and 96 kHz) and
`state --roundtrip`, and the tests pass under AddressSanitizer and
UndefinedBehaviorSanitizer and as an Intel build under Rosetta.

## Source

- `source/mono_srr_chorus.h`: SRR and the chorus (C99 and libm).
- `source/mono_srr_chorus_clap.c`: the CLAP plug-in around them.
- `vendor/clap`: CLAP 1.2 headers (MIT).

## License

GPL-3.0-only. The DSP is an independent reimplementation of parts of the
Monomachine, derived from analysis of Elektron's firmware; no firmware is
included. Monomachine and Elektron are trademarks of Elektron Music
Machines; this project is not affiliated with Elektron.
