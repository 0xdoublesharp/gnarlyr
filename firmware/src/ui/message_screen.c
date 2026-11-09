#include "ui/message_screen.h"

#include <stddef.h>

#include "ui/header.h"
#include "ui/icons.h"
#include "ui/label.h"
#include "ui/navigation.h"

enum {
    CARD_ICON = 2 * ICON_LARGE_GRID,
    CARD_GAP = 8
};

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    (void)context;
    (void)now_ms;
    return navigation_exit_action(event);
}

static void update(void *context, uint32_t now_ms)
{
    (void)context;
    (void)now_ms;
}

void message_draw_card(Canvas *canvas, const Theme *theme, Rect area, MessageCard card)
{
    const ThemeColors *colors = &theme->colors;
    IconId icon = (IconId)card.icon;
    int line = font_line_height(theme->fonts.body);
    int lift = icon != ICON_NONE ? CARD_ICON + CARD_GAP : 0;
    int top = area.y + (area.height - lift - line) / 2;
    Rect box = {area.x + (area.width - CARD_ICON) / 2, top, CARD_ICON, CARD_ICON};
    icon_draw(canvas, icon, box, (IconInk){colors->text_secondary, colors->accent});
    TextStyle text = {.font = theme->fonts.body,
                      .color = lift > 0 ? colors->text : colors->text_secondary};
    Rect row = {area.x, top + lift, area.width, line};
    label_draw(canvas, text, (Label){label_centered_area(text.font, row, card.text), card.text});
}

static void draw(void *context, Canvas *canvas)
{
    const MessageScreen *message = context;
    const Theme *theme = message->config.theme;
    const MessageConfig *config = &message->config;
    Rect content = theme_content_area(theme);
    canvas_fill_rect(canvas, content, theme->colors.background);
    header_draw(canvas, theme, config->title);
    message_draw_card(canvas, theme, content, (MessageCard){config->icon, config->message});
}

Screen *message_screen_init(MessageScreen *message, MessageConfig config)
{
    *message = (MessageScreen){.config = config};
    message->screen = (Screen){
        .title = config.title,
        .context = message,
        .enter = NULL,
        .handle_key = handle_key,
        .update = update,
        .draw = draw,
    };
    return &message->screen;
}
