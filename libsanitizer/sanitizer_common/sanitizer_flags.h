//===-- sanitizer_flags.h ---------------------------------------*- C++ -*-===//
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_FLAGS_H
#define SANITIZER_FLAGS_H

#include "sanitizer_internal_defs.h"

namespace __sanitizer {

void ParseFlag(const char *env, bool *flag, const char *name);
void ParseFlag(const char *env, int *flag, const char *name);
void ParseFlag(const char *env, const char **flag, const char *name);

struct CommonFlags {
  // If set, use the online symbolizer from common sanitizer runtime.
  bool symbolize;
  // Path to external symbolizer. If it is NULL, symbolizer will be looked for
  // in PATH. If it is empty, external symbolizer will not be started.
  const char *external_symbolizer_path;
  // Strips this prefix from file paths in error reports.
  const char *strip_path_prefix;
  // Use fast (frame-pointer-based) unwinder on fatal errors (if available).
  bool fast_unwind_on_fatal;
  // Use fast (frame-pointer-based) unwinder on malloc/free (if available).
  bool fast_unwind_on_malloc;
  // Intercept and handle ioctl requests.
  bool handle_ioctl;
  // Max number of stack frames kept for each allocation/deallocation.
  int malloc_context_size;
  // Write logs to "log_path.pid".
  // The special values are "stdout" and "stderr".
  // The default is "stderr".
  const char *log_path;
  // Verbosity level (0 - silent, 1 - a bit of output, 2+ - more output).
  int  verbosity;
  // Enable memory leak detection.
  bool detect_leaks;
  // Invoke leak checking in an atexit handler. Has no effect if
  // detect_leaks=false, or if __lsan_do_leak_check() is called before the
  // handler has a chance to run.
  bool leak_check_at_exit;
  // If false, the allocator will crash instead of returning 0 on out-of-memory.
  bool allocator_may_return_null;
  // If false, disable printing error summaries in addition to error reports.
  bool print_summary;
};

inline CommonFlags *common_flags() {
  static CommonFlags f;
  return &f;
}

void SetCommonFlagsDefaults(CommonFlags *f);
void ParseCommonFlagsFromString(CommonFlags *f, const char *str);

}  // namespace __sanitizer

#endif  // SANITIZER_FLAGS_H
