#if defined(__linux__)
#define _GNU_SOURCE
#endif

#include "module_guard.h"

#include <dlfcn.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "module_guard_sets.h"

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define MODULE_GUARD_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define MODULE_GUARD_ASAN 1
#endif

#if defined(MODULE_GUARD_ASAN)
#include <sanitizer/asan_interface.h>
#endif

#if defined(MODULE_GUARD_SANITIZER_STACK) || defined(MODULE_GUARD_ASAN)
void __sanitizer_print_stack_trace(void);
#endif

enum {
    GUARD_CAPACITY = 24,
    GUARD_POISON = 0xA5,
    PRISTINE_BYTES = 8 * 1024 * 1024,
    SHADOW_BYTES = 2 * 1024 * 1024,
    REPORT_BYTES = 1024
};

typedef enum GuardState {
    GUARD_UNTOUCHED,
    GUARD_LIVE,
    GUARD_EVICTED
} GuardState;

typedef struct GuardRange {
    char *start;
    char *end;
    uint8_t *pristine;
    uint8_t *shadow;
} GuardRange;

enum {
    RANGE_TEXT,
    RANGE_DATA,
    RANGE_CONST,
    RANGE_RODATA,
    RANGE_BSS,
    RANGE_COUNT
};

typedef struct GuardSet {
    GuardRange ranges[RANGE_COUNT];
    uint32_t mask;
    GuardState state;
    const char *evicted_by;
    uint32_t evicted_module;
} GuardSet;

#define GUARD_SETS(X)                                                                              \
    X(00)                                                                                          \
    X(01)                                                                                          \
    X(02)                                                                                          \
    X(03)                                                                                          \
    X(04)                                                                                          \
    X(05)                                                                                          \
    X(06)                                                                                          \
    X(07)                                                                                          \
    X(08)                                                                                          \
    X(09)                                                                                          \
    X(10)                                                                                          \
    X(11)                                                                                          \
    X(12)                                                                                          \
    X(13)                                                                                          \
    X(14)                                                                                          \
    X(15)                                                                                          \
    X(16)                                                                                          \
    X(17)                                                                                          \
    X(18)                                                                                          \
    X(19)                                                                                          \
    X(20)                                                                                          \
    X(21)                                                                                          \
    X(22)                                                                                          \
    X(23)

#if defined(__APPLE__)
#define GUARD_BOUND(n, segment, kind)                                                              \
    extern char guard_start_##n##kind[] __asm("section$start$" segment "$__em" #n "_" #kind);      \
    extern char guard_end_##n##kind[] __asm("section$end$" segment "$__em" #n "_" #kind);
#define GUARD_DECLARE(n)                                                                           \
    GUARD_BOUND(n, "__TEXT", t)                                                                    \
    GUARD_BOUND(n, "__DATA", d)                                                                    \
    GUARD_BOUND(n, "__DATA", c) GUARD_BOUND(n, "__DATA", r) GUARD_BOUND(n, "__DATA", b)
#define GUARD_START(n, kind) guard_start_##n##kind
#define GUARD_END(n, kind) guard_end_##n##kind
#else
#define GUARD_BOUND(n, kind)                                                                       \
    extern char __start_em##n##_##kind[] __attribute__((weak));                                    \
    extern char __stop_em##n##_##kind[] __attribute__((weak));
#define GUARD_DECLARE(n)                                                                           \
    GUARD_BOUND(n, t) GUARD_BOUND(n, d) GUARD_BOUND(n, c) GUARD_BOUND(n, r) GUARD_BOUND(n, b)
#define GUARD_START(n, kind) __start_em##n##_##kind
#define GUARD_END(n, kind) __stop_em##n##_##kind
#endif

GUARD_SETS(GUARD_DECLARE)

