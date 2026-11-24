//! The widget's look: two palettes around the Open Development blue, Inter
//! with Phosphor icons, and the few controls the window is built from.

use eframe::egui::{
    self, Color32, CornerRadius, FontData, FontDefinitions, FontFamily, FontId, Frame, Margin,
    Pos2, Rect, Response, RichText, Sense, Stroke, StrokeKind, TextStyle, Ui, Vec2, Visuals,
};

use crate::glyph;

pub struct Palette {
    /// Window background.
    pub base: Color32,
    /// Settings drawer background.
    pub panel: Color32,
    pub control: Color32,
    pub control_hover: Color32,
    pub line: Color32,
    pub text: Color32,
    pub sub: Color32,
    pub faint: Color32,
    pub brand: Color32,
    pub good: Color32,
    pub caution: Color32,
    pub alert: Color32,
    pub idle: Color32,
    dark: bool,
}

const fn rgb(r: u8, g: u8, b: u8) -> Color32 {
    Color32::from_rgb(r, g, b)
}

pub const DARK: Palette = Palette {
    base: rgb(22, 25, 30),
    panel: rgb(28, 32, 38),
    control: rgb(38, 44, 52),
    control_hover: rgb(48, 55, 66),
    line: rgb(46, 52, 61),
    text: rgb(230, 234, 239),
    sub: rgb(152, 162, 175),
    faint: rgb(102, 112, 125),
    brand: rgb(43, 168, 232),
    good: rgb(53, 194, 122),
    caution: rgb(239, 164, 58),
    alert: rgb(239, 95, 95),
    idle: rgb(127, 137, 150),
    dark: true,
};

pub const LIGHT: Palette = Palette {
    base: rgb(255, 255, 255),
    panel: rgb(245, 247, 249),
    control: rgb(235, 239, 243),
    control_hover: rgb(223, 229, 235),
    line: rgb(220, 226, 232),
    text: rgb(23, 32, 43),
    sub: rgb(90, 102, 117),
    faint: rgb(152, 162, 174),
    brand: rgb(0, 137, 199),
    good: rgb(26, 150, 85),
    caution: rgb(184, 110, 0),
    alert: rgb(207, 52, 52),
    idle: rgb(139, 149, 161),
    dark: false,
};

