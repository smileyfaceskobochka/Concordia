#include "auxilia/toon.hpp"

#define TOON_IMPLEMENTATION
extern "C" {
#include "toon.h"
}

namespace Auxilia {

// ── helpers ────────────────────────────────────────────────────────────────

void toon_doc::log_error(const char *msg) {
  fprintf(stderr, "[toon] %s\n", msg);
}

static toon_value *resolve_get(toon_value *obj, const char *path) {
  if (!obj || obj->type != TOON_OBJECT)
    return nullptr;
  if (!path || !*path)
    return obj;
  const char *dot = strchr(path, '.');
  if (!dot)
    return toon_obj_get(obj, path);
  char buf[256];
  const char *p = path;
  toon_value *cur = obj;
  while (*p) {
    const char *d = strchr(p, '.');
    size_t len = d ? (size_t)(d - p) : strlen(p);
    if (len >= sizeof(buf))
      return nullptr;
    memcpy(buf, p, len);
    buf[len] = '\0';
    if (cur->type != TOON_OBJECT)
      return nullptr;
    cur = toon_obj_get(cur, buf);
    if (!cur)
      return nullptr;
    if (!d)
      break;
    p = d + 1;
  }
  return cur;
}

// ── life cycle ─────────────────────────────────────────────────────────────

toon_doc::toon_doc() : m_root(toon_value_null()) {}

toon_doc::toon_doc(toon_type t) : m_root(toon_value_null()) {
  if (t == TOON_BOOL)
    m_root = toon_value_bool(0);
  else if (t == TOON_NUMBER)
    m_root = toon_value_number(0.0);
  else if (t == TOON_STRING)
    m_root = toon_value_string("");
  else if (t == TOON_ARRAY)
    m_root = toon_value_array();
  else if (t == TOON_OBJECT)
    m_root = toon_value_object();
}

toon_doc::toon_doc(const char *path) : m_root(toon_value_null()) {
  load_file(path);
}

toon_doc::~toon_doc() { toon_value_free(m_root); }

toon_doc::toon_doc(toon_doc &&other) noexcept
    : m_root(other.m_root) {
  other.m_root = nullptr;
}

toon_doc &toon_doc::operator=(toon_doc &&other) noexcept {
  if (this != &other) {
    toon_value_free(m_root);
    m_root = other.m_root;
    other.m_root = nullptr;
  }
  return *this;
}

// ── file I/O ───────────────────────────────────────────────────────────────

bool toon_doc::load_file(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) {
    log_error("Cannot open file");
    return false;
  }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = (char *)malloc((size_t)len + 1);
  if (!buf) {
    fclose(f);
    log_error("malloc failed");
    return false;
  }
  fread(buf, 1, (size_t)len, f);
  buf[len] = '\0';
  fclose(f);

  toon_decoder dec;
  toon_decoder_init(&dec);
  dec.indent_size = 2;
  dec.strict = 1;
  dec.expand_paths = 1;

  toon_value *v = toon_decode(buf, &dec);
  free(buf);

  if (!v) {
    log_error(dec.error);
    toon_decoder_free(&dec);
    return false;
  }

  toon_value_free(m_root);
  m_root = v;
  toon_decoder_free(&dec);
  return true;
}

bool toon_doc::save_file(const char *path,
                          const toon_encoder_opts *opts) const {
  if (!m_root)
    return false;
  toon_encoder_opts enc =
      opts ? *opts : toon_encoder_opts{2, ':', 1, -1};
  char *out = toon_encode(m_root, &enc);
  if (!out) {
    log_error("toon_encode failed");
    return false;
  }
  FILE *f = fopen(path, "w");
  if (!f) {
    log_error("Cannot open file for writing");
    free(out);
    return false;
  }
  fputs(out, f);
  fclose(f);
  free(out);
  return true;
}

// ── type ───────────────────────────────────────────────────────────────────

toon_type toon_doc::type() const {
  return m_root ? m_root->type : TOON_NULL;
}

bool toon_doc::valid() const {
  return m_root && m_root->type != TOON_NULL;
}

// ── inspection ─────────────────────────────────────────────────────────────

bool toon_doc::has(const char *dot_path) const {
  return resolve_get(m_root, dot_path) != nullptr;
}

bool toon_doc::get_bool(const char *dot_path, bool default_val) const {
  toon_value *v = resolve_get(m_root, dot_path);
  if (!v || v->type != TOON_BOOL)
    return default_val;
  return v->bool_val != 0;
}

double toon_doc::get_number(const char *dot_path, double default_val) const {
  toon_value *v = resolve_get(m_root, dot_path);
  if (!v || v->type != TOON_NUMBER)
    return default_val;
  return v->num_val;
}

const char *toon_doc::get_string(const char *dot_path,
                                  const char *default_val) const {
  toon_value *v = resolve_get(m_root, dot_path);
  if (!v || v->type != TOON_STRING)
    return default_val;
  return v->str_val ? v->str_val : default_val;
}

// ── setters ────────────────────────────────────────────────────────────────

void toon_doc::set(const char *dot_path, bool val) {
  if (!m_root || m_root->type != TOON_OBJECT) {
    toon_value_free(m_root);
    m_root = toon_value_object();
  }
  toon_decoder d;
  toon_decoder_init(&d);
  toon_value *slot = toon_obj_set_ex(m_root, dot_path, 1, &d);
  if (slot) {
    toon_value_free_content(slot);
    slot->type = TOON_BOOL;
    slot->bool_val = val ? 1 : 0;
  }
  toon_decoder_free(&d);
}

void toon_doc::set(const char *dot_path, double val) {
  if (!m_root || m_root->type != TOON_OBJECT) {
    toon_value_free(m_root);
    m_root = toon_value_object();
  }
  toon_decoder d;
  toon_decoder_init(&d);
  toon_value *slot = toon_obj_set_ex(m_root, dot_path, 1, &d);
  if (slot) {
    toon_value_free_content(slot);
    slot->type = TOON_NUMBER;
    slot->num_val = val;
  }
  toon_decoder_free(&d);
}

void toon_doc::set(const char *dot_path, const char *val) {
  if (!m_root || m_root->type != TOON_OBJECT) {
    toon_value_free(m_root);
    m_root = toon_value_object();
  }
  toon_decoder d;
  toon_decoder_init(&d);
  toon_value *slot = toon_obj_set_ex(m_root, dot_path, 1, &d);
  if (slot) {
    toon_value_free_content(slot);
    slot->type = TOON_STRING;
    slot->str_val = toon_strdup(val ? val : "");
  }
  toon_decoder_free(&d);
}

// ── containers ─────────────────────────────────────────────────────────────

size_t toon_doc::size() const {
  if (!m_root)
    return 0;
  if (m_root->type == TOON_ARRAY || m_root->type == TOON_OBJECT)
    return m_root->len;
  return 0;
}

toon_value *toon_doc::push() {
  if (!m_root || m_root->type != TOON_ARRAY) {
    toon_value_free(m_root);
    m_root = toon_value_array();
  }
  return toon_array_push(m_root);
}

// ── raw access ─────────────────────────────────────────────────────────────

toon_value *toon_doc::release() {
  toon_value *v = m_root;
  m_root = nullptr;
  return v;
}

toon_value *toon_doc::get() const { return m_root; }

void toon_doc::reset(toon_value *v) {
  toon_value_free(m_root);
  m_root = v;
}

} // namespace Auxilia