#define GUARD_ROW(n)                                                                               \
    {{{GUARD_START(n, t), GUARD_END(n, t), NULL, NULL},                                            \
      {GUARD_START(n, d), GUARD_END(n, d), NULL, NULL},                                            \
      {GUARD_START(n, c), GUARD_END(n, c), NULL, NULL},                                            \
      {GUARD_START(n, r), GUARD_END(n, r), NULL, NULL},                                            \
      {GUARD_START(n, b), GUARD_END(n, b), NULL, NULL}},                                           \
     0,                                                                                            \
     GUARD_UNTOUCHED,                                                                              \
     NULL,                                                                                         \
     0},

static GuardSet sets[GUARD_CAPACITY] = {GUARD_SETS(GUARD_ROW)};
static const uint32_t MASKS[] = MODULE_GUARD_SET_MASKS;
static uint8_t pristine_store[PRISTINE_BYTES];
static size_t pristine_used;
static bool shadow_known;
static bool armed;
static uint32_t current;
static ModuleGuardTrap trap;
static char report[REPORT_BYTES];

void __cyg_profile_func_enter(void *function, void *site);
void __cyg_profile_func_exit(void *function, void *site);

static size_t range_bytes(const GuardRange *range)
{
    return range->start != NULL && range->end > range->start ? (size_t)(range->end - range->start)
                                                             : 0;
}

static void fail(const char *message)
{
    fprintf(stderr, "module guard: %s\n", message);
    abort();
}

static uint8_t *reserve(size_t bytes)
{
    if (bytes > PRISTINE_BYTES - pristine_used) {
        fail("snapshot store is too small for the module sections");
    }
    uint8_t *place = pristine_store + pristine_used;
    pristine_used += (bytes + 15u) & ~(size_t)15u;
    return place;
}

#if defined(MODULE_GUARD_ASAN)
__attribute__((no_sanitize("address"))) static void raw_copy(uint8_t *target, const char *source,
                                                             size_t bytes)
{
    volatile const char *from = source;
    volatile uint8_t *to = target;
    for (size_t index = 0; index < bytes; index++) {
        to[index] = (uint8_t)from[index];
    }
}

static uint8_t *shadow_of(const char *address)
{
    size_t scale = 0;
    size_t offset = 0;
    __asan_get_shadow_mapping(&scale, &offset);
    return (uint8_t *)(((uintptr_t)address >> scale) + offset);
}

static size_t shadow_bytes(const GuardRange *range)
{
    return (size_t)(shadow_of(range->end - 1) - shadow_of(range->start)) + 1;
}

static void snapshot_shadow(void)
{
    for (size_t set = 0; set < GUARD_CAPACITY; set++) {
        for (int kind = RANGE_DATA; kind < RANGE_COUNT; kind++) {
            GuardRange *range = &sets[set].ranges[kind];
            if (range_bytes(range) != 0) {
                size_t bytes = shadow_bytes(range);
                range->shadow = reserve(bytes);
                raw_copy(range->shadow, (const char *)shadow_of(range->start), bytes);
            }
        }
    }
}

static void unpoison(GuardRange *range)
{
    __asan_unpoison_memory_region(range->start, range_bytes(range));
}

static void restore_shadow(GuardRange *range)
{
    raw_copy(shadow_of(range->start), (const char *)range->shadow, shadow_bytes(range));
}

static void poison_shadow(GuardRange *range)
{
    __asan_poison_memory_region(range->start, range_bytes(range));
}
#else
static void raw_copy(uint8_t *target, const char *source, size_t bytes)
{
    memcpy(target, source, bytes);
}

static void snapshot_shadow(void)
{
}

static void unpoison(GuardRange *range)
{
    (void)range;
}

static void restore_shadow(GuardRange *range)
{
    (void)range;
}

static void poison_shadow(GuardRange *range)
{
    (void)range;
}
#endif

static bool writable(int kind)
{
#if defined(__APPLE__)
    (void)kind;
    return true;
#else
    return kind != RANGE_CONST && kind != RANGE_RODATA;
#endif
}

