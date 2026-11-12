#include "core/parametric.h"

bool parametric_same(const ParametricEq *first, const ParametricEq *second)
{
    if (first->count != second->count || first->preamp != second->preamp) {
        return false;
    }
    for (int index = 0; index < first->count && index < PARAMETRIC_FILTERS; index++) {
        const ParametricFilter *a = &first->filters[index];
        const ParametricFilter *b = &second->filters[index];
        if (a->hz != b->hz || a->q != b->q || a->gain != b->gain || a->type != b->type ||
            a->enabled != b->enabled) {
            return false;
        }
    }
    return true;
}