pub fn pal(ui: &Ui) -> &'static Palette {
    if ui.visuals().dark_mode {
        &DARK
    } else {
        &LIGHT
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Tone {
    Good,
    Caution,
    Alert,
    Idle,
    Brand,
}

impl Palette {
    pub fn tone(&self, tone: Tone) -> Color32 {
        match tone {
            Tone::Good => self.good,
            Tone::Caution => self.caution,
            Tone::Alert => self.alert,
            Tone::Idle => self.idle,
            Tone::Brand => self.brand,
        }
    }

    /// The tone as a faint wash for backgrounds.
    pub fn wash(&self, tone: Tone) -> Color32 {
        let c = self.tone(tone);
        Color32::from_rgba_unmultiplied(c.r(), c.g(), c.b(), if self.dark { 40 } else { 28 })
    }
}

// ---- fonts and visuals ---------------------------------------------------

pub fn bold() -> FontFamily {
    FontFamily::Name("bold".into())
}

pub fn install(ctx: &egui::Context) {
    let mut fonts = FontDefinitions::default();
    for (name, bytes) in [
        (
            "inter",
            &include_bytes!("../assets/fonts/Inter-Regular-subset.ttf")[..],
        ),
        (
            "inter-semibold",
            &include_bytes!("../assets/fonts/Inter-SemiBold-subset.ttf")[..],
        ),
        (
            "phosphor",
            &include_bytes!("../assets/fonts/Phosphor-subset.ttf")[..],
        ),
    ] {
        fonts
            .font_data
            .insert(name.into(), FontData::from_static(bytes).into());
    }
    let fallbacks = fonts
        .families
        .get(&FontFamily::Proportional)
        .cloned()
        .unwrap_or_default();
    let family = |first: &str| {
        let mut keys = vec![first.to_string(), "phosphor".to_string()];
        keys.extend(fallbacks.iter().cloned());
        keys
    };
    fonts
        .families
        .insert(FontFamily::Proportional, family("inter"));
    fonts.families.insert(bold(), family("inter-semibold"));
    if let Some(mono) = fonts.families.get_mut(&FontFamily::Monospace) {
        mono.push("phosphor".into());
    }
    ctx.set_fonts(fonts);

    ctx.all_styles_mut(|style| {
        style.text_styles = [
            (TextStyle::Heading, FontId::new(15.0, bold())),
            (TextStyle::Body, FontId::new(13.0, FontFamily::Proportional)),
            (
                TextStyle::Button,
                FontId::new(13.0, FontFamily::Proportional),
            ),
            (
                TextStyle::Small,
                FontId::new(11.5, FontFamily::Proportional),
            ),
            (
                TextStyle::Monospace,
                FontId::new(12.0, FontFamily::Monospace),
            ),
        ]
        .into();
        let s = &mut style.spacing;
        s.item_spacing = Vec2::new(6.0, 6.0);
        s.button_padding = Vec2::new(10.0, 4.0);
        s.interact_size.y = 26.0;
        s.combo_width = 120.0;
        s.menu_margin = Margin::same(4);
        s.scroll = egui::style::ScrollStyle::thin();
        style.interaction.tooltip_delay = 0.4;
    });
    ctx.set_visuals_of(egui::Theme::Dark, visuals(&DARK));
    ctx.set_visuals_of(egui::Theme::Light, visuals(&LIGHT));
}

fn visuals(p: &Palette) -> Visuals {
    let mut v = if p.dark {
        Visuals::dark()
    } else {
        Visuals::light()
    };
    let radius = CornerRadius::same(6);
    v.panel_fill = p.base;
    v.window_fill = p.panel;
    v.window_stroke = Stroke::new(1.0, p.line);
    v.window_corner_radius = CornerRadius::same(8);
    v.menu_corner_radius = CornerRadius::same(8);
    v.extreme_bg_color = p.control;
    v.text_edit_bg_color = Some(p.control);
    v.faint_bg_color = p.panel;
    v.code_bg_color = p.control;
    v.hyperlink_color = p.brand;
    v.warn_fg_color = p.caution;
    v.error_fg_color = p.alert;
    v.selection.bg_fill = p.brand;
    v.selection.stroke = Stroke::new(1.0, Color32::WHITE);
    v.text_cursor.stroke = Stroke::new(1.5, p.brand);
    v.weak_text_color = Some(p.sub);
    let w = &mut v.widgets;
    w.noninteractive.bg_fill = p.panel;
    w.noninteractive.weak_bg_fill = p.panel;
    w.noninteractive.bg_stroke = Stroke::new(1.0, p.line);
    w.noninteractive.fg_stroke = Stroke::new(1.0, p.text);
    w.noninteractive.corner_radius = radius;
    for (state, fill) in [
        (&mut w.inactive, p.control),
        (&mut w.hovered, p.control_hover),
        (&mut w.active, p.control_hover),
        (&mut w.open, p.control_hover),
    ] {
        state.bg_fill = fill;
        state.weak_bg_fill = fill;
        state.corner_radius = radius;
        state.fg_stroke = Stroke::new(1.0, p.text);
        state.bg_stroke = Stroke::NONE;
        state.expansion = 0.0;
    }
    w.active.bg_stroke = Stroke::new(1.0, p.brand);
    v
}

// ---- text ------------------------------------------------------------------

pub fn strong(text: impl Into<String>) -> RichText {
    RichText::new(text).family(bold())
}

/// Small caps label above a group of settings.
pub fn heading(ui: &mut Ui, text: &str) {
    let p = pal(ui);
    ui.add_space(4.0);
    ui.label(
        RichText::new(text.to_uppercase())
            .family(bold())
            .size(10.5)
            .extra_letter_spacing(0.8)
            .color(p.faint),
    );
}

pub fn sub(ui: &mut Ui, text: impl Into<String>) -> Response {
    let color = pal(ui).sub;
    ui.label(RichText::new(text).color(color).size(12.0))
}

// ---- controls ----------------------------------------------------------------

/// A square icon button. `active` marks a switched-on state, `hot` tints it
/// on hover (destructive actions).
pub fn tool(ui: &mut Ui, glyph: &str, tip: &str, active: bool, hot: Option<Tone>) -> Response {
    let p = pal(ui);
    let size = Vec2::splat(30.0);
    let (rect, response) = ui.allocate_exact_size(size, Sense::click());
    if ui.is_rect_visible(rect) {
        let enabled = ui.is_enabled();
        let hovered = enabled && response.hovered();
        let accent = hot.map_or(p.brand, |t| p.tone(t));
        let fill = if active {
            p.wash(Tone::Brand)
        } else if hovered {
            hot.map_or(p.control_hover, |t| p.wash(t))
        } else {
            p.control
        };
        let color = if !enabled {
            p.faint
        } else if active || (hovered && hot.is_some()) {
            accent
        } else if hovered {
            p.text
        } else {
            p.sub
        };
        let painter = ui.painter();
        painter.rect_filled(rect, CornerRadius::same(7), fill);
        if response.has_focus() {
            painter.rect_stroke(
                rect,
                CornerRadius::same(7),
                Stroke::new(1.0, p.brand),
                StrokeKind::Inside,
            );
        }
        painter.text(
            rect.center(),
            egui::Align2::CENTER_CENTER,
            glyph,
            FontId::proportional(16.0),
            color,
        );
    }
    response.on_hover_text(tip)
}

/// A bare icon for the corner of the window (settings, close).
pub fn corner(ui: &mut Ui, glyph: &str, tip: &str, active: bool) -> Response {
    let p = pal(ui);
    let (rect, response) = ui.allocate_exact_size(Vec2::splat(22.0), Sense::click());
    if ui.is_rect_visible(rect) {
        if response.hovered() || active {
            ui.painter()
                .rect_filled(rect, CornerRadius::same(5), p.control);
        }
        let color = if active || response.hovered() {
            p.text
        } else {
            p.faint
        };
        ui.painter().text(
            rect.center(),
            egui::Align2::CENTER_CENTER,
            glyph,
            FontId::proportional(14.0),
            color,
        );
    }
    response.on_hover_text(tip)
}

/// A coloured dot and a short text, sized to fit.
pub fn tag(ui: &mut Ui, color: Color32, text: &str) -> Response {
    let p = pal(ui);
    let galley = ui
        .painter()
        .layout_no_wrap(text.to_string(), FontId::proportional(12.0), p.sub);
    let size = Vec2::new(12.0 + galley.size().x, galley.size().y.max(14.0));
    let (rect, response) = ui.allocate_exact_size(size, Sense::hover());
    if ui.is_rect_visible(rect) {
        ui.painter()
            .circle_filled(Pos2::new(rect.left() + 3.5, rect.center().y), 3.5, color);
        ui.painter().galley(
            Pos2::new(rect.left() + 12.0, rect.center().y - galley.size().y / 2.0),
            galley,
            p.sub,
        );
    }
    response
}

/// An on/off switch.
pub fn toggle(ui: &mut Ui, on: &mut bool) -> Response {
    let p = pal(ui);
    let size = Vec2::new(32.0, 18.0);
    let (rect, mut response) = ui.allocate_exact_size(size, Sense::click());
    if response.clicked() {
        *on = !*on;
        response.mark_changed();
    }
    response
        .widget_info(|| egui::WidgetInfo::selected(egui::Role::CheckBox, ui.is_enabled(), *on, ""));
    if ui.is_rect_visible(rect) {
        let t = ui.ctx().animate_bool_responsive(response.id, *on);
        let off = if response.hovered() {
            p.control_hover
        } else {
            p.line
        };
        let track = lerp(off, if ui.is_enabled() { p.brand } else { p.faint }, t);
        let r = rect.height() / 2.0;
        ui.painter().rect_filled(rect, r, track);
        let x = egui::lerp((rect.left() + r)..=(rect.right() - r), t);
        ui.painter()
            .circle_filled(Pos2::new(x, rect.center().y), r - 2.5, Color32::WHITE);
    }
    response
}

fn lerp(a: Color32, b: Color32, t: f32) -> Color32 {
    let m = |x: u8, y: u8| (f32::from(x) + (f32::from(y) - f32::from(x)) * t).round() as u8;
    Color32::from_rgb(m(a.r(), b.r()), m(a.g(), b.g()), m(a.b(), b.b()))
}

/// One settings line: icon and label on the left, `control` on the right.
pub fn row<R>(ui: &mut Ui, glyph: &str, label: &str, control: impl FnOnce(&mut Ui) -> R) -> R {
    row_tip(ui, glyph, label, None, control)
}

/// [`row`] with an explanation on the label.
pub fn row_tip<R>(
    ui: &mut Ui,
    glyph: &str,
    label: &str,
    tip: Option<&str>,
    control: impl FnOnce(&mut Ui) -> R,
) -> R {
    let p = pal(ui);
    let height = 30.0;
    let width = ui.available_width();
    ui.allocate_ui_with_layout(
        Vec2::new(width, height),
        egui::Layout::left_to_right(egui::Align::Center),
        |ui| {
            ui.set_min_size(Vec2::new(width, height));
            // A fixed icon column keeps the labels aligned, with or without one.
            let (cell, _) = ui.allocate_exact_size(Vec2::new(17.0, height), Sense::hover());
            ui.painter().text(
                cell.center(),
                egui::Align2::CENTER_CENTER,
                glyph,
                FontId::proportional(15.0),
                p.sub,
            );
            let label = ui.label(RichText::new(label).color(p.text));
            if let Some(tip) = tip {
                label.on_hover_text(tip);
            }
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), control)
                .inner
        },
    )
    .inner
}

