#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "toon.h"

namespace Auxilia {

class toon_doc {
public:
  toon_doc();
  explicit toon_doc(toon_type t);
  explicit toon_doc(const char *path);
  ~toon_doc();

  toon_doc(const toon_doc &) = delete;
  toon_doc &operator=(const toon_doc &) = delete;

  toon_doc(toon_doc &&other) noexcept;
  toon_doc &operator=(toon_doc &&other) noexcept;

  bool load_file(const char *path);
  bool save_file(const char *path,
                  const toon_encoder_opts *opts = nullptr) const;

  toon_type type() const;
  bool valid() const;

  bool has(const char *dot_path) const;

  bool get_bool(const char *dot_path, bool default_val = false) const;
  double get_number(const char *dot_path, double default_val = 0.0) const;
  const char *get_string(const char *dot_path,
                          const char *default_val = "") const;

  void set(const char *dot_path, bool val);
  void set(const char *dot_path, double val);
  void set(const char *dot_path, const char *val);

  size_t size() const;
  toon_value *push();

  toon_value *release();
  toon_value *get() const;
  void reset(toon_value *v = nullptr);

private:
  static void log_error(const char *msg);

  toon_value *m_root = nullptr;
};

} // namespace Auxilia
