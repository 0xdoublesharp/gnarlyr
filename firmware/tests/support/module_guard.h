#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*ModuleGuardTrap)(const char *report, const void *function);

void module_guard_reset(void);
void module_guard_restore_all(void);
void module_guard_load(uint32_t module);
void module_guard_evict(const char *event);
void module_guard_borrow(uint32_t module);
void module_guard_lend(const void *address);
void module_guard_oracle_begin(uint32_t module);
void module_guard_oracle_end(void);
bool module_guard_live(uint32_t module);
const void *module_guard_probe(uint32_t module);
uint32_t module_guard_owners(const void *address);
bool module_guard_evicted(const void *address);
size_t module_guard_guarded_sets(void);
void module_guard_set_trap(ModuleGuardTrap trap);
void module_guard_abort(const char *text);
void module_guard_trap_abort(const char *report, const void *function);
const char *module_guard_module_name(uint32_t module);
