// matth-x/MicroOcppSimulator
// Copyright Matthias Akstaller 2022 - 2024
// GPL-3.0 License

#include "sim_log.h"

#include <MicroOcpp/Platform.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#define SIM_LOG_CAP (64 * 1024)

static char s_ring[SIM_LOG_CAP];
static size_t s_head = 0;
static size_t s_filled = 0;
static bool s_enabled = true;
static int s_level = MG_LL_INFO;
static struct mg_mgr *s_mgr = nullptr;

// Track live-log listeners by connection id (c->data is owned by dashboard)
#define SIM_LOG_MAX_LIVE 8
static unsigned long s_live_ids[SIM_LOG_MAX_LIVE];
static int s_live_n = 0;

static bool live_has(unsigned long id) {
  for (int i = 0; i < s_live_n; i++) {
    if (s_live_ids[i] == id) return true;
  }
  return false;
}

static void live_add(unsigned long id) {
  if (live_has(id)) return;
  if (s_live_n < SIM_LOG_MAX_LIVE) {
    s_live_ids[s_live_n++] = id;
    return;
  }
  // Slot full: drop ids whose connections are gone
  int w = 0;
  for (int i = 0; i < s_live_n; i++) {
    bool alive = false;
    for (struct mg_connection *c = s_mgr ? s_mgr->conns : nullptr; c;
         c = c->next) {
      if (c->id == s_live_ids[i]) {
        alive = true;
        break;
      }
    }
    if (alive) s_live_ids[w++] = s_live_ids[i];
  }
  s_live_n = w;
  if (s_live_n < SIM_LOG_MAX_LIVE) s_live_ids[s_live_n++] = id;
}

static void ring_putc(char ch) {
  s_ring[s_head] = ch;
  s_head = (s_head + 1) % SIM_LOG_CAP;
  if (s_filled < SIM_LOG_CAP) s_filled++;
}

static void ring_write(const char *s, size_t n) {
  for (size_t i = 0; i < n; i++) ring_putc(s[i]);
}

static void broadcast_live(const char *s, size_t n) {
  if (!s_mgr || !n || s_live_n == 0) return;
  for (struct mg_connection *c = s_mgr->conns; c != NULL; c = c->next) {
    if (live_has(c->id)) {
      mg_http_printf_chunk(c, "%.*s", (int) n, s);
    }
  }
}

static void emit(const char *s, size_t n) {
  if (!s_enabled || !n || !s) return;
  fwrite(s, 1, n, stdout);
  fflush(stdout);
  ring_write(s, n);
  broadcast_live(s, n);
}

static void mg_log_char(char ch, void *param) {
  (void) param;
  emit(&ch, 1);
}

static void mo_console_hook(const char *msg) {
  if (!msg) return;
  emit(msg, strlen(msg));
}

static size_t ring_snapshot(char *out, size_t out_len) {
  if (out_len == 0) return 0;
  size_t n = s_filled;
  if (n > out_len - 1) n = out_len - 1;
  size_t start = (s_filled < SIM_LOG_CAP) ? 0 : s_head;
  for (size_t i = 0; i < n; i++) {
    out[i] = s_ring[(start + i) % SIM_LOG_CAP];
  }
  out[n] = '\0';
  return n;
}

void sim_log_set_enabled(bool enabled) { s_enabled = enabled; }
bool sim_log_is_enabled(void) { return s_enabled; }

void sim_log_set_level(int mg_level) {
  if (mg_level < MG_LL_NONE) mg_level = MG_LL_NONE;
  if (mg_level > MG_LL_VERBOSE) mg_level = MG_LL_VERBOSE;
  s_level = mg_level;
  mg_log_set(mg_level);
}

int sim_log_get_level(void) { return s_level; }

void sim_log_init(struct mg_mgr *mgr) {
  s_mgr = mgr;
  mg_log_set_fn(mg_log_char, NULL);
  mg_log_set(s_level);
#ifdef MO_CUSTOM_CONSOLE
  mocpp_set_console_out(mo_console_hook);
#endif
  emit("[Sim] Live log capture ready\n", 30);
}

void sim_log_http_handler(struct mg_connection *c, int ev, void *ev_data) {
  if (ev != MG_EV_HTTP_MSG) return;
  struct mg_http_message *hm =
      reinterpret_cast<struct mg_http_message *>(ev_data);

  if (mg_match(hm->uri, mg_str("/api/log/static"), NULL)) {
    static char snap[SIM_LOG_CAP + 1];
    size_t n = ring_snapshot(snap, sizeof(snap));
    mg_http_reply(c, 200,
                  "Content-Type: text/plain; charset=utf-8\r\n"
                  "Cache-Control: no-cache\r\n",
                  "%.*s", (int) n, snap);
    return;
  }

  if (mg_match(hm->uri, mg_str("/api/log/live"), NULL)) {
    live_add(c->id);
    mg_printf(c,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/plain; charset=utf-8\r\n"
              "Cache-Control: no-cache\r\n"
              "Transfer-Encoding: chunked\r\n\r\n");
    return;
  }

  mg_http_reply(c, 404, "Content-Type: text/plain\r\n", "not found\n");
}
