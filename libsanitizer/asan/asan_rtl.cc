//===-- asan_rtl.cc -------------------------------------------------------===//
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Main file of the ASan run-time library.
//===----------------------------------------------------------------------===//
#include "asan_allocator.h"
#include "asan_interceptors.h"
#include "asan_interface_internal.h"
#include "asan_internal.h"
#include "asan_mapping.h"
#include "asan_poisoning.h"
#include "asan_report.h"
#include "asan_stack.h"
#include "asan_stats.h"
#include "asan_thread.h"
#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_symbolizer.h"
#include "lsan/lsan_common.h"

int __asan_option_detect_stack_use_after_return;  // Global interface symbol.

namespace __asan {

uptr AsanMappingProfile[kAsanMappingProfileSize];

static void AsanDie() {
  static atomic_uint32_t num_calls;
  if (atomic_fetch_add(&num_calls, 1, memory_order_relaxed) != 0) {
    // Don't die twice - run a busy loop.
    while (1) { }
  }
  if (flags()->sleep_before_dying) {
    Report("Sleeping for %d second(s)\n", flags()->sleep_before_dying);
    SleepForSeconds(flags()->sleep_before_dying);
  }
  if (flags()->unmap_shadow_on_exit) {
    if (kMidMemBeg) {
      UnmapOrDie((void*)kLowShadowBeg, kMidMemBeg - kLowShadowBeg);
      UnmapOrDie((void*)kMidMemEnd, kHighShadowEnd - kMidMemEnd);
    } else {
      UnmapOrDie((void*)kLowShadowBeg, kHighShadowEnd - kLowShadowBeg);
    }
  }
  if (flags()->coverage)
    __sanitizer_cov_dump();
  if (death_callback)
    death_callback();
  if (flags()->abort_on_error)
    Abort();
  internal__exit(flags()->exitcode);
}

static void AsanCheckFailed(const char *file, int line, const char *cond,
                            u64 v1, u64 v2) {
  Report("AddressSanitizer CHECK failed: %s:%d \"%s\" (0x%zx, 0x%zx)\n",
             file, line, cond, (uptr)v1, (uptr)v2);
  // FIXME: check for infinite recursion without a thread-local counter here.
  PRINT_CURRENT_STACK();
  Die();
}

// -------------------------- Flags ------------------------- {{{1
static const int kDefaultMallocContextSize = 30;

Flags asan_flags_dont_use_directly;  // use via flags().

static const char *MaybeCallAsanDefaultOptions() {
  return (&__asan_default_options) ? __asan_default_options() : "";
}

static const char *MaybeUseAsanDefaultOptionsCompileDefiniton() {
#ifdef ASAN_DEFAULT_OPTIONS
// Stringize the macro value.
# define ASAN_STRINGIZE(x) #x
# define ASAN_STRINGIZE_OPTIONS(options) ASAN_STRINGIZE(options)
  return ASAN_STRINGIZE_OPTIONS(ASAN_DEFAULT_OPTIONS);
#else
  return "";
#endif
}

static void ParseFlagsFromString(Flags *f, const char *str) {
  CommonFlags *cf = common_flags();
  ParseCommonFlagsFromString(cf, str);
  CHECK((uptr)cf->malloc_context_size <= kStackTraceMax);

  ParseFlag(str, &f->quarantine_size, "quarantine_size");
  ParseFlag(str, &f->redzone, "redzone");
  CHECK_GE(f->redzone, 16);
  CHECK(IsPowerOfTwo(f->redzone));

  ParseFlag(str, &f->debug, "debug");
  ParseFlag(str, &f->report_globals, "report_globals");
  ParseFlag(str, &f->check_initialization_order, "check_initialization_order");

  ParseFlag(str, &f->replace_str, "replace_str");
  ParseFlag(str, &f->replace_intrin, "replace_intrin");
  ParseFlag(str, &f->mac_ignore_invalid_free, "mac_ignore_invalid_free");
  ParseFlag(str, &f->detect_stack_use_after_return,
            "detect_stack_use_after_return");
  ParseFlag(str, &f->uar_stack_size_log, "uar_stack_size_log");
  ParseFlag(str, &f->max_malloc_fill_size, "max_malloc_fill_size");
  ParseFlag(str, &f->malloc_fill_byte, "malloc_fill_byte");
  ParseFlag(str, &f->exitcode, "exitcode");
  ParseFlag(str, &f->allow_user_poisoning, "allow_user_poisoning");
  ParseFlag(str, &f->sleep_before_dying, "sleep_before_dying");
  ParseFlag(str, &f->handle_segv, "handle_segv");
  ParseFlag(str, &f->allow_user_segv_handler, "allow_user_segv_handler");
  ParseFlag(str, &f->use_sigaltstack, "use_sigaltstack");
  ParseFlag(str, &f->check_malloc_usable_size, "check_malloc_usable_size");
  ParseFlag(str, &f->unmap_shadow_on_exit, "unmap_shadow_on_exit");
  ParseFlag(str, &f->abort_on_error, "abort_on_error");
  ParseFlag(str, &f->print_stats, "print_stats");
  ParseFlag(str, &f->print_legend, "print_legend");
  ParseFlag(str, &f->atexit, "atexit");
  ParseFlag(str, &f->coverage, "coverage");
  ParseFlag(str, &f->disable_core, "disable_core");
  ParseFlag(str, &f->allow_reexec, "allow_reexec");
  ParseFlag(str, &f->print_full_thread_history, "print_full_thread_history");
  ParseFlag(str, &f->poison_heap, "poison_heap");
  ParseFlag(str, &f->poison_partial, "poison_partial");
  ParseFlag(str, &f->alloc_dealloc_mismatch, "alloc_dealloc_mismatch");
  ParseFlag(str, &f->strict_memcmp, "strict_memcmp");
  ParseFlag(str, &f->strict_init_order, "strict_init_order");
}

void InitializeFlags(Flags *f, const char *env) {
  CommonFlags *cf = common_flags();
  SetCommonFlagsDefaults(cf);
  cf->external_symbolizer_path = GetEnv("ASAN_SYMBOLIZER_PATH");
  cf->malloc_context_size = kDefaultMallocContextSize;

  internal_memset(f, 0, sizeof(*f));
  f->quarantine_size = (ASAN_LOW_MEMORY) ? 1UL << 26 : 1UL << 28;
  f->redzone = 16;
  f->debug = false;
  f->report_globals = 1;
  f->check_initialization_order = false;
  f->replace_str = true;
  f->replace_intrin = true;
  f->mac_ignore_invalid_free = false;
  f->detect_stack_use_after_return = false;  // Also needs the compiler flag.
  f->uar_stack_size_log = 0;
  f->max_malloc_fill_size = 0x1000;  // By default, fill only the first 4K.
  f->malloc_fill_byte = 0xbe;
  f->exitcode = ASAN_DEFAULT_FAILURE_EXITCODE;
  f->allow_user_poisoning = true;
  f->sleep_before_dying = 0;
  f->handle_segv = ASAN_NEEDS_SEGV;
  f->allow_user_segv_handler = false;
  f->use_sigaltstack = false;
  f->check_malloc_usable_size = true;
  f->unmap_shadow_on_exit = false;
  f->abort_on_error = false;
  f->print_stats = false;
  f->print_legend = true;
  f->atexit = false;
  f->coverage = false;
  f->disable_core = (SANITIZER_WORDSIZE == 64);
  f->allow_reexec = true;
  f->print_full_thread_history = true;
  f->poison_heap = true;
  f->poison_partial = true;
  // Turn off alloc/dealloc mismatch checker on Mac and Windows for now.
  // TODO(glider,timurrrr): Fix known issues and enable this back.
  f->alloc_dealloc_mismatch = (SANITIZER_MAC == 0) && (SANITIZER_WINDOWS == 0);
  f->strict_memcmp = true;
  f->strict_init_order = false;

  // Override from compile definition.
  ParseFlagsFromString(f, MaybeUseAsanDefaultOptionsCompileDefiniton());

  // Override from user-specified string.
  ParseFlagsFromString(f, MaybeCallAsanDefaultOptions());
  if (cf->verbosity) {
    Report("Using the defaults from __asan_default_options: %s\n",
           MaybeCallAsanDefaultOptions());
  }

  // Override from command line.
  ParseFlagsFromString(f, env);

#if !CAN_SANITIZE_LEAKS
  if (cf->detect_leaks) {
    Report("%s: detect_leaks is not supported on this platform.\n",
           SanitizerToolName);
    cf->detect_leaks = false;
  }
#endif

  // Make "strict_init_order" imply "check_initialization_order".
  // TODO(samsonov): Use a single runtime flag for an init-order checker.
  if (f->strict_init_order) {
    f->check_initialization_order = true;
  }
}

// -------------------------- Globals --------------------- {{{1
int asan_inited;
bool asan_init_is_running;
void (*death_callback)(void);

#if !ASAN_FIXED_MAPPING
uptr kHighMemEnd, kMidMemBeg, kMidMemEnd;
#endif

// -------------------------- Misc ---------------- {{{1
void ShowStatsAndAbort() {
  __asan_print_accumulated_stats();
  Die();
}

// ---------------------- mmap -------------------- {{{1
// Reserve memory range [beg, end].
static void ReserveShadowMemoryRange(uptr beg, uptr end) {
  CHECK_EQ((beg % GetPageSizeCached()), 0);
  CHECK_EQ(((end + 1) % GetPageSizeCached()), 0);
  uptr size = end - beg + 1;
  void *res = MmapFixedNoReserve(beg, size);
  if (res != (void*)beg) {
    Report("ReserveShadowMemoryRange failed while trying to map 0x%zx bytes. "
           "Perhaps you're using ulimit -v\n", size);
    Abort();
  }
}

// --------------- LowLevelAllocateCallbac ---------- {{{1
static void OnLowLevelAllocate(uptr ptr, uptr size) {
  PoisonShadow(ptr, size, kAsanInternalHeapMagic);
}

// -------------------------- Run-time entry ------------------- {{{1
// exported functions
#define ASAN_REPORT_ERROR(type, is_write, size)                     \
extern "C" NOINLINE INTERFACE_ATTRIBUTE                        \
void __asan_report_ ## type ## size(uptr addr);                \
void __asan_report_ ## type ## size(uptr addr) {               \
  GET_CALLER_PC_BP_SP;                                              \
  __asan_report_error(pc, bp, sp, addr, is_write, size);            \
}