/// A row of mutually exclusive choices.
pub fn choice<T: PartialEq + Copy>(ui: &mut Ui, current: &mut T, items: &[(T, &str)]) -> bool {
    let p = pal(ui);
    let mut changed = false;
    Frame::new()
        .fill(p.control)
        .corner_radius(CornerRadius::same(7))
        .inner_margin(Margin::same(2))
        .show(ui, |ui| {
            ui.spacing_mut().item_spacing.x = 2.0;
            // A right-to-left parent (a settings row) would reverse them.
            let rtl = ui.layout().main_dir() == egui::Direction::RightToLeft;
            let ordered: Vec<&(T, &str)> = if rtl {
                items.iter().rev().collect()
            } else {
                items.iter().collect()
            };
            ui.horizontal(|ui| {
                for (value, label) in ordered {
                    let selected = *current == *value;
                    let text = RichText::new(*label).size(12.0).color(if selected {
                        p.text
                    } else {
                        p.sub
                    });
                    let text = if selected { text.family(bold()) } else { text };
                    let mut b = egui::Button::new(text)
                        .corner_radius(CornerRadius::same(5))
                        .min_size(Vec2::new(0.0, 22.0))
                        .frame_when_inactive(selected);
                    if selected {
                        b = b.fill(p.base);
                    }
                    if ui.add(b).clicked() && !selected {
                        *current = *value;
                        changed = true;
                    }
                }
            });
        });
    changed
}

