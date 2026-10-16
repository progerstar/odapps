/*
 * gpio_extender (Nuklear / SDL2 port)
 *
 * A C rewrite of the original Qt "gpio_extender" controller. It talks to a
 * GPIO extender board over a serial port using a tiny '~'-prefixed protocol:
 *
 *   Sent periodically (one command every 10 ms, alternating):
 *     "~A"            request the state of all inputs
 *     "~P" + 5 digits set outputs 1..5 ('1'/'0' per channel)
 *
 *   Received:
 *     "~A" + 5 digits state of inputs 6..10
 *     "~G" <pin> <st> state of a single pin (pin is a hex digit 6..A)
 *     "~I" <ver...>   firmware version string
 *
 * Outputs 1..5 are stateful toggle buttons; inputs 6..10 are colored
 * indicators (green = high, gray = low).
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#include <SDL2/SDL.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_SDL_RENDERER_IMPLEMENTATION
#include "../third_party/nuklear.h"
#include "../third_party/nuklear_sdl_renderer.h"

#include "serial.h"

#define NUM_OUT 5      /* output channels 1..5  */
#define NUM_IN  5      /* input  channels 6..10 */
#define POLL_MS 10     /* periodic command interval (~A / ~P alternate) */
#define BAUD    115200 /* fixed serial speed */

typedef struct {
    serial_port_list ports;
    const char      *port_items[SERIAL_MAX_PORTS];
    int              port_sel;

    int  fd;                 /* open port fd, or -1 */
    int  out[NUM_OUT];       /* output toggle states */
    int  in[NUM_IN];         /* input indicator states */

    char     status[256];    /* transient status-bar message */
    Uint64   status_until;   /* ms timestamp when status clears (0 = sticky) */
    char     fw[64];         /* firmware version, once reported */
    int      has_fw;

    Uint64 last_poll;        /* last periodic-send timestamp */
    int    phase;            /* alternates ~A / ~P each poll */

    /* incoming-byte parser state (mirrors the original state machine) */
    int  pstate;
    int  ppin;               /* pin index resolved from a ~G command */
    int  pcount;             /* chars collected for multi-char responses */
    char pbuf[NUM_IN];       /* staged ~A digits, committed only when valid */
} app_state;

/* ---- helpers ------------------------------------------------------------ */

static void set_status(app_state *a, Uint64 now, int ms, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->status, sizeof(a->status), fmt, ap);
    va_end(ap);
    a->status_until = (ms > 0) ? now + (Uint64)ms : 0;
}

static void rebuild_port_items(app_state *a)
{
    int i;
    for (i = 0; i < a->ports.count; i++)
        a->port_items[i] = a->ports.names[i];
    if (a->port_sel >= a->ports.count)
        a->port_sel = a->ports.count > 0 ? a->ports.count - 1 : 0;
}

static void do_scan(app_state *a, Uint64 now)
{
    serial_list_ports(&a->ports);
    rebuild_port_items(a);
    set_status(a, now, 2000, "Портов найдено: %d", a->ports.count);
}

static void do_disconnect(app_state *a, Uint64 now)
{
    if (a->fd >= 0) {
        serial_close(a->fd);
        a->fd = -1;
        set_status(a, now, 2000, "Отключено");
    }
    a->pstate = 0;
}

static void do_connect(app_state *a, Uint64 now)
{
    if (a->ports.count == 0) {
        set_status(a, now, 2000, "Нет доступных портов");
        return;
    }
    do_disconnect(a, now);
    a->fd = serial_open(a->port_items[a->port_sel], BAUD);
    if (a->fd < 0) {
        set_status(a, now, 3000, "Не удалось открыть %s", a->port_items[a->port_sel]);
    } else {
        a->last_poll = now;
        a->phase = 0;
        set_status(a, now, 2000, "Подключено: %s @ %d",
                   a->port_items[a->port_sel], BAUD);
    }
}

static void send_cmd(app_state *a, Uint64 now, const char *cmd)
{
    if (a->fd < 0)
        return;
    if (serial_write(a->fd, cmd, strlen(cmd)) < 0)
        set_status(a, now, 1000, "Ошибка записи");
}