ASAN_REPORT_ERROR(load, false, 1)
ASAN_REPORT_ERROR(load, false, 2)
ASAN_REPORT_ERROR(load, false, 4)
ASAN_REPORT_ERROR(load, false, 8)
ASAN_REPORT_ERROR(load, false, 16)
ASAN_REPORT_ERROR(store, true, 1)
ASAN_REPORT_ERROR(store, true, 2)
ASAN_REPORT_ERROR(store, true, 4)
ASAN_REPORT_ERROR(store, true, 8)
ASAN_REPORT_ERROR(store, true, 16)

#define ASAN_REPORT_ERROR_N(type, is_write)                    \
extern "C" NOINLINE INTERFACE_ATTRIBUTE                        \
void __asan_report_ ## type ## _n(uptr addr, uptr size);       \
void __asan_report_ ## type ## _n(uptr addr, uptr size) {      \
  GET_CALLER_PC_BP_SP;                                         \
  __asan_report_error(pc, bp, sp, addr, is_write, size);       \
}

ASAN_REPORT_ERROR_N(load, false)
ASAN_REPORT_ERROR_N(store, true)

// Force the linker to keep the symbols for various ASan interface functions.
// We want to keep those in the executable in order to let the instrumented
// dynamic libraries access the symbol even if it is not used by the executable
// itself. This should help if the build system is removing dead code at link
// time.
static NOINLINE void force_interface_symbols() {
  volatile int fake_condition = 0;  // prevent dead condition elimination.
  // __asan_report_* functions are noreturn, so we need a switch to prevent
  // the compiler from removing any of them.
  switch (fake_condition) {
    case 1: __asan_report_load1(0); break;
    case 2: __asan_report_load2(0); break;
    case 3: __asan_report_load4(0); break;
    case 4: __asan_report_load8(0); break;
    case 5: __asan_report_load16(0); break;
    case 6: __asan_report_store1(0); break;
    case 7: __asan_report_store2(0); break;
    case 8: __asan_report_store4(0); break;
    case 9: __asan_report_store8(0); break;
    case 10: __asan_report_store16(0); break;
    case 12: __asan_register_globals(0, 0); break;
    case 13: __asan_unregister_globals(0, 0); break;
    case 14: __asan_set_death_callback(0); break;
    case 15: __asan_set_error_report_callback(0); break;
    case 16: __asan_handle_no_return(); break;
    case 17: __asan_address_is_poisoned(0); break;
    case 18: __asan_get_allocated_size(0); break;
    case 19: __asan_get_current_allocated_bytes(); break;
    case 20: __asan_get_estimated_allocated_size(0); break;
    case 21: __asan_get_free_bytes(); break;
    case 22: __asan_get_heap_size(); break;
    case 23: __asan_get_ownership(0); break;
    case 24: __asan_get_unmapped_bytes(); break;
    case 25: __asan_poison_memory_region(0, 0); break;
    case 26: __asan_unpoison_memory_region(0, 0); break;
    case 27: __asan_set_error_exit_code(0); break;
    case 30: __asan_before_dynamic_init(0); break;
    case 31: __asan_after_dynamic_init(); break;
    case 32: __asan_poison_stack_memory(0, 0); break;
    case 33: __asan_unpoison_stack_memory(0, 0); break;
    case 34: __asan_region_is_poisoned(0, 0); break;
    case 35: __asan_describe_address(0); break;
  }
}

