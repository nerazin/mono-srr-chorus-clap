/* Loads the built plug-in like a host and checks what the core test cannot:
 * the wrapper around mono_srr_chorus.h. usage: clap_test "build/Mono SRR Chorus.clap"
 *  - at 44.1 kHz the output is the core's after its 64 silent blocks, 16
 *    frames late, exactly
 *  - at 44.1, 48 and 96 kHz, with SRR 0 and Chorus Level 0, the output is the
 *    input, latency frames late (exact to 1 LSB at 44.1 kHz)
 *  - the tail, state round trip, rejected bad states, parameter text */
#include <clap/clap.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../source/mono_srr_chorus.h"

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); puts(""); exit(1); } } while (0)
#define N (192 * 512)   /* whole process calls */

typedef struct { clap_input_events_t api; clap_event_param_value_t v[SR_CONTROLS]; uint32_t n; } Events;
static uint32_t ev_size(const clap_input_events_t *e) { return ((const Events *)e->ctx)->n; }
static const clap_event_header_t *ev_get(const clap_input_events_t *e, uint32_t i) { return &((const Events *)e->ctx)->v[i].header; }
static const void *host_ext(const clap_host_t *h, const char *id) { (void)h; (void)id; return NULL; }
static void host_cb(const clap_host_t *h) { (void)h; }
static const clap_host_t host = {CLAP_VERSION_INIT, NULL, "test", "", "", "1", host_ext, host_cb, host_cb, host_cb};
static const clap_plugin_factory_t *factory;

typedef struct { uint8_t data[256]; uint64_t size, pos; } Buffer;
static int64_t buf_write(const clap_ostream_t *s, const void *p, uint64_t n) { Buffer *b = s->ctx; if (b->size + n > sizeof b->data) return -1; memcpy(b->data + b->size, p, n); b->size += n; return (int64_t)n; }
static int64_t buf_read(const clap_istream_t *s, void *p, uint64_t n) { Buffer *b = s->ctx; if (n > b->size - b->pos) n = b->size - b->pos; memcpy(p, b->data + b->pos, n); b->pos += n; return (int64_t)n; }

/* A plug-in with these parameter values (SRR, Chorus Level, Chorus Width), active at rate. */
static const clap_plugin_t *make(const double v[SR_CONTROLS], double rate) {
   const clap_plugin_t *p = factory->create_plugin(factory, &host, "works.reconstruction.monosrrchorus");
   CHECK(p && p->init(p), "create");
   const clap_plugin_params_t *params = p->get_extension(p, CLAP_EXT_PARAMS);
   Events ev = {{&ev, ev_size, ev_get}, {{{0}}}, SR_CONTROLS};
   for (uint32_t i = 0; i < SR_CONTROLS; ++i)
      ev.v[i] = (clap_event_param_value_t){{sizeof ev.v[i], 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0}, i, NULL, -1, -1, -1, -1, v[i]};
   ev.api.ctx = &ev;
   params->flush(p, &ev.api, NULL);
   CHECK(p->activate(p, rate, 1, 512) && p->start_processing(p), "activate");
   return p;
}
/* Runs N frames of in through p, 512 at a time. */
static void run(const clap_plugin_t *p, double in[2][N], double out[2][N]) {
   Events ev = {{NULL, ev_size, ev_get}, {{{0}}}, 0};
   ev.api.ctx = &ev;
   for (uint32_t at = 0; at < N; at += 512) {
      double *i[2] = {in[0] + at, in[1] + at}, *o[2] = {out[0] + at, out[1] + at};
      clap_audio_buffer_t ai = {NULL, i, 2, 0, 0}, ao = {NULL, o, 2, 0, 0};
      clap_process_t pr = {0};
      pr.frames_count = 512; pr.audio_inputs = &ai; pr.audio_outputs = &ao;
      pr.audio_inputs_count = pr.audio_outputs_count = 1; pr.in_events = &ev.api;
      CHECK(p->process(p, &pr) == CLAP_PROCESS_CONTINUE, "process");
   }
}
static uint32_t latency_of(const clap_plugin_t *p) { return ((const clap_plugin_latency_t *)p->get_extension(p, CLAP_EXT_LATENCY))->get(p); }
static uint32_t tail_of(const clap_plugin_t *p) { return ((const clap_plugin_tail_t *)p->get_extension(p, CLAP_EXT_TAIL))->get(p); }
static void done(const clap_plugin_t *p) { p->stop_processing(p); p->deactivate(p); p->destroy(p); }