__attribute__((constructor)) static void snapshot_pristine(void)
{
    for (size_t set = 0; set < GUARD_CAPACITY; set++) {
        sets[set].mask = set < MODULE_GUARD_SET_COUNT ? MASKS[set] : 0;
        for (int kind = RANGE_DATA; kind <= RANGE_RODATA; kind++) {
            GuardRange *range = &sets[set].ranges[kind];
            size_t bytes = range_bytes(range);
            if (bytes != 0 && writable(kind)) {
                range->pristine = reserve(bytes);
                raw_copy(range->pristine, range->start, bytes);
            }
        }
    }
}

static void ensure_shadow(void)
{
    if (!shadow_known) {
        shadow_known = true;
        snapshot_shadow();
    }
}

static void refresh(GuardSet *set)
{
    for (int kind = RANGE_DATA; kind < RANGE_COUNT; kind++) {
        GuardRange *range = &set->ranges[kind];
        size_t bytes = range_bytes(range);
        if (bytes == 0) {
            continue;
        }
        unpoison(range);
        if (range->pristine != NULL) {
            memcpy(range->start, range->pristine, bytes);
        } else if (writable(kind)) {
            memset(range->start, 0, bytes);
        }
        restore_shadow(range);
    }
}

static void poison(GuardSet *set, const char *event, uint32_t module)
{
    for (int kind = RANGE_DATA; kind < RANGE_COUNT; kind++) {
        GuardRange *range = &set->ranges[kind];
        size_t bytes = range_bytes(range);
        if (bytes != 0) {
            unpoison(range);
            if (writable(kind)) {
                memset(range->start, GUARD_POISON, bytes);
            }
            poison_shadow(range);
        }
    }
    set->state = GUARD_EVICTED;
    set->evicted_by = event;
    set->evicted_module = module;
}

static bool armed_any(void)
{
    for (size_t index = 0; index < GUARD_CAPACITY; index++) {
        if (sets[index].state == GUARD_LIVE) {
            return true;
        }
    }
    return false;
}

static void rearm(void)
{
    armed = false;
    for (size_t set = 0; set < GUARD_CAPACITY; set++) {
        armed = armed || sets[set].state == GUARD_EVICTED;
    }
}

const char *module_guard_module_name(uint32_t module)
{
    switch (module) {
    case 0:
        return "none";
    case 1:
        return "views";
    case 2:
        return "settings";
    case 3:
        return "library";
    case 4:
        return "listening";
    case 9:
        return "equalizer";
    case 11:
        return "playing";
    case 16:
        return "stage";
    default:
        return "unknown";
    }
}

static size_t append_names(char *text, size_t room, uint32_t mask)
{
    size_t used = 0;
    for (uint32_t module = 1; module < 32; module++) {
        if ((mask & 1u << module) != 0 && used < room) {
            int written = snprintf(text + used, room - used, "%s%s", used == 0 ? "" : "+",
                                   module_guard_module_name(module));
            used += written > 0 ? (size_t)written : 0;
        }
    }
    return used < room ? used : room;
}

static bool reported(const void *function)
{
    static const void *seen[64];
    static size_t count;
    for (size_t index = 0; index < count; index++) {
        if (seen[index] == function) {
            return true;
        }
    }
    if (count < 64) {
        seen[count++] = function;
    }
    return false;
}

static void log_report(void)
{
    const char *path = getenv("ECHO_MODULE_GUARD_LOG");
    FILE *log = path != NULL ? fopen(path, "a") : NULL;
    if (log == NULL) {
        return;
    }
    fputs(report, log);
    fflush(log);
    void *frames[24];
    int count = backtrace(frames, 24);
    backtrace_symbols_fd(frames, count, fileno(log));
    fputs("\n", log);
    fclose(log);
}

static void report_call(const GuardSet *set, const void *function, const void *site)
{
    char owners[128] = {0};
    append_names(owners, sizeof owners, set->mask);
    snprintf(report, sizeof report,
             "module guard: call into evicted module code\n"
             "  function %p in the %s module code\n"
             "  called from %p\n"
             "  the slot holds %s; the code was evicted by %s (loading %s)\n",
             function, owners, site, module_guard_module_name(current),
             set->evicted_by != NULL ? set->evicted_by : "?",
             module_guard_module_name(set->evicted_module));
    if (trap != NULL) {
        trap(report, function);
        return;
    }
    fputs(report, stderr);
    const char *mode = getenv("ECHO_MODULE_GUARD");
    if (mode != NULL && strcmp(mode, "report") == 0) {
        if (!reported(function)) {
            log_report();
        }
        return;
    }
#if defined(MODULE_GUARD_SANITIZER_STACK) || defined(MODULE_GUARD_ASAN)
    __sanitizer_print_stack_trace();
#endif
    abort();
}