static void asan_atexit() {
  Printf("AddressSanitizer exit stats:\n");
  __asan_print_accumulated_stats();
  // Print AsanMappingProfile.
  for (uptr i = 0; i < kAsanMappingProfileSize; i++) {
    if (AsanMappingProfile[i] == 0) continue;
    Printf("asan_mapping.h:%zd -- %zd\n", i, AsanMappingProfile[i]);
  }
}

static void InitializeHighMemEnd() {
#if !ASAN_FIXED_MAPPING
  kHighMemEnd = GetMaxVirtualAddress();
  // Increase kHighMemEnd to make sure it's properly
  // aligned together with kHighMemBeg:
  kHighMemEnd |= SHADOW_GRANULARITY * GetPageSizeCached() - 1;
#endif  // !ASAN_FIXED_MAPPING
  CHECK_EQ((kHighMemBeg % GetPageSizeCached()), 0);
}

static void ProtectGap(uptr a, uptr size) {
  CHECK_EQ(a, (uptr)Mprotect(a, size));
}

static void PrintAddressSpaceLayout() {
  Printf("|| `[%p, %p]` || HighMem    ||\n",
         (void*)kHighMemBeg, (void*)kHighMemEnd);
  Printf("|| `[%p, %p]` || HighShadow ||\n",
         (void*)kHighShadowBeg, (void*)kHighShadowEnd);
  if (kMidMemBeg) {
    Printf("|| `[%p, %p]` || ShadowGap3 ||\n",
           (void*)kShadowGap3Beg, (void*)kShadowGap3End);
    Printf("|| `[%p, %p]` || MidMem     ||\n",
           (void*)kMidMemBeg, (void*)kMidMemEnd);
    Printf("|| `[%p, %p]` || ShadowGap2 ||\n",
           (void*)kShadowGap2Beg, (void*)kShadowGap2End);
    Printf("|| `[%p, %p]` || MidShadow  ||\n",
           (void*)kMidShadowBeg, (void*)kMidShadowEnd);
  }
  Printf("|| `[%p, %p]` || ShadowGap  ||\n",
         (void*)kShadowGapBeg, (void*)kShadowGapEnd);
  if (kLowShadowBeg) {
    Printf("|| `[%p, %p]` || LowShadow  ||\n",
           (void*)kLowShadowBeg, (void*)kLowShadowEnd);
    Printf("|| `[%p, %p]` || LowMem     ||\n",
           (void*)kLowMemBeg, (void*)kLowMemEnd);
  }
  Printf("MemToShadow(shadow): %p %p %p %p",
         (void*)MEM_TO_SHADOW(kLowShadowBeg),
         (void*)MEM_TO_SHADOW(kLowShadowEnd),
         (void*)MEM_TO_SHADOW(kHighShadowBeg),
         (void*)MEM_TO_SHADOW(kHighShadowEnd));
  if (kMidMemBeg) {
    Printf(" %p %p",
           (void*)MEM_TO_SHADOW(kMidShadowBeg),
           (void*)MEM_TO_SHADOW(kMidShadowEnd));
  }
  Printf("\n");
  Printf("red_zone=%zu\n", (uptr)flags()->redzone);
  Printf("quarantine_size=%zuM\n", (uptr)flags()->quarantine_size >> 20);
  Printf("malloc_context_size=%zu\n",
         (uptr)common_flags()->malloc_context_size);

  Printf("SHADOW_SCALE: %zx\n", (uptr)SHADOW_SCALE);
  Printf("SHADOW_GRANULARITY: %zx\n", (uptr)SHADOW_GRANULARITY);
  Printf("SHADOW_OFFSET: %zx\n", (uptr)SHADOW_OFFSET);
  CHECK(SHADOW_SCALE >= 3 && SHADOW_SCALE <= 7);
  if (kMidMemBeg)
    CHECK(kMidShadowBeg > kLowShadowEnd &&
          kMidMemBeg > kMidShadowEnd &&
          kHighShadowBeg > kMidMemEnd);
}

}  // namespace __asan

