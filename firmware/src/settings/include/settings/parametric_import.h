#pragma once

#include "core/parametric.h"

enum {
    PARAMETRIC_LINE_BYTES = 160
};

typedef struct ParametricImport {
    ParametricEq eq;
    char line[PARAMETRIC_LINE_BYTES];
    uint16_t length;
    uint16_t total;
    bool recognized;
    bool has_filters;
    bool invalid;
} ParametricImport;

void parametric_import_start(ParametricImport *parser);
void parametric_import_text(ParametricImport *parser, const char *text, uint32_t length);
bool parametric_import_finish(ParametricImport *parser, ParametricEq *eq);