static void check_call(const void *function, const void *site)
{
    for (size_t index = 0; index < GUARD_CAPACITY; index++) {
        const GuardSet *set = &sets[index];
        const GuardRange *text = &set->ranges[RANGE_TEXT];
        if (set->state == GUARD_EVICTED && (const char *)function >= text->start &&
            (const char *)function < text->end) {
            report_call(set, function, site);
            return;
        }
    }
}

void __cyg_profile_func_enter(void *function, void *site)
{
    if (armed) {
        check_call(function, site);
    }
}

void __cyg_profile_func_exit(void *function, void *site)
{
    (void)function;
    (void)site;
}

static void trace(const char *event, uint32_t module)
{
    static int known = -1;
    if (known < 0) {
        known = getenv("ECHO_MODULE_GUARD_TRACE") != NULL ? 1 : 0;
    }
    if (known == 1) {
        fprintf(stderr, "module guard: %s %s (slot held %s)\n", event,
                module_guard_module_name(module), module_guard_module_name(current));
        const char *stack = getenv("ECHO_MODULE_GUARD_TRACE");
        if (stack != NULL && strcmp(stack, "stack") == 0) {
#if defined(MODULE_GUARD_SANITIZER_STACK) || defined(MODULE_GUARD_ASAN)
            __sanitizer_print_stack_trace();
#endif
        }
    }
}

static bool called_from(const char *name)
{
    void *frames[48];
    int count = backtrace(frames, 48);
    for (int index = 0; index < count; index++) {
        Dl_info info;
        if (dladdr(frames[index], &info) != 0 && info.dli_sname != NULL &&
            strcmp(info.dli_sname, name) == 0) {
            return true;
        }
    }
    return false;
}

static const char *load_event(const char *event, const char *shortcut_event)
{
    return armed_any() && called_from("app_menu_hold") ? shortcut_event : event;
}

void module_guard_load(uint32_t module)
{
    ensure_shadow();
    trace("load", module);
    const char *event = load_event("a module load", "a module load for a Hold Menu shortcut");
    for (size_t index = 0; index < GUARD_CAPACITY; index++) {
        GuardSet *set = &sets[index];
        bool member = module < 32 && (set->mask & 1u << module) != 0;
        bool other_copy = current != 0 && current != module && (set->mask & 1u << current) != 0;
        if (member && (set->state == GUARD_EVICTED || (set->state == GUARD_LIVE && other_copy))) {
            refresh(set);
            set->state = GUARD_LIVE;
        } else if (member) {
            set->state = GUARD_LIVE;
        } else if (set->state == GUARD_LIVE) {
            poison(set, event, module);
        }
    }
    current = module;
    rearm();
}

void module_guard_evict(const char *cause)
{
    ensure_shadow();
    trace(cause, 0);
    const char *event = strcmp(cause, "a refused module load") == 0
                            ? load_event(cause, "a refused module load for a Hold Menu shortcut")
                            : cause;
    for (size_t index = 0; index < GUARD_CAPACITY; index++) {
        if (sets[index].state == GUARD_LIVE) {
            poison(&sets[index], event, 0);
        }
    }
    current = 0;
    rearm();
}

void module_guard_reset(void)
{
    ensure_shadow();
    for (size_t index = 0; index < GUARD_CAPACITY; index++) {
        if (sets[index].state == GUARD_EVICTED) {
            refresh(&sets[index]);
        }
        sets[index].state = GUARD_UNTOUCHED;
        sets[index].evicted_by = NULL;
    }
    current = 0;
    trap = NULL;
    rearm();
}