// ---------------------- Interface ---------------- {{{1
using namespace __asan;  // NOLINT

#if !SANITIZER_SUPPORTS_WEAK_HOOKS
extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE
const char* __asan_default_options() { return ""; }
}  // extern "C"
#endif

int NOINLINE __asan_set_error_exit_code(int exit_code) {
  int old = flags()->exitcode;
  flags()->exitcode = exit_code;
  return old;
}

void NOINLINE __asan_handle_no_return() {
  int local_stack;
  AsanThread *curr_thread = GetCurrentThread();
  CHECK(curr_thread);
  uptr PageSize = GetPageSizeCached();
  uptr top = curr_thread->stack_top();
  uptr bottom = ((uptr)&local_stack - PageSize) & ~(PageSize-1);
  static const uptr kMaxExpectedCleanupSize = 64 << 20;  // 64M
  if (top - bottom > kMaxExpectedCleanupSize) {
    static bool reported_warning = false;
    if (reported_warning)
      return;
    reported_warning = true;
    Report("WARNING: ASan is ignoring requested __asan_handle_no_return: "
           "stack top: %p; bottom %p; size: %p (%zd)\n"
           "False positive error reports may follow\n"
           "For details see "
           "http://code.google.com/p/address-sanitizer/issues/detail?id=189\n",
           top, bottom, top - bottom, top - bottom);
    return;
  }
  PoisonShadow(bottom, top - bottom, 0);
  if (curr_thread->has_fake_stack())
    curr_thread->fake_stack()->HandleNoReturn();
}

