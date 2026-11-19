#include "ab_repeat.h"

#include "core/i18n.h"

static bool cancels_end_sleep(PlayerMark mark, PowerPolicy *power)
{
    return mark == PLAYER_MARK_B && power->sleep_timer_active && power->sleep_track_end;
}

PlayerMark ab_repeat_step(Player *player, PowerPolicy *power, Toast *toast, uint32_t now_ms)
{
    PlayerMark mark = player_repeat_mark(player);
    StringId text = mark == PLAYER_MARK_REFUSED    ? STR_AB_B_AFTER_A
                    : mark == PLAYER_MARK_NO_TRACK ? STR_NOTHING_IS_PLAYING
                                                   : STRING_COUNT;
    if (cancels_end_sleep(mark, power)) {
        power_policy_cancel_sleep_timer(power);
        power->sleep_track_end = false;
        power->sleep_option = 0;
        player_set_stop_after_track(player, false);
        text = STR_SLEEP_OFF;
    }
    if (text != STRING_COUNT) {
        toast_show(toast, i18n_text(text), now_ms);
    }
    return mark;
}