/* Periodic poll: alternate "~A" (read inputs) and "~P....." (set outputs). */
static void poll_tick(app_state *a, Uint64 now)
{
    if (a->fd < 0 || now - a->last_poll < POLL_MS)
        return;
    a->last_poll = now;

    if (a->phase == 0) {
        send_cmd(a, now, "~A");
    } else {
        char cmd[3 + NUM_OUT];
        int i;
        cmd[0] = '~';
        cmd[1] = 'P';
        for (i = 0; i < NUM_OUT; i++)
            cmd[2 + i] = a->out[i] ? '1' : '0';
        cmd[2 + NUM_OUT] = '\0';
        send_cmd(a, now, cmd);
    }
    a->phase ^= 1;
}

/* Resolve a hex digit (6..A) from a ~G command to an input index 0..4. */
static int pin_to_index(char c)
{
    int v = -1;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    if (v >= 6 && v <= 10) return v - 6;
    return -1;
}

/* Feed one received byte through the response parser. */
static void parse_byte(app_state *a, Uint64 now, char c)
{
    switch (a->pstate) {
    case 0: /* wait for frame start */
        if (c == '~') a->pstate = 1;
        break;
    case 1: /* command selector */
        if (c == 'G') {
            a->pstate = 2;
        } else if (c == 'A') {
            a->pcount = 0;
            a->pstate = 4;
        } else if (c == 'I') {
            a->pcount = 0;
            a->pstate = 5;
        } else if (c == '~') {
            a->pstate = 1; /* resync on a fresh start byte */
        } else {
            a->pstate = 0;
        }
        break;
    case 2: /* ~G: pin */
        a->ppin = pin_to_index(c);
        a->pstate = (a->ppin >= 0) ? 3 : 0;
        break;
    case 3: /* ~G: state — accept only a strict '0'/'1' */
        if ((c == '0' || c == '1') && a->ppin >= 0 && a->ppin < NUM_IN)
            a->in[a->ppin] = (c == '1');
        a->pstate = (c == '~') ? 1 : 0; /* resync if a new frame starts */
        break;
    case 4: /* ~A: five input states, validated as a complete frame */
        if (c != '0' && c != '1') {
            /* malformed digit: drop the whole frame, leave inputs untouched,
             * and resync if this byte actually starts a new frame */
            a->pstate = (c == '~') ? 1 : 0;
            break;
        }
        a->pbuf[a->pcount] = c;
        if (++a->pcount >= NUM_IN) {
            int k;
            for (k = 0; k < NUM_IN; k++)
                a->in[k] = (a->pbuf[k] == '1');
            a->pstate = 0;
        }
        break;
    case 5: /* ~I: firmware version (collect printable chars) */
        if (c == '~' || c == '\r' || c == '\n' ||
            a->pcount >= (int)sizeof(a->fw) - 1) {
            a->fw[a->pcount] = '\0';
            a->has_fw = 1;
            set_status(a, now, 4000, "Firmware: %s", a->fw);
            a->pstate = (c == '~') ? 1 : 0;
        } else {
            a->fw[a->pcount++] = c;
        }
        break;
    default:
        a->pstate = 0;
        break;
    }
}

static void serial_pump(app_state *a, Uint64 now)
{
    char buf[256];
    int n, i;
    if (a->fd < 0)
        return;
    while ((n = serial_read(a->fd, buf, sizeof(buf))) > 0) {
        for (i = 0; i < n; i++)
            parse_byte(a, now, buf[i]);
        if (n < (int)sizeof(buf))
            break; /* drained for now */
    }
    if (n < 0)
        do_disconnect(a, now);
}

/* ---- UI widgets --------------------------------------------------------- */

/* A momentary-click button styled green while `state` is on. Returns 1 if
 * it was clicked this frame. */
static int toggle_button(struct nk_context *ctx, const char *label, int state)
{
    struct nk_style_button saved = ctx->style.button;
    int clicked;
    if (state) {
        ctx->style.button.normal      = nk_style_item_color(nk_rgb(46, 160, 67));
        ctx->style.button.hover       = nk_style_item_color(nk_rgb(56, 180, 77));
        ctx->style.button.active      = nk_style_item_color(nk_rgb(36, 140, 57));
        ctx->style.button.text_normal = nk_rgb(255, 255, 255);
        ctx->style.button.text_hover  = nk_rgb(255, 255, 255);
        ctx->style.button.text_active = nk_rgb(255, 255, 255);
    }
    clicked = nk_button_label(ctx, label);
    ctx->style.button = saved;
    return clicked;
}