/// `− value +` for a small number. The buttons repeat while held.
pub fn stepper(
    ui: &mut Ui,
    value: &mut u8,
    range: std::ops::RangeInclusive<u8>,
    format: impl Fn(u8) -> String,
) -> bool {
    let p = pal(ui);
    let mut changed = false;
    Frame::new()
        .fill(p.control)
        .corner_radius(CornerRadius::same(7))
        .inner_margin(Margin::same(2))
        .show(ui, |ui| {
            ui.spacing_mut().item_spacing.x = 0.0;
            let enabled = ui.is_enabled();
            let mut step = |ui: &mut Ui, value: &mut u8, up: bool| {
                let (glyph, can) = if up {
                    (glyph::PLUS, *value < *range.end())
                } else {
                    (glyph::MINUS, *value > *range.start())
                };
                if step_button(ui, glyph, enabled && can) {
                    *value = if up { *value + 1 } else { *value - 1 };
                    changed = true;
                }
            };
            // A right-to-left parent (a settings row) lays out backwards.
            let rtl = ui.layout().main_dir() == egui::Direction::RightToLeft;
            ui.horizontal(|ui| {
                step(ui, value, rtl);
                let (rect, _) = ui.allocate_exact_size(Vec2::new(98.0, 22.0), Sense::hover());
                ui.painter().text(
                    rect.center(),
                    egui::Align2::CENTER_CENTER,
                    format(*value),
                    FontId::proportional(12.5),
                    if enabled { p.text } else { p.faint },
                );
                step(ui, value, !rtl);
            });
        });
    changed
}