static double in[2][N], out[2][N];
int main(int argc, char **argv) {
   CHECK(argc == 2, "usage: clap_test plugin.clap");
   char path[1024];
   snprintf(path, sizeof path, "%s/Contents/MacOS/Mono SRR Chorus", argv[1]);
   void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
   CHECK(lib, "%s", dlerror());
   const clap_plugin_entry_t *entry = dlsym(lib, "clap_entry");
   CHECK(entry && entry->init(path), "entry");
   factory = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
   sr_tables();
   uint32_t seed = 7;
   for (int c = 0; c < 2; ++c)
      for (int n = 0; n < N; ++n) { seed = seed * 1664525u + 1013904223u; in[c][n] = (double)((int32_t)seed >> 9) / 8388608.0; }

   /* 44.1 kHz: the core itself */
   const double sets[3][SR_CONTROLS] = {{0, 0, 127}, {127, 127, 0}, {37, 90, 64}};
   for (int t = 0; t < 3; ++t) {
      const clap_plugin_t *p = make(sets[t], 44100);
      CHECK(latency_of(p) == 18, "latency %u", latency_of(p));
      run(p, in, out);
      done(p);
      static MonoSrrChorus m;
      int32_t ctl[SR_CONTROLS];
      for (int k = 0; k < SR_CONTROLS; ++k) ctl[k] = (int32_t)sets[t][k] << 16;
      sr_reset(&m);
      for (int b = 0; b < SR_GATE; ++b) {
         int32_t z[2][16] = {{0}};
         sr_block(&m, ctl, z);
      }
      for (int at = 0; at + 32 <= N; at += 16) {
         int32_t x[2][16];
         for (int c = 0; c < 2; ++c) for (int n = 0; n < 16; ++n) x[c][n] = (int32_t)lrint(in[c][at + n] * 8388608.0);
         sr_block(&m, ctl, x);
         for (int c = 0; c < 2; ++c) for (int n = 0; n < 16; ++n)
            CHECK(out[c][at + 16 + n] == x[c][n] / 8388608.0, "set %d frame %d: %.9f, core %.9f", t, at + 16 + n, out[c][at + 16 + n], x[c][n] / 8388608.0);
      }
   }
   puts("PASS 44.1 kHz output is the core's after its 64 silent blocks, bit for bit (default, extreme and middle settings)");

   /* SRR 0 and Chorus Level 0: the input, latency frames late. Band-limited input, so the resampler passes it. */
   for (int c = 0; c < 2; ++c)
      for (int n = 0; n < N; ++n) in[c][n] = 0.3 * sin(n * (0.011 + 0.02 * c)) + 0.2 * sin(n * 0.31) + 0.1 * sin(n * (1.1 + 0.1 * c));
   const double rates[3] = {44100, 48000, 96000};
   for (int r = 0; r < 3; ++r) {
      const double v[SR_CONTROLS] = {0, 0, 127};
      const clap_plugin_t *p = make(v, rates[r]);
      const uint32_t lat = latency_of(p), tail = tail_of(p);
      run(p, in, out);
      done(p);
      double worst = 0;
      for (int c = 0; c < 2; ++c) for (uint32_t n = 4096; n + lat < N; ++n) worst = fmax(worst, fabs(out[c][n + lat] - in[c][n]));
      CHECK(worst <= (r ? 1e-3 : 1.5 / 8388608.0), "dry at %.0f Hz is %g off", rates[r], worst);
      CHECK(tail == lat + (uint32_t)ceil(1024 * rates[r] / 44100), "tail %u at %.0f Hz", tail, rates[r]);
      printf("PASS dry signal at %.0f Hz arrives after the reported latency (%u frames, worst error %.1f dB)\n", rates[r], lat, 20 * log10(worst));
   }

   /* state and text */
   {
      const double v[SR_CONTROLS] = {10, 20.5, 30};
      const clap_plugin_t *a = make(v, 44100), *b = make((const double[SR_CONTROLS]){0}, 44100);
      const clap_plugin_state_t *sa = a->get_extension(a, CLAP_EXT_STATE), *sb = b->get_extension(b, CLAP_EXT_STATE);
      Buffer buf = {{0}, 0, 0};
      clap_ostream_t os = {&buf, buf_write};
      clap_istream_t is = {&buf, buf_read};
      CHECK(sa->save(a, &os) && sb->load(b, &is), "state");
      const clap_plugin_params_t *pb = b->get_extension(b, CLAP_EXT_PARAMS);
      for (clap_id i = 0; i < SR_CONTROLS; ++i) { double x; CHECK(pb->get_value(b, i, &x) && x == v[i], "param %u after load", i); }
      buf.size -= 8; buf.pos = 0;
      CHECK(!sb->load(b, &is), "a short state must be refused");
      buf.size += 8; buf.data[0] = 'X'; buf.pos = 0;
      CHECK(!sb->load(b, &is), "a bad state must be refused");
      static const struct { clap_id id; double v; const char *text; } want[] = {
         {SR_SRR, 0, "0.0 (off)"}, {SR_SRR, 1, "1.0 (39.47 kHz)"}, {SR_SRR, 64, "64.0 (5.19 kHz)"},
         {SR_SRR, 127, "127.0 (2.78 kHz)"}, {SR_CHRL, 64, "64.0"}, {SR_CHRW, 127, "127.0"}};
      char text[64];
      for (unsigned i = 0; i < sizeof want / sizeof want[0]; ++i)
         CHECK(pb->value_to_text(b, want[i].id, want[i].v, text, sizeof text) && !strcmp(text, want[i].text), "text of %u at %g: \"%s\", want \"%s\"", want[i].id, want[i].v, text, want[i].text);
      double x;
      CHECK(pb->text_to_value(b, SR_SRR, "100", &x) && x == 100, "from text %f", x);
      done(a); done(b);
      puts("PASS state round trip, bad states refused, parameter text");
   }
   entry->deinit();
   return 0;
}