/* A non-interactive indicator: green when on, gray when off. */
static void indicator(struct nk_context *ctx, const char *label, int on)
{
    struct nk_style_button saved = ctx->style.button;
    struct nk_color c = on ? nk_rgb(46, 160, 67) : nk_rgb(80, 80, 80);
    ctx->style.button.normal      = nk_style_item_color(c);
    ctx->style.button.hover       = nk_style_item_color(c);
    ctx->style.button.active      = nk_style_item_color(c);
    ctx->style.button.text_normal = nk_rgb(235, 235, 235);
    ctx->style.button.text_hover  = nk_rgb(235, 235, 235);
    ctx->style.button.text_active = nk_rgb(235, 235, 235);
    nk_button_label(ctx, label); /* click intentionally ignored */
    ctx->style.button = saved;
}

static void build_ui(struct nk_context *ctx, app_state *a, Uint64 now, int w, int h)
{
    if (nk_begin(ctx, "GPIO Extender Demo", nk_rect(0, 0, (float)w, (float)h),
                 NK_WINDOW_NO_SCROLLBAR)) {
        int i;
        char num[8];

        /* row: port selector + scan + connect/disconnect */
        nk_layout_row_begin(ctx, NK_DYNAMIC, 28, 4);
        nk_layout_row_push(ctx, 0.12f);
        nk_label(ctx, "Порт", NK_TEXT_LEFT);
        nk_layout_row_push(ctx, 0.40f);
        if (a->ports.count > 0) {
            a->port_sel = nk_combo(ctx, a->port_items, a->ports.count,
                                   a->port_sel, 24, nk_vec2(280, 220));
        } else {
            static const char *none[] = {"(нет портов)"};
            nk_combo(ctx, none, 1, 0, 24, nk_vec2(280, 24));
        }
        nk_layout_row_push(ctx, 0.16f);
        if (nk_button_label(ctx, "Скан"))
            do_scan(a, now);
        nk_layout_row_push(ctx, 0.32f);
        if (toggle_button(ctx, a->fd >= 0 ? "Отключить" : "Подключить",
                          a->fd >= 0)) {
            if (a->fd >= 0) do_disconnect(a, now);
            else            do_connect(a, now);
        }
        nk_layout_row_end(ctx);

        /* a little breathing room between the port row and the channels */
        nk_layout_row_dynamic(ctx, 12, 1);
        nk_spacing(ctx, 1);

        /* OUT row: 5 stateful toggle buttons (channels 1..5), placed right
         * under the port row and enlarged for easier touch/click targets */
        nk_layout_row_begin(ctx, NK_DYNAMIC, 56, 1 + NUM_OUT);
        nk_layout_row_push(ctx, 0.16f);
        nk_label(ctx, "Выход", NK_TEXT_LEFT);
        for (i = 0; i < NUM_OUT; i++) {
            nk_layout_row_push(ctx, 0.84f / NUM_OUT);
            snprintf(num, sizeof(num), "%d", i + 1);
            if (toggle_button(ctx, num, a->out[i]))
                a->out[i] = !a->out[i];
        }
        nk_layout_row_end(ctx);

        /* IN row: 5 indicators (channels 6..10) */
        nk_layout_row_begin(ctx, NK_DYNAMIC, 56, 1 + NUM_IN);
        nk_layout_row_push(ctx, 0.16f);
        nk_label(ctx, "Вход", NK_TEXT_LEFT);
        for (i = 0; i < NUM_IN; i++) {
            nk_layout_row_push(ctx, 0.84f / NUM_IN);
            snprintf(num, sizeof(num), "%d", i + 6);
            indicator(ctx, num, a->in[i]);
        }
        nk_layout_row_end(ctx);

        /* push the status line to the very bottom of the window: fill the
         * gap between the channel rows above and the status line below.
         * content height = h - 2*padding.y; consumed by the four rows above
         * (28 + 56 + 56) plus the status row (22) and the 4px spacing that
         * Nuklear inserts between each of the five rows (4 gaps) */
        {
            float pad  = ctx->style.window.padding.y;
            float gap  = ctx->style.window.spacing.y;
            float used = 28.0f + 12.0f + 56.0f + 56.0f + 22.0f + 5.0f * gap;
            float fill = ((float)h - 2.0f * pad) - used;
            if (fill < 4.0f) fill = 4.0f;
            nk_layout_row_dynamic(ctx, fill, 1);
            nk_spacing(ctx, 1);
        }

        /* status line, pinned at the bottom */
        nk_layout_row_dynamic(ctx, 22, 1);
        if (a->status_until != 0 && now >= a->status_until)
            a->status[0] = '\0';
        nk_label(ctx, a->status[0] ? a->status :
                 (a->fd >= 0 ? "Работает" : "Не подключено"), NK_TEXT_LEFT);
    }
    nk_end(ctx);
}