/// Fires when pressed, then repeats while the button is held.
fn step_button(ui: &mut Ui, glyph: &str, enabled: bool) -> bool {
    let p = pal(ui);
    let sense = if enabled {
        Sense::click()
    } else {
        Sense::hover()
    };
    let (rect, response) = ui.allocate_exact_size(Vec2::new(24.0, 22.0), sense);
    let hovered = enabled && response.hovered();
    if hovered {
        ui.painter()
            .rect_filled(rect, CornerRadius::same(5), p.control_hover);
    }
    let color = match (enabled, hovered) {
        (false, _) => p.faint,
        (true, true) => p.text,
        (true, false) => p.sub,
    };
    ui.painter().text(
        rect.center(),
        egui::Align2::CENTER_CENTER,
        glyph,
        FontId::proportional(13.0),
        color,
    );
    let id = response.id;
    if !(enabled && response.is_pointer_button_down_on()) {
        ui.data_mut(|d| d.remove::<f64>(id));
        return false;
    }
    let now = ui.input(|i| i.time);
    ui.ctx().request_repaint();
    match ui.data(|d| d.get_temp::<f64>(id)) {
        None => {
            ui.data_mut(|d| d.insert_temp(id, now + 0.45));
            true
        }
        Some(next) if now >= next => {
            ui.data_mut(|d| d.insert_temp(id, now + 0.06));
            true
        }
        Some(_) => false,
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Primary,
    Danger,
    Plain,
}

pub fn button(ui: &mut Ui, kind: Kind, text: &str) -> Response {
    let p = pal(ui);
    let b = match kind {
        Kind::Primary => egui::Button::new(strong(text).color(Color32::WHITE)).fill(p.brand),
        Kind::Danger => egui::Button::new(strong(text).color(Color32::WHITE)).fill(p.alert),
        Kind::Plain => egui::Button::new(text),
    };
    ui.add(
        b.corner_radius(CornerRadius::same(6))
            .min_size(Vec2::new(0.0, 26.0)),
    )
}

/// A tinted box for notes and warnings.
pub fn note(ui: &mut Ui, tone: Tone, glyph: &str, add: impl FnOnce(&mut Ui)) {
    let p = pal(ui);
    Frame::new()
        .fill(p.wash(tone))
        .corner_radius(CornerRadius::same(6))
        .inner_margin(Margin::symmetric(10, 8))
        .show(ui, |ui| {
            ui.set_min_width(ui.available_width());
            ui.horizontal_top(|ui| {
                ui.label(RichText::new(glyph).size(15.0).color(p.tone(tone)));
                ui.vertical(|ui| {
                    ui.spacing_mut().item_spacing.y = 4.0;
                    add(ui)
                });
            });
        });
}

/// The status disc. `pulse` is the time since the last heartbeat: the disc
/// beats and sends out a ring. `spin` turns the glyph into a rotating arc.
pub fn disc(ui: &Ui, rect: Rect, tone: Tone, glyph: &str, pulse: Option<f32>, spin: bool) {
    let p = pal(ui);
    let painter = ui.painter();
    let c = rect.center();
    let beat = pulse
        .filter(|t| *t < 0.25)
        .map_or(0.0, |t| 2.0 * (1.0 - t / 0.25));
    let r = rect.width().min(rect.height()) / 2.0 - 4.0;
    let color = p.tone(tone);
    if let Some(t) = pulse.filter(|t| *t < 0.9) {
        let k = t / 0.9;
        let alpha = ((1.0 - k) * 120.0) as u8;
        painter.circle_stroke(
            c,
            r + 4.0 * k,
            Stroke::new(
                3.0 * (1.0 - k) + 0.5,
                Color32::from_rgba_unmultiplied(color.r(), color.g(), color.b(), alpha),
            ),
        );
        ui.ctx().request_repaint();
    }
    painter.circle_filled(c, r + beat, p.wash(tone));
    painter.circle_stroke(c, r + beat, Stroke::new(1.5, color));
    if spin {
        let t = ui.input(|i| i.time) as f32;
        let a0 = t * 4.0;
        let points: Vec<Pos2> = (0..=24)
            .map(|i| {
                let a = a0 + i as f32 / 24.0 * std::f32::consts::PI * 1.3;
                c + Vec2::angled(a) * (r * 0.45)
            })
            .collect();
        painter.add(egui::Shape::line(points, Stroke::new(2.5, color)));
        ui.ctx().request_repaint();
    } else if !glyph.is_empty() {
        painter.text(
            c,
            egui::Align2::CENTER_CENTER,
            glyph,
            FontId::proportional(r * 0.95),
            color,
        );
    }
}
