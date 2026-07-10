#define CTOON_IMPLEMENTATION
#include "auxilia/ctoon.hpp"

namespace Auxilia {

// ── helpers ────────────────────────────────────────────────────────────────

void ctoon_doc::log_error(const char *msg) {
  fprintf(stderr, "[ctoon] %s\n", msg);
}

static ctoon_value *resolve_get(ctoon_value *obj, const char *path) {
  if (!obj || obj->type != CTOON_OBJECT)
    return nullptr;
  if (!path || !*path)
    return obj;
  const char *dot = strchr(path, '.');
  if (!dot)
    return ctoon_obj_get(obj, path);
  char buf[256];
  const char *p = path;
  ctoon_value *cur = obj;
  while (*p) {
    const char *d = strchr(p, '.');
    size_t len = d ? (size_t)(d - p) : strlen(p);
    if (len >= sizeof(buf))
      return nullptr;
    memcpy(buf, p, len);
    buf[len] = '\0';
    if (cur->type != CTOON_OBJECT)
      return nullptr;
    cur = ctoon_obj_get(cur, buf);
    if (!cur)
      return nullptr;
    if (!d)
      break;
    p = d + 1;
  }
  return cur;
}

// ── life cycle ─────────────────────────────────────────────────────────────

ctoon_doc::ctoon_doc() : m_root(ctoon_value_null()) {}

ctoon_doc::ctoon_doc(ctoon_type t) : m_root(nullptr) {
  if (t == CTOON_BOOL)
    m_root = ctoon_value_bool(0);
  else if (t == CTOON_NUMBER)
    m_root = ctoon_value_number(0.0);
  else if (t == CTOON_STRING)
    m_root = ctoon_value_string("");
  else if (t == CTOON_ARRAY)
    m_root = ctoon_value_array();
  else if (t == CTOON_OBJECT)
    m_root = ctoon_value_object();
  else
    m_root = ctoon_value_null();
}

ctoon_doc::ctoon_doc(const char *path) : m_root(ctoon_value_null()) {
  load_file(path);
}

ctoon_doc::~ctoon_doc() { ctoon_value_free(m_root); }

ctoon_doc::ctoon_doc(ctoon_doc &&other) noexcept
    : m_root(other.m_root) {
  other.m_root = nullptr;
}

ctoon_doc &ctoon_doc::operator=(ctoon_doc &&other) noexcept {
  if (this != &other) {
    ctoon_value_free(m_root);
    m_root = other.m_root;
    other.m_root = nullptr;
  }
  return *this;
}

// ── file I/O ───────────────────────────────────────────────────────────────

bool ctoon_doc::load_file(const char *path) {
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

  ctoon_decoder dec;
  ctoon_decoder_init(&dec);
  dec.indent_size = 2;
  dec.strict = 1;
  dec.expand_paths = 1;

  ctoon_value *v = ctoon_decode(buf, &dec);
  free(buf);

  if (!v) {
    log_error(dec.error);
    ctoon_decoder_free(&dec);
    return false;
  }

  ctoon_value_free(m_root);
  m_root = v;
  ctoon_decoder_free(&dec);
  return true;
}

bool ctoon_doc::save_file(const char *path,
                          const ctoon_encoder_opts *opts) const {
  if (!m_root)
    return false;
  ctoon_encoder_opts enc =
      opts ? *opts : ctoon_encoder_opts{2, ':', 1, -1};
  char *out = ctoon_encode(m_root, &enc);
  if (!out) {
    log_error("ctoon_encode failed");
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

ctoon_type ctoon_doc::type() const {
  return m_root ? m_root->type : CTOON_NULL;
}

bool ctoon_doc::valid() const {
  return m_root && m_root->type != CTOON_NULL;
}

// ── inspection ─────────────────────────────────────────────────────────────

bool ctoon_doc::has(const char *dot_path) const {
  return resolve_get(m_root, dot_path) != nullptr;
}

bool ctoon_doc::get_bool(const char *dot_path, bool default_val) const {
  ctoon_value *v = resolve_get(m_root, dot_path);
  if (!v || v->type != CTOON_BOOL)
    return default_val;
  return v->bool_val != 0;
}

double ctoon_doc::get_number(const char *dot_path, double default_val) const {
  ctoon_value *v = resolve_get(m_root, dot_path);
  if (!v || v->type != CTOON_NUMBER)
    return default_val;
  return v->num_val;
}

const char *ctoon_doc::get_string(const char *dot_path,
                                  const char *default_val) const {
  ctoon_value *v = resolve_get(m_root, dot_path);
  if (!v || v->type != CTOON_STRING)
    return default_val;
  return v->str_val ? v->str_val : default_val;
}

// ── setters ────────────────────────────────────────────────────────────────

void ctoon_doc::set(const char *dot_path, bool val) {
  if (!m_root || m_root->type != CTOON_OBJECT) {
    ctoon_value_free(m_root);
    m_root = ctoon_value_object();
  }
  ctoon_decoder d;
  ctoon_decoder_init(&d);
  ctoon_value *slot = ctoon_obj_set_ex(m_root, dot_path, 1, &d);
  if (slot) {
    ctoon_value_free_content(slot);
    slot->type = CTOON_BOOL;
    slot->bool_val = val ? 1 : 0;
  }
  ctoon_decoder_free(&d);
}

void ctoon_doc::set(const char *dot_path, double val) {
  if (!m_root || m_root->type != CTOON_OBJECT) {
    ctoon_value_free(m_root);
    m_root = ctoon_value_object();
  }
  ctoon_decoder d;
  ctoon_decoder_init(&d);
  ctoon_value *slot = ctoon_obj_set_ex(m_root, dot_path, 1, &d);
  if (slot) {
    ctoon_value_free_content(slot);
    slot->type = CTOON_NUMBER;
    slot->num_val = val;
  }
  ctoon_decoder_free(&d);
}

void ctoon_doc::set(const char *dot_path, const char *val) {
  if (!m_root || m_root->type != CTOON_OBJECT) {
    ctoon_value_free(m_root);
    m_root = ctoon_value_object();
  }
  ctoon_decoder d;
  ctoon_decoder_init(&d);
  ctoon_value *slot = ctoon_obj_set_ex(m_root, dot_path, 1, &d);
  if (slot) {
    ctoon_value_free_content(slot);
    slot->type = CTOON_STRING;
    slot->str_val = ctoon_strdup(val ? val : "");
  }
  ctoon_decoder_free(&d);
}

// ── containers ─────────────────────────────────────────────────────────────

size_t ctoon_doc::size() const {
  if (!m_root)
    return 0;
  if (m_root->type == CTOON_ARRAY || m_root->type == CTOON_OBJECT)
    return m_root->len;
  return 0;
}

ctoon_value *ctoon_doc::push() {
  if (!m_root || m_root->type != CTOON_ARRAY) {
    ctoon_value_free(m_root);
    m_root = ctoon_value_array();
  }
  return ctoon_array_push(m_root);
}

// ── raw access ─────────────────────────────────────────────────────────────

ctoon_value *ctoon_doc::release() {
  ctoon_value *v = m_root;
  m_root = nullptr;
  return v;
}

ctoon_value *ctoon_doc::get() const { return m_root; }

void ctoon_doc::reset(ctoon_value *v) {
  ctoon_value_free(m_root);
  m_root = v;
}

} // namespace Auxilia
