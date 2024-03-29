//===-- interception.h ------------------------------------------*- C++ -*-===//
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Machinery for providing replacements/wrappers for system functions.
//===----------------------------------------------------------------------===//

#ifndef INTERCEPTION_H
#define INTERCEPTION_H

#if !defined(__linux__) && !defined(__FreeBSD__) && \
  !defined(__APPLE__) && !defined(_WIN32)
# error "Interception doesn't work on this operating system."
#endif

#include "sanitizer_common/sanitizer_internal_defs.h"

// These typedefs should be used only in the interceptor definitions to replace
// the standard system types (e.g. SSIZE_T instead of ssize_t)
typedef __sanitizer::uptr    SIZE_T;
typedef __sanitizer::sptr    SSIZE_T;
typedef __sanitizer::sptr    PTRDIFF_T;
typedef __sanitizer::s64     INTMAX_T;
typedef __sanitizer::OFF_T   OFF_T;
typedef __sanitizer::OFF64_T OFF64_T;

// How to add an interceptor:
// Suppose you need to wrap/replace system function (generally, from libc):
//      int foo(const char *bar, double baz);
// You'll need to:
//      1) define INTERCEPTOR(int, foo, const char *bar, double baz) { ... } in
//         your source file. See the notes below for cases when
//         INTERCEPTOR_WITH_SUFFIX(...) should be used instead.
//      2) Call "INTERCEPT_FUNCTION(foo)" prior to the first call of "foo".
//         INTERCEPT_FUNCTION(foo) evaluates to "true" iff the function was
//         intercepted successfully.
// You can access original function by calling REAL(foo)(bar, baz).
// By default, REAL(foo) will be visible only inside your interceptor, and if
// you want to use it in other parts of RTL, you'll need to:
//      3a) add DECLARE_REAL(int, foo, const char*, double) to a
//          header file.
// However, if the call "INTERCEPT_FUNCTION(foo)" and definition for
// INTERCEPTOR(..., foo, ...) are in different files, you'll instead need to:
//      3b) add DECLARE_REAL_AND_INTERCEPTOR(int, foo, const char*, double)
//          to a header file.

// Notes: 1. Things may not work properly if macro INTERCEPTOR(...) {...} or
//           DECLARE_REAL(...) are located inside namespaces.
//        2. On Mac you can also use: "OVERRIDE_FUNCTION(foo, zoo)" to
//           effectively redirect calls from "foo" to "zoo". In this case
//           you aren't required to implement
//           INTERCEPTOR(int, foo, const char *bar, double baz) {...}
//           but instead you'll have to add
//           DECLARE_REAL(int, foo, const char *bar, double baz) in your
//           source file (to define a pointer to overriden function).
//        3. Some Mac functions have symbol variants discriminated by
//           additional suffixes, e.g. _$UNIX2003 (see
//           https://developer.apple.com/library/mac/#releasenotes/Darwin/SymbolVariantsRelNotes/index.html
//           for more details). To intercept such functions you need to use the
//           INTERCEPTOR_WITH_SUFFIX(...) macro.

// How it works:
// To replace system functions on Linux we just need to declare functions
// with same names in our library and then obtain the real function pointers
// using dlsym().
// There is one complication. A user may also intercept some of the functions
// we intercept. To resolve this we declare our interceptors with __interceptor_
// prefix, and then make actual interceptors weak aliases to __interceptor_
// functions.
//
// This is not so on Mac OS, where the two-level namespace makes
// our replacement functions invisible to other libraries. This may be overcomed
// using the DYLD_FORCE_FLAT_NAMESPACE, but some errors loading the shared
// libraries in Chromium were noticed when doing so.
// Instead we create a dylib containing a __DATA,__interpose section that
// associates library functions with their wrappers. When this dylib is
// preloaded before an executable using DYLD_INSERT_LIBRARIES, it routes all
// the calls to interposed functions done through stubs to the wrapper
// functions.
// As it's decided at compile time which functions are to be intercepted on Mac,
// INTERCEPT_FUNCTION() is effectively a no-op on this system.

