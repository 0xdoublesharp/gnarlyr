#include "cassette_flip_screen.h"

#include "cassette_art.h"
#include "core/i18n.h"
#include "core/seam.h"
#include "core/string_builder.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/message_screen.h"
#include "ui/navigation.h"

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    CassetteFlipScreen *screen = context;
    (void)now_ms;
    Navigation navigation = navigation_from_key(event);
    if (navigation.intent == NAVIGATION_SELECT) {
        player_toggle_pause(screen->player);
        return screen_action_pop();
    }
    return navigation_exit_action(event);
}

CORE_SEAM void cassette_flip_screen_enter(void *context, uint32_t now_ms)
{
    CassetteFlipScreen *screen = context;
    (void)now_ms;
    MixtapeStore *store = &screen->mixtapes->store;
    (void)mixtape_store_init(store);
    MixEntry entry;
    StringBuilder name = string_builder_make(screen->name, sizeof screen->name);
    bool found = mixtape_store_read(store, screen->player->cassette.slot, &entry) && entry.active &&
                 entry.generation == screen->player->cassette.generation;
    string_builder_append(&name, found ? entry.name : i18n_text(STR_MIXTAPE));
    screen->screen.title = screen->name;
}

static void draw(void *context, Canvas *canvas)
{
    const CassetteFlipScreen *screen = context;
    const Theme *theme = screen->theme;
    bool side_b = screen->player->cassette.side != 0;
    const char *message = i18n_text(side_b ? STR_TAPE_FINISHED : STR_FLIP_SIDE_B);
    Rect content = theme_content_area(theme);
    int padding = theme->metrics.padding;
    int width = content.width - 2 * padding;
    int height =
        label_wrap_count(theme->fonts.body, message, width) * font_line_height(theme->fonts.body);
    int top = content.y + (content.height - CASSETTE_HEIGHT - padding - height) / 2;
    header_draw(canvas, theme, screen->name);
    Rect art = {content.x + (content.width - CASSETTE_WIDTH) / 2, top, CASSETTE_WIDTH,
                CASSETTE_HEIGHT};
    cassette_art_draw(canvas, art, theme,
                      (CassetteTape){.position_ms = 1,
                                     .duration_ms = 1,
                                     .label = i18n_text(side_b ? STR_SIDE_B : STR_SIDE_A)});
    Rect text = {content.x, top + CASSETTE_HEIGHT + padding, content.width, height};
    message_draw(canvas, theme, text, message);
}

Screen *cassette_flip_screen_init(CassetteFlipScreen *screen, Player *player, const Theme *theme,
                                  MixtapeUi *mixtapes)
{
    *screen = (CassetteFlipScreen){.player = player, .theme = theme, .mixtapes = mixtapes};
    screen->screen = (Screen){.title = i18n_text(STR_MIXTAPE),
                              .context = screen,
                              .takes_every_key = true,
                              .stays_on_wake = true,
                              .enter = cassette_flip_screen_enter,
                              .handle_key = handle_key,
                              .draw = draw};
    return &screen->screen;
}