void NOINLINE __asan_set_death_callback(void (*callback)(void)) {
  death_callback = callback;
}

void __asan_init() {
  if (asan_inited) return;
  SanitizerToolName = "AddressSanitizer";
  CHECK(!asan_init_is_running && "ASan init calls itself!");
  asan_init_is_running = true;
  InitializeHighMemEnd();

  // Make sure we are not statically linked.
  AsanDoesNotSupportStaticLinkage();

  // Install tool-specific callbacks in sanitizer_common.
  SetDieCallback(AsanDie);
  SetCheckFailedCallback(AsanCheckFailed);
  SetPrintfAndReportCallback(AppendToErrorMessageBuffer);

  // Initialize flags. This must be done early, because most of the
  // initialization steps look at flags().
  const char *options = GetEnv("ASAN_OPTIONS");
  InitializeFlags(flags(), options);
  __sanitizer_set_report_path(common_flags()->log_path);
  __asan_option_detect_stack_use_after_return =
      flags()->detect_stack_use_after_return;

  if (common_flags()->verbosity && options) {
    Report("Parsed ASAN_OPTIONS: %s\n", options);
  }

  // Re-exec ourselves if we need to set additional env or command line args.
  MaybeReexec();

  // Setup internal allocator callback.
  SetLowLevelAllocateCallback(OnLowLevelAllocate);

  InitializeAsanInterceptors();

  ReplaceSystemMalloc();
  ReplaceOperatorsNewAndDelete();

  uptr shadow_start = kLowShadowBeg;
  if (kLowShadowBeg)
    shadow_start -= GetMmapGranularity();
  bool full_shadow_is_available =
      MemoryRangeIsAvailable(shadow_start, kHighShadowEnd);

#if SANITIZER_LINUX && defined(__x86_64__) && !ASAN_FIXED_MAPPING
  if (!full_shadow_is_available) {
    kMidMemBeg = kLowMemEnd < 0x3000000000ULL ? 0x3000000000ULL : 0;
    kMidMemEnd = kLowMemEnd < 0x3000000000ULL ? 0x4fffffffffULL : 0;
  }
#endif

  if (common_flags()->verbosity)
    PrintAddressSpaceLayout();

  if (flags()->disable_core) {
    DisableCoreDumper();
  }

  if (full_shadow_is_available) {
    // mmap the low shadow plus at least one page at the left.
    if (kLowShadowBeg)
      ReserveShadowMemoryRange(shadow_start, kLowShadowEnd);
    // mmap the high shadow.
    ReserveShadowMemoryRange(kHighShadowBeg, kHighShadowEnd);
    // protect the gap.
    ProtectGap(kShadowGapBeg, kShadowGapEnd - kShadowGapBeg + 1);
  } else if (kMidMemBeg &&
      MemoryRangeIsAvailable(shadow_start, kMidMemBeg - 1) &&
      MemoryRangeIsAvailable(kMidMemEnd + 1, kHighShadowEnd)) {
    CHECK(kLowShadowBeg != kLowShadowEnd);
    // mmap the low shadow plus at least one page at the left.
    ReserveShadowMemoryRange(shadow_start, kLowShadowEnd);
    // mmap the mid shadow.
    ReserveShadowMemoryRange(kMidShadowBeg, kMidShadowEnd);
    // mmap the high shadow.
    ReserveShadowMemoryRange(kHighShadowBeg, kHighShadowEnd);
    // protect the gaps.
    ProtectGap(kShadowGapBeg, kShadowGapEnd - kShadowGapBeg + 1);
    ProtectGap(kShadowGap2Beg, kShadowGap2End - kShadowGap2Beg + 1);
    ProtectGap(kShadowGap3Beg, kShadowGap3End - kShadowGap3Beg + 1);
  } else {
    Report("Shadow memory range interleaves with an existing memory mapping. "
           "ASan cannot proceed correctly. ABORTING.\n");
    DumpProcessMap();
    Die();
  }

  AsanTSDInit(PlatformTSDDtor);
  InstallSignalHandlers();

  // Allocator should be initialized before starting external symbolizer, as
  // fork() on Mac locks the allocator.
  InitializeAllocator();

  // Start symbolizer process if necessary.
  if (common_flags()->symbolize) {
    Symbolizer::Init(common_flags()->external_symbolizer_path);
  } else {
    Symbolizer::Disable();
  }

  // On Linux AsanThread::ThreadStart() calls malloc() that's why asan_inited
  // should be set to 1 prior to initializing the threads.
  asan_inited = 1;
  asan_init_is_running = false;

  if (flags()->atexit)
    Atexit(asan_atexit);

  if (flags()->coverage)
    Atexit(__sanitizer_cov_dump);

  // interceptors
  InitTlsSize();

  // Create main thread.
  AsanThread *main_thread = AsanThread::Create(0, 0);
  CreateThreadContextArgs create_main_args = { main_thread, 0 };
  u32 main_tid = asanThreadRegistry().CreateThread(
      0, true, 0, &create_main_args);
  CHECK_EQ(0, main_tid);
  SetCurrentThread(main_thread);
  main_thread->ThreadStart(internal_getpid());
  force_interface_symbols();  // no-op.

#if CAN_SANITIZE_LEAKS
  __lsan::InitCommonLsan();
  if (common_flags()->detect_leaks && common_flags()->leak_check_at_exit) {
    Atexit(__lsan::DoLeakCheck);
  }
#endif  // CAN_SANITIZE_LEAKS

  if (common_flags()->verbosity) {
    Report("AddressSanitizer Init done\n");
  }
}