#if defined(__APPLE__)
#include <sys/cdefs.h>  // For __DARWIN_ALIAS_C().

// Just a pair of pointers.
struct interpose_substitution {
  const uptr replacement;
  const uptr original;
};

// For a function foo() create a global pair of pointers { wrap_foo, foo } in
// the __DATA,__interpose section.
// As a result all the calls to foo() will be routed to wrap_foo() at runtime.
#define INTERPOSER(func_name) __attribute__((used)) \
const interpose_substitution substitution_##func_name[] \
    __attribute__((section("__DATA, __interpose"))) = { \
    { reinterpret_cast<const uptr>(WRAP(func_name)), \
      reinterpret_cast<const uptr>(func_name) } \
}

// For a function foo() and a wrapper function bar() create a global pair
// of pointers { bar, foo } in the __DATA,__interpose section.
// As a result all the calls to foo() will be routed to bar() at runtime.
#define INTERPOSER_2(func_name, wrapper_name) __attribute__((used)) \
const interpose_substitution substitution_##func_name[] \
    __attribute__((section("__DATA, __interpose"))) = { \
    { reinterpret_cast<const uptr>(wrapper_name), \
      reinterpret_cast<const uptr>(func_name) } \
}

# define WRAP(x) wrap_##x
# define WRAPPER_NAME(x) "wrap_"#x
# define INTERCEPTOR_ATTRIBUTE
# define DECLARE_WRAPPER(ret_type, func, ...)

#elif defined(_WIN32)
# if defined(_DLL)  // DLL CRT
#  define WRAP(x) x
#  define WRAPPER_NAME(x) #x
#  define INTERCEPTOR_ATTRIBUTE
# else  // Static CRT
#  define WRAP(x) wrap_##x
#  define WRAPPER_NAME(x) "wrap_"#x
#  define INTERCEPTOR_ATTRIBUTE
# endif
# define DECLARE_WRAPPER(ret_type, func, ...) \
    extern "C" ret_type func(__VA_ARGS__);
# define DECLARE_WRAPPER_WINAPI(ret_type, func, ...) \
    extern "C" __declspec(dllimport) ret_type __stdcall func(__VA_ARGS__);
