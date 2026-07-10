#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ctoon.h"



namespace Auxilia {

class ctoon_doc {
public:
  ctoon_doc();
  explicit ctoon_doc(ctoon_type t);
  explicit ctoon_doc(const char *path);
  ~ctoon_doc();

  ctoon_doc(const ctoon_doc &) = delete;
  ctoon_doc &operator=(const ctoon_doc &) = delete;

  ctoon_doc(ctoon_doc &&other) noexcept;
  ctoon_doc &operator=(ctoon_doc &&other) noexcept;

  bool load_file(const char *path);
  bool save_file(const char *path,
                  const ctoon_encoder_opts *opts = nullptr) const;

  ctoon_type type() const;
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
  ctoon_value *push();

  ctoon_value *release();
  ctoon_value *get() const;
  void reset(ctoon_value *v = nullptr);

private:
  static void log_error(const char *msg);

  ctoon_value *m_root = nullptr;
};

} // namespace Auxilia