/* ---- main --------------------------------------------------------------- */

int main(int argc, char **argv)
{
    /* On Windows, SDL2's SDL_main wrapper calls main(argc, argv), so the
     * signature must take them. We don't use the command line. Harmless on
     * Linux/macOS, where SDL doesn't redefine main. */
    SDL_Window   *win;
    SDL_Renderer *renderer;
    struct nk_context *ctx;
    struct nk_font *font;
    float font_scale = 1.0f;
    int running = 1;
    app_state app;

    (void)argc;
    (void)argv;

    memset(&app, 0, sizeof(app));
    app.fd = -1;

    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    win = SDL_CreateWindow("GPIO Extender Demo",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            420, 300, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI |
                      SDL_WINDOW_RESIZABLE);
    if (!win) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(win, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    /* scale renderer output for High-DPI displays */
    {
        int rw, rh, ww, wh;
        SDL_GetRendererOutputSize(renderer, &rw, &rh);
        SDL_GetWindowSize(win, &ww, &wh);
        SDL_RenderSetScale(renderer, (float)rw / ww, (float)rh / wh);
        font_scale = (float)rh / wh;
    }

    ctx = nk_sdl_init(win, renderer);
    {
        /* Cyrillic-capable TTF candidates (the built-in Nuklear font has no
         * Cyrillic glyphs). Fall back to the default font if none is found. */
        static const char *font_paths[] = {
            "/System/Library/Fonts/Supplemental/Arial.ttf",
            "/System/Library/Fonts/Supplemental/Verdana.ttf",
            "/Library/Fonts/Arial.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            NULL
        };
        struct nk_font_atlas *atlas;
        struct nk_font_config cfg = nk_font_config(0);
        int i;

        cfg.range = nk_font_cyrillic_glyph_ranges();
        font = NULL;
        nk_sdl_font_stash_begin(&atlas);
        for (i = 0; font_paths[i]; i++) {
            FILE *f = fopen(font_paths[i], "rb");
            if (f) {
                fclose(f);
                font = nk_font_atlas_add_from_file(atlas, font_paths[i],
                                                   15 * font_scale, &cfg);
                if (font) break;
            }
        }
        if (!font) /* no Cyrillic TTF available: Latin-only fallback */
            font = nk_font_atlas_add_default(atlas, 14 * font_scale, &cfg);
        nk_sdl_font_stash_end();
        font->handle.height /= font_scale;
        nk_style_set_font(ctx, &font->handle);
    }

    /* breathing room around the edges so widgets don't hug the border */
    ctx->style.window.padding.x = 14.0f;
    ctx->style.window.padding.y = 12.0f;

    do_scan(&app, SDL_GetTicks64());

    while (running) {
        SDL_Event evt;
        Uint64 now;
        int ww, wh;

        nk_input_begin(ctx);
        while (SDL_PollEvent(&evt)) {
            if (evt.type == SDL_QUIT)
                running = 0;
            nk_sdl_handle_event(&evt);
        }
        nk_sdl_handle_grab();
        nk_input_end(ctx);

        now = SDL_GetTicks64();
        serial_pump(&app, now);
        poll_tick(&app, now);

        SDL_GetWindowSize(win, &ww, &wh);
        build_ui(ctx, &app, now, ww, wh);

        SDL_SetRenderDrawColor(renderer, 30, 30, 35, 255);
        SDL_RenderClear(renderer);
        nk_sdl_render(NK_ANTI_ALIASING_ON);
        SDL_RenderPresent(renderer);
    }

    do_disconnect(&app, SDL_GetTicks64());
    nk_sdl_shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
