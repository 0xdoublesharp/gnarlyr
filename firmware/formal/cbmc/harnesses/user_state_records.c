#include "device/update_trial.h"
#include "formal.h"
#include "settings/user_state.h"

static uint8_t bytes[USER_STATE_LIST_BYTES];

void harness(void)
{
    __CPROVER_havoc_object(bytes);
    UserStateList list = user_state_list_decode(bytes);
    for (int slot = 0; slot < USER_STATE_BACKUPS; slot++) {
        __CPROVER_assert(list.builds[slot][USER_STATE_BUILD_BYTES] == '\0',
                         "a backup's build name ends inside its field");
    }
    __CPROVER_assert(list.newest < USER_STATE_BACKUPS || list.newest == USER_STATE_NO_SLOT,
                     "the newest backup is a slot or none");
    uint8_t request = user_state_mark_request(bytes);
    __CPROVER_assert(request <= USER_STATE_BACKUPS, "a restore request names a slot or none");
    char name[USER_STATE_NAME_CAPACITY];
    UserStateKind kind = user_state_kind(formal_below(user_state_count()), name);
    (void)user_state_age(&kind, bytes, formal_below(USER_STATE_HEAD_BYTES + 1));
    UpdateTrial trial = update_trial_decode(bytes);
    __CPROVER_assert(trial.state <= UPDATE_TRIAL_ROLLED_BACK, "a trial record state is known");
    (void)user_state_list_order(&list, (int)formal_below(USER_STATE_BACKUPS));
}
