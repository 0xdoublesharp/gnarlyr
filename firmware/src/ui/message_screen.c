#include "ui/message_screen.h"

#include <stddef.h>

#include "ui/header.h"
#include "ui/label.h"
#include "ui/navigation.h"

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

void message_draw(Canvas *canvas, const Theme *theme, Rect area, const char *text)
{
    TextStyle style = {.font = theme->fonts.body, .color = theme->colors.text_secondary};
    Rect inner = {area.x + theme->metrics.padding, area.y, area.width - 2 * theme->metrics.padding,
                  area.height};
    label_draw_wrapped(canvas, style, (Label){.area = inner, .text = text});
}

void message_draw_working(Canvas *canvas, Rect area, const MessageConfig *config)
{
    const Theme *theme = config->theme;
    const Font *font = theme->fonts.body;
    int padding = theme->metrics.padding;
    Rect inner = {area.x + padding, area.y + padding, area.width - 2 * padding,
                  area.height - 2 * padding};
    int text = label_wrap_count(font, config->message, inner.width) * font_line_height(font);
    Rect space = {inner.x, inner.y, inner.width, inner.height - text - padding};
    int side = config->motion != NULL ? wire_motion_fit(space).width : 0;
    int lead = side > 0 ? side + padding : 0;
    int top = inner.y + (inner.height - lead - text) / 2;
    if (side > 0) {
        wire_motion_draw(config->motion, canvas, theme, (Rect){inner.x, top, inner.width, side});
    }
    TextStyle style = {.font = font, .color = theme->colors.text_secondary};
    Rect below = {inner.x, top + lead, inner.width, text};
    label_draw_wrapped(canvas, style, (Label){.area = below, .text = config->message});
}

static void draw(void *context, Canvas *canvas)
{
    const MessageScreen *message = context;
    const Theme *theme = message->config.theme;
    Rect content = theme_content_area(theme);
    canvas_fill_rect(canvas, content, theme->colors.background);
    header_draw(canvas, theme, message->config.title);
    message_draw_working(canvas, content, &message->config);
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