void module_guard_restore_all(void)
{
    ensure_shadow();
    for (size_t index = 0; index < GUARD_CAPACITY; index++) {
        refresh(&sets[index]);
        sets[index].state = GUARD_UNTOUCHED;
        sets[index].evicted_by = NULL;
    }
    current = 0;
    rearm();
}

void module_guard_borrow(uint32_t module)
{
    ensure_shadow();
    for (size_t index = 0; index < GUARD_CAPACITY; index++) {
        GuardSet *set = &sets[index];
        if (module < 32 && (set->mask & 1u << module) != 0 && set->state == GUARD_EVICTED) {
            refresh(set);
            set->state = GUARD_UNTOUCHED;
        }
    }
    rearm();
}

static uint32_t borrowed;
static const char *borrowed_events[GUARD_CAPACITY];

void module_guard_oracle_begin(uint32_t module)
{
    ensure_shadow();
    borrowed = 0;
    for (size_t index = 0; index < GUARD_CAPACITY; index++) {
        GuardSet *set = &sets[index];
        if (module < 32 && (set->mask & 1u << module) != 0 && set->state == GUARD_EVICTED) {
            borrowed |= 1u << index;
            borrowed_events[index] = set->evicted_by;
            refresh(set);
            set->state = GUARD_UNTOUCHED;
        }
    }
    rearm();
}

void module_guard_oracle_end(void)
{
    for (size_t index = 0; index < GUARD_CAPACITY; index++) {
        if ((borrowed & 1u << index) != 0) {
            poison(&sets[index], borrowed_events[index], sets[index].evicted_module);
        }
    }
    borrowed = 0;
    rearm();
}

bool module_guard_live(uint32_t module)
{
    for (size_t index = 0; index < GUARD_CAPACITY; index++) {
        const GuardSet *set = &sets[index];
        if (module < 32 && (set->mask & 1u << module) != 0 && set->state == GUARD_EVICTED) {
            return false;
        }
    }
    return true;
}

const void *module_guard_probe(uint32_t module)
{
    for (size_t index = 0; index < GUARD_CAPACITY; index++) {
        const GuardSet *set = &sets[index];
        if (module < 32 && set->mask == 1u << module && range_bytes(&set->ranges[RANGE_BSS]) != 0) {
            return set->ranges[RANGE_BSS].start;
        }
    }
    return NULL;
}

static GuardSet *set_of(const void *address);

void module_guard_lend(const void *address)
{
    GuardSet *set = set_of(address);
    if (set != NULL && set->state == GUARD_EVICTED) {
        refresh(set);
        set->state = GUARD_UNTOUCHED;
        rearm();
    }
}

static GuardSet *set_of(const void *address)
{
    for (size_t index = 0; index < GUARD_CAPACITY; index++) {
        for (int kind = 0; kind < RANGE_COUNT; kind++) {
            const GuardRange *range = &sets[index].ranges[kind];
            if ((const char *)address >= range->start && (const char *)address < range->end &&
                range_bytes(range) != 0) {
                return &sets[index];
            }
        }
    }
    return NULL;
}

uint32_t module_guard_owners(const void *address)
{
    const GuardSet *set = set_of(address);
    return set != NULL ? set->mask : 0;
}

bool module_guard_evicted(const void *address)
{
    const GuardSet *set = set_of(address);
    return set != NULL && set->state == GUARD_EVICTED;
}

size_t module_guard_guarded_sets(void)
{
    size_t count = 0;
    for (size_t index = 0; index < GUARD_CAPACITY; index++) {
        count += range_bytes(&sets[index].ranges[RANGE_TEXT]) != 0 ? 1u : 0u;
    }
    return count;
}

void module_guard_abort(const char *text)
{
    fputs(text, stderr);
#if defined(MODULE_GUARD_SANITIZER_STACK) || defined(MODULE_GUARD_ASAN)
    __sanitizer_print_stack_trace();
#endif
    abort();
}

void module_guard_set_trap(ModuleGuardTrap handler)
{
    trap = handler;
}