#else
# define WRAP(x) __interceptor_ ## x
# define WRAPPER_NAME(x) "__interceptor_" #x
# define INTERCEPTOR_ATTRIBUTE __attribute__((visibility("default")))
# define DECLARE_WRAPPER(ret_type, func, ...) \
    extern "C" ret_type func(__VA_ARGS__) \
    __attribute__((weak, alias("__interceptor_" #func), visibility("default")));
#endif

#if !defined(__APPLE__)
# define PTR_TO_REAL(x) real_##x
# define REAL(x) __interception::PTR_TO_REAL(x)
# define FUNC_TYPE(x) x##_f

# define DECLARE_REAL(ret_type, func, ...) \
    typedef ret_type (*FUNC_TYPE(func))(__VA_ARGS__); \
    namespace __interception { \
      extern FUNC_TYPE(func) PTR_TO_REAL(func); \
    }
#else  // __APPLE__
# define REAL(x) x
# define DECLARE_REAL(ret_type, func, ...) \
    extern "C" ret_type func(__VA_ARGS__);
#endif  // __APPLE__

#define DECLARE_REAL_AND_INTERCEPTOR(ret_type, func, ...) \
  DECLARE_REAL(ret_type, func, __VA_ARGS__) \
  extern "C" ret_type WRAP(func)(__VA_ARGS__);

// Generally, you don't need to use DEFINE_REAL by itself, as INTERCEPTOR
// macros does its job. In exceptional cases you may need to call REAL(foo)
// without defining INTERCEPTOR(..., foo, ...). For example, if you override
// foo with an interceptor for other function.
#if !defined(__APPLE__)
# define DEFINE_REAL(ret_type, func, ...) \
    typedef ret_type (*FUNC_TYPE(func))(__VA_ARGS__); \
    namespace __interception { \
      FUNC_TYPE(func) PTR_TO_REAL(func); \
    }
#else
# define DEFINE_REAL(ret_type, func, ...)
#endif

#if !defined(__APPLE__)
#define INTERCEPTOR(ret_type, func, ...) \
  DEFINE_REAL(ret_type, func, __VA_ARGS__) \
  DECLARE_WRAPPER(ret_type, func, __VA_ARGS__) \
  extern "C" \
  INTERCEPTOR_ATTRIBUTE \
  ret_type WRAP(func)(__VA_ARGS__)

// We don't need INTERCEPTOR_WITH_SUFFIX on non-Darwin for now.
#define INTERCEPTOR_WITH_SUFFIX(ret_type, func, ...) \
  INTERCEPTOR(ret_type, func, __VA_ARGS__)

#else  // __APPLE__

#define INTERCEPTOR_ZZZ(suffix, ret_type, func, ...) \
  extern "C" ret_type func(__VA_ARGS__) suffix; \
  extern "C" ret_type WRAP(func)(__VA_ARGS__); \
  INTERPOSER(func); \
  extern "C" INTERCEPTOR_ATTRIBUTE ret_type WRAP(func)(__VA_ARGS__)

#define INTERCEPTOR(ret_type, func, ...) \
  INTERCEPTOR_ZZZ(/*no symbol variants*/, ret_type, func, __VA_ARGS__)

#define INTERCEPTOR_WITH_SUFFIX(ret_type, func, ...) \
  INTERCEPTOR_ZZZ(__DARWIN_ALIAS_C(func), ret_type, func, __VA_ARGS__)

// Override |overridee| with |overrider|.
#define OVERRIDE_FUNCTION(overridee, overrider) \
  INTERPOSER_2(overridee, WRAP(overrider))
#endif

#if defined(_WIN32)
# define INTERCEPTOR_WINAPI(ret_type, func, ...) \
    typedef ret_type (__stdcall *FUNC_TYPE(func))(__VA_ARGS__); \
    namespace __interception { \
      FUNC_TYPE(func) PTR_TO_REAL(func); \
    } \
    DECLARE_WRAPPER_WINAPI(ret_type, func, __VA_ARGS__) \
    extern "C" \
    INTERCEPTOR_ATTRIBUTE \
    ret_type __stdcall WRAP(func)(__VA_ARGS__)
#endif

// ISO C++ forbids casting between pointer-to-function and pointer-to-object,
// so we use casting via an integral type __interception::uptr,
// assuming that system is POSIX-compliant. Using other hacks seem
// challenging, as we don't even pass function type to
// INTERCEPT_FUNCTION macro, only its name.
namespace __interception {
#if defined(_WIN64)
typedef unsigned long long uptr;  // NOLINT
#else
typedef unsigned long uptr;  // NOLINT
#endif  // _WIN64
}  // namespace __interception

#define INCLUDED_FROM_INTERCEPTION_LIB

#if defined(__linux__) || defined(__FreeBSD__)
# include "interception_linux.h"
# define INTERCEPT_FUNCTION(func) INTERCEPT_FUNCTION_LINUX_OR_FREEBSD(func)
# define INTERCEPT_FUNCTION_VER(func, symver) \
    INTERCEPT_FUNCTION_VER_LINUX_OR_FREEBSD(func, symver)
#elif defined(__APPLE__)
# include "interception_mac.h"
# define INTERCEPT_FUNCTION(func) INTERCEPT_FUNCTION_MAC(func)
# define INTERCEPT_FUNCTION_VER(func, symver) \
    INTERCEPT_FUNCTION_VER_MAC(func, symver)
#else  // defined(_WIN32)
# include "interception_win.h"
# define INTERCEPT_FUNCTION(func) INTERCEPT_FUNCTION_WIN(func)
# define INTERCEPT_FUNCTION_VER(func, symver) \
    INTERCEPT_FUNCTION_VER_WIN(func, symver)
#endif

#undef INCLUDED_FROM_INTERCEPTION_LIB

#endif  // INTERCEPTION_H
