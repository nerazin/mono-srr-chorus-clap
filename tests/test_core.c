/* Regression test for mono_srr_chorus.h that needs no firmware.
 *
 * The expected hashes come from Monomachine OS 1.32B itself: the tables were
 * read from the emulated DSP's memory, and the same stimulus was fed into the
 * SRR stage of an ENS track running in an emulator, the chorus following (see
 * the README). The stimulus starts with the chorus's 64-block gate, moves
 * every control often, including to the ends of its range and down far
 * enough that SRR reads past its own buffers, and drives the input to full
 * scale. For that run the emulator's memory of the track's other stages was
 * cleared, as the core models it. */
#include <stdio.h>
#include "../source/mono_srr_chorus.h"

static uint32_t seed = 1;
static uint32_t rnd(uint32_t n) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % n; }

/* Controls and input of one block. */
static void stimulus(int block, int32_t ctl[SR_CONTROLS], int32_t in[2][SR_FRAMES]) {
   static int level = 8, kind = 0;
   static const int32_t defaults[SR_CONTROLS] = {0, 0, 127};
   if (block == 0)
      for (int k = 0; k < SR_CONTROLS; ++k) ctl[k] = defaults[k] << 16;
   else if (block % 37 == 0) {                      /* knob-like moves, also between steps and to the ends */
      const int k = (int)rnd(SR_CONTROLS);
      const int32_t top = k == SR_SRR ? 0x7f0000 : 0x7fffff;
      ctl[k] = rnd(4) == 0 ? (rnd(2) ? top : 0) : (int32_t)rnd(0x7f0000);
      level = 8 + (int)rnd(8);                      /* full scale down to -42 dB */
      kind = (int)rnd(5);
   }
   for (int c = 0; c < 2; ++c)
      for (int n = 0; n < SR_FRAMES; ++n) {
         seed = seed * 1664525u + 1013904223u;
         in[c][n] = kind == 0 ? 0 : kind == 1 ? ((seed >> 31) ? SR_ONE : -SR_ONE - 1) : (int32_t)seed >> level;
      }
}

static uint64_t hash = 1469598103934665603ULL;
static void mix(const int32_t *v, int n) {
   for (int i = 0; i < n; ++i)
      for (int b = 0; b < 4; ++b) hash = (hash ^ (((uint32_t)v[i] >> (8 * b)) & 0xff)) * 1099511628211ULL;
}

#define TEST_BLOCKS 12000

#ifndef TEST_CORE_NO_MAIN
int main(void) {
   static MonoSrrChorus s;
   sr_tables();
   mix(sr_step, 128); mix(sr_gain, 128); mix(sr_pan, 2);
   const uint64_t tables = hash;
   hash = 1469598103934665603ULL;
   sr_reset(&s);
   int32_t ctl[SR_CONTROLS], io[2][SR_FRAMES];
   for (int block = 0; block < TEST_BLOCKS; ++block) {
      stimulus(block, ctl, io);
      sr_block(&s, ctl, io);
      mix(io[0], SR_FRAMES); mix(io[1], SR_FRAMES);
   }
   const uint64_t want_tables = 0xb657036920de1cc6ULL, want_audio = 0xff8221e5e751984dULL;
   printf("tables %016llx audio %016llx\n", (unsigned long long)tables, (unsigned long long)hash);
   if (tables != want_tables || hash != want_audio) { puts("FAIL: output differs from the firmware"); return 1; }
   puts("PASS core tables and audio match the firmware");
   return 0;
}
#endif
