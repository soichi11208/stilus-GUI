// src/widgets.cpp — core widget implementations
#include "stilus/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace stilus {

static const Font& pick_font(const Theme& t, bool bold) {
    if (bold && t.font_bold && t.font_bold->valid()) return *t.font_bold;
    if (t.font && t.font->valid())                   return *t.font;
    static Font empty;
    return empty;
}

// ---------------------------------------------------------------------------
// Label
// ---------------------------------------------------------------------------
Size Label::measure(const Constraints& c) {
    // We don't have a theme here — defer detailed measurement to paint using
    // ascent/descent; return an approximation based on pixel_size_.
    float h = pixel_size_ > 0 ? pixel_size_ : 20.0f;
    float w = h * float(text_.size()) * 0.5f; // rough
    Size s{std::min(w, c.max_w), std::min(h, c.max_h)};
    if (s.w < c.min_w) s.w = c.min_w;
    if (s.h < c.min_h) s.h = c.min_h;
    return s;
}

void Label::paint(Canvas& c, const Theme& t) {
    const Font& f = pick_font(t, bold_);
    if (!f.valid()) return;
    Color col = has_color_ ? color_ : t.text;
    float baseline = rect_.y + f.ascent();
    c.draw_text({rect_.x, baseline}, text_, f, col);
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------
Size Button::measure(const Constraints& c) {
    // Prefer "label width + padding" assumptions; caller clamps.
    float fh = 22.f;
    float fw = fh * float(label_.size()) * 0.55f;
    Size s{std::min(fw + 28.f, c.max_w), std::min(fh + 18.f, c.max_h)};
    if (s.w < c.min_w) s.w = c.min_w;
    if (s.h < c.min_h) s.h = c.min_h;
    return s;
}

void Button::paint(Canvas& c, const Theme& t) {
    Color bg = primary_ ? t.primary : t.surface;
    if (hover_)   bg = Color{bg.r * 1.12f, bg.g * 1.12f, bg.b * 1.12f, bg.a};
    if (active_)  bg = Color{bg.r * 0.85f, bg.g * 0.85f, bg.b * 0.85f, bg.a};

    c.fill_rounded_rect(rect_, t.radius, bg);
    if (focused_) {
        c.stroke_rounded_rect(rect_, t.radius, 2.f, t.primary);
    } else if (!primary_) {
        c.stroke_rounded_rect(rect_, t.radius, 1.f, t.border);
    }

    const Font& f = pick_font(t, primary_);
    if (f.valid()) {
        float baseline = rect_.y + (rect_.h + f.ascent() - f.descent()) * 0.5f;
        Color fg = primary_ ? t.bg : t.text;
        c.draw_text({rect_.x + (rect_.w - f.measure(label_)) * 0.5f, baseline}, label_, f, fg);
    }
}

bool Button::on_event(const Event& e) {
    using T = Event::Type;
    bool was_hover  = hover_;
    bool was_active = active_;
    switch (e.type) {
    case T::MouseMove:
        hover_ = rect_.contains({e.x, e.y});
        break;
    case T::MouseDown:
        if (rect_.contains({e.x, e.y})) {
            active_ = true;
            set_focused(true);
            invalidate();
            return true;
        }
        break;
    case T::MouseUp:
        if (active_) {
            active_ = false;
            bool inside = rect_.contains({e.x, e.y});
            invalidate();
            if (inside && on_click_) on_click_();
            return true;
        }
        break;
    case T::KeyDown:
        if (focused_ && (e.key == Key::Enter || e.key == Key::Space)) {
            if (on_click_) on_click_();
            return true;
        }
        break;
    default: break;
    }
    if (hover_ != was_hover || active_ != was_active) invalidate();
    return false;
}

// ---------------------------------------------------------------------------
// Slider
// ---------------------------------------------------------------------------
void Slider::set_value(float v) {
    if (v < min_) v = min_;
    if (v > max_) v = max_;
    if (v != value_) {
        value_ = v;
        if (on_change_) on_change_(v);
        invalidate();
    }
}

Size Slider::measure(const Constraints& c) {
    Size s{std::min(200.f, c.max_w), std::min(24.f, c.max_h)};
    if (s.w < c.min_w) s.w = c.min_w;
    if (s.h < c.min_h) s.h = c.min_h;
    return s;
}

void Slider::paint(Canvas& c, const Theme& t) {
    float cy  = rect_.y + rect_.h * 0.5f;
    float track_h = 6.f;
    Rect  track{rect_.x, cy - track_h * 0.5f, rect_.w, track_h};
    c.fill_rounded_rect(track, track_h * 0.5f, t.surface_hi);

    float frac = (value_ - min_) / (max_ - min_);
    if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
    Rect fill{rect_.x, track.y, rect_.w * frac, track_h};
    c.fill_rounded_rect(fill, track_h * 0.5f, t.primary);

    float knob_r = 10.f;
    Vec2 knob{rect_.x + rect_.w * frac, cy};
    if (focused_) c.fill_circle(knob, knob_r + 3.f, t.primary);
    c.fill_circle(knob, knob_r, t.text);
}

bool Slider::on_event(const Event& e) {
    using T = Event::Type;
    auto update_from_x = [&](float x) {
        float frac = (x - rect_.x) / rect_.w;
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        set_value(min_ + frac * (max_ - min_));
    };
    switch (e.type) {
    case T::MouseDown:
        if (rect_.contains({e.x, e.y})) {
            dragging_ = true;
            set_focused(true);
            update_from_x(e.x);
            return true;
        }
        break;
    case T::MouseMove:
        if (dragging_) { update_from_x(e.x); return true; }
        break;
    case T::MouseUp:
        if (dragging_) { dragging_ = false; return true; }
        break;
    case T::KeyDown:
        if (focused_) {
            float step = (max_ - min_) * 0.05f;
            if (e.key == Key::Left)  { set_value(value_ - step); return true; }
            if (e.key == Key::Right) { set_value(value_ + step); return true; }
            if (e.key == Key::Home)  { set_value(min_);          return true; }
            if (e.key == Key::End)   { set_value(max_);          return true; }
        }
        break;
    default: break;
    }
    return false;
}

// ---------------------------------------------------------------------------
// TextInput
// ---------------------------------------------------------------------------
Size TextInput::measure(const Constraints& c) {
    Size s{std::min(220.f, c.max_w), std::min(34.f, c.max_h)};
    if (s.w < c.min_w) s.w = c.min_w;
    if (s.h < c.min_h) s.h = c.min_h;
    return s;
}

void TextInput::paint(Canvas& c, const Theme& t) {
    c.fill_rounded_rect(rect_, t.radius * 0.7f, t.surface);
    Color border = focused_ ? t.primary : t.border;
    c.stroke_rounded_rect(rect_, t.radius * 0.7f, focused_ ? 2.f : 1.f, border);

    const Font& f = pick_font(t, false);
    if (!f.valid()) return;
    paint_font_ = &f;

    float pad = 8.f;
    float baseline = rect_.y + (rect_.h + f.ascent() - f.descent()) * 0.5f;
    paint_text_x_ = rect_.x + pad;

    c.push_clip({rect_.x + pad, rect_.y, rect_.w - pad * 2, rect_.h});
    if (text_.empty() && preedit_.empty() && !focused_ && !placeholder_.empty()) {
        c.draw_text({rect_.x + pad, baseline}, placeholder_, f, t.text_dim);
    } else {
        // Committed text before the cursor.
        std::string before = text_.substr(0, cursor_);
        std::string after  = text_.substr(cursor_);
        float x = rect_.x + pad;
        c.draw_text({x, baseline}, before, f, t.text);
        x += f.measure(before);
        if (!preedit_.empty()) {
            // Draw preedit in a dimmer color with an underline to mark it as
            // composing text not yet committed.
            float pw = f.measure(preedit_);
            c.draw_text({x, baseline}, preedit_, f, t.text_dim);
            c.fill_rect({x, rect_.y + rect_.h - 4, pw, 1.f}, t.primary);
            x += pw;
        }
        c.draw_text({x, baseline}, after, f, t.text);
    }
    // Cursor. When a preedit is active the caret sits at the preedit's
    // logical cursor position (offset within the preedit string, if given).
    if (focused_) {
        std::string before = text_.substr(0, cursor_);
        float cx = rect_.x + pad + f.measure(before);
        if (!preedit_.empty()) {
            int pc = preedit_cursor_;
            if (pc < 0 || pc > int(preedit_.size())) pc = int(preedit_.size());
            cx += f.measure(preedit_.substr(0, pc));
        }
        c.fill_rect({cx, rect_.y + 6, 1.5f, rect_.h - 12}, t.text);
    }
    c.pop_clip();
}

// Remove up to N bytes of a UTF-8 sequence ending at cursor_.
static void utf8_erase_before(std::string& s, size_t& cursor) {
    if (cursor == 0) return;
    size_t i = cursor;
    // back up to the first byte (not 10xxxxxx)
    do { --i; } while (i > 0 && (uint8_t(s[i]) & 0xC0) == 0x80);
    s.erase(i, cursor - i);
    cursor = i;
}

// Delete one codepoint starting at cursor_.
static void utf8_erase_at(std::string& s, size_t cursor) {
    if (cursor >= s.size()) return;
    size_t j = cursor + 1;
    while (j < s.size() && (uint8_t(s[j]) & 0xC0) == 0x80) ++j;
    s.erase(cursor, j - cursor);
}

// Move cursor one codepoint left/right (clamped).
static size_t utf8_prev(const std::string& s, size_t cursor) {
    if (cursor == 0) return 0;
    size_t i = cursor;
    do { --i; } while (i > 0 && (uint8_t(s[i]) & 0xC0) == 0x80);
    return i;
}
static size_t utf8_next(const std::string& s, size_t cursor) {
    if (cursor >= s.size()) return s.size();
    size_t j = cursor + 1;
    while (j < s.size() && (uint8_t(s[j]) & 0xC0) == 0x80) ++j;
    return j;
}

// Find the UTF-8-aligned cursor byte index whose pixel position is closest
// to `target_x` (in absolute window coords), measuring with `f`.
static size_t pick_cursor_from_x(const std::string& s, const Font& f,
                                 float x0, float target_x) {
    if (s.empty() || !f.valid()) return 0;
    size_t best = 0;
    float best_d = std::fabs(x0 - target_x);
    size_t i = 0;
    while (i < s.size()) {
        size_t j = utf8_next(s, i);
        float w = f.measure(s.substr(0, j));
        float d = std::fabs((x0 + w) - target_x);
        if (d < best_d) { best_d = d; best = j; }
        i = j;
    }
    return best;
}

bool TextInput::on_event(const Event& e) {
    using T = Event::Type;
    switch (e.type) {
    case T::MouseDown: {
        bool inside = rect_.contains({e.x, e.y});
        set_focused(inside);
        if (inside) {
            if (paint_font_ && paint_font_->valid()) {
                cursor_ = pick_cursor_from_x(text_, *paint_font_,
                                             paint_text_x_, e.x);
            } else {
                cursor_ = text_.size();
            }
            invalidate();
            return true;
        }
        break;
    }
    case T::KeyDown: {
        if (!focused_) return false;
        if (e.key == Key::Backspace) {
            utf8_erase_before(text_, cursor_);
            if (on_change_) on_change_(text_);
            invalidate();
            return true;
        }
        if (e.key == Key::Delete) {
            utf8_erase_at(text_, cursor_);
            if (on_change_) on_change_(text_);
            invalidate();
            return true;
        }
        if (e.key == Key::Left) {
            cursor_ = utf8_prev(text_, cursor_);
            invalidate();
            return true;
        }
        if (e.key == Key::Right) {
            cursor_ = utf8_next(text_, cursor_);
            invalidate();
            return true;
        }
        if (e.key == Key::Home) {
            cursor_ = 0;
            invalidate();
            return true;
        }
        if (e.key == Key::End) {
            cursor_ = text_.size();
            invalidate();
            return true;
        }
        if (e.key == Key::Enter) {
            if (on_submit_) on_submit_(text_);
            return true;
        }
        break;
    }
    case T::TextInput: {
        if (!focused_) return false;
        if (e.text.empty()) break;
        // Ignore control characters — printable UTF-8 only.
        unsigned char b0 = (unsigned char)e.text[0];
        if (b0 < 0x20 || b0 == 0x7F) break;
        text_.insert(cursor_, e.text);
        cursor_ += e.text.size();
        preedit_.clear();
        if (on_change_) on_change_(text_);
        invalidate();
        return true;
    }
    case T::Preedit: {
        if (!focused_) return false;
        preedit_         = e.text;
        preedit_cursor_  = e.preedit_cursor_begin;
        invalidate();
        return true;
    }
    default: break;
    }
    return false;
}

// ---------------------------------------------------------------------------
// CheckBox
// ---------------------------------------------------------------------------
Size CheckBox::measure(const Constraints& c) {
    float h = 22.f;
    float w = h * float(text_.size()) * 0.45f + 30.f;
    Size s{std::min(w, c.max_w), std::min(h, c.max_h)};
    if (s.w < c.min_w) s.w = c.min_w;
    if (s.h < c.min_h) s.h = c.min_h;
    return s;
}

void CheckBox::paint(Canvas& c, const Theme& t) {
    const Font& f = pick_font(t, false);
    if (!f.valid()) return;

    float box_size = 18.f;
    float box_x = rect_.x + 4.f;
    float box_y = rect_.y + (rect_.h - box_size) * 0.5f;
    Rect box{box_x, box_y, box_size, box_size};

    // Box background
    Color box_bg = checked_ ? t.primary : t.surface;
    Color box_border = checked_ ? t.primary
                                 : (focused_ || hover_ ? t.primary : t.border);
    c.fill_rounded_rect(box, 4.f, box_bg);
    c.stroke_rounded_rect(box, 4.f, focused_ ? 2.f : 1.2f, box_border);

    // Checkmark
    if (checked_) {
        float mx = box_x + box_size * 0.5f;
        float my = box_y + box_size * 0.5f;
        float s = 6.f;
        Path p;
        p.move_to({mx - s * 0.6f, my});
        p.line_to({mx - s * 0.1f, my + s * 0.5f});
        p.line_to({mx + s * 0.7f, my - s * 0.5f});
        c.fill_path(p, t.bg, FillRule::NonZero);
    }

    // Label
    float text_x = box_x + box_size + 8.f;
    float baseline = rect_.y + (rect_.h + f.ascent() - f.descent()) * 0.5f;
    c.draw_text({text_x, baseline}, text_, f, t.text);
}

bool CheckBox::on_event(const Event& e) {
    using T = Event::Type;
    bool was_hover = hover_;
    switch (e.type) {
    case T::MouseMove:
        hover_ = rect_.contains({e.x, e.y});
        break;
    case T::MouseDown:
        if (rect_.contains({e.x, e.y})) {
            checked_ = !checked_;
            set_focused(true);
            invalidate();
            if (on_change_) on_change_(checked_);
            return true;
        }
        break;
    case T::KeyDown:
        if (focused_ && (e.key == Key::Space || e.key == Key::Enter)) {
            checked_ = !checked_;
            invalidate();
            if (on_change_) on_change_(checked_);
            return true;
        }
        break;
    default: break;
    }
    if (hover_ != was_hover) invalidate();
    return false;
}

// ---------------------------------------------------------------------------
// ProgressBar
// ---------------------------------------------------------------------------
Size ProgressBar::measure(const Constraints& c) {
    Size s{std::min(200.f, c.max_w), std::min(14.f, c.max_h)};
    if (s.w < c.min_w) s.w = c.min_w;
    if (s.h < c.min_h) s.h = c.min_h;
    return s;
}

void ProgressBar::paint(Canvas& c, const Theme& t) {
    float track_h = rect_.h;
    Rect track{rect_.x, rect_.y, rect_.w, track_h};
    c.fill_rounded_rect(track, track_h * 0.5f, t.surface);

    Rect fill{track.x, track.y, track.w * value_, track_h};
    c.fill_rounded_rect(fill, track_h * 0.5f, t.primary);
}

// ---------------------------------------------------------------------------
// Image
// ---------------------------------------------------------------------------
Size Image::measure(const Constraints& c) {
    Size s{
        std::min(float(img_.width), c.max_w),
        std::min(float(img_.height), c.max_h)
    };
    if (s.w < c.min_w) s.w = c.min_w;
    if (s.h < c.min_h) s.h = c.min_h;
    return s;
}

void Image::paint(Canvas& c, const Theme& t) {
    (void)t;
    c.draw_image({rect_.x, rect_.y}, img_);
}

// ---------------------------------------------------------------------------
// Container
// ---------------------------------------------------------------------------
Size Container::measure(const Constraints& c) {
    Size max_size{0, 0};
    for (auto& child : children_) {
        Size s = child->measure(c.loosen());
        max_size.w = std::max(max_size.w, s.w);
        max_size.h = std::max(max_size.h, s.h);
    }
    return max_size;
}

void Container::layout(Rect r) {
    rect_ = r;
    for (auto& child : children_) {
        child->layout(r);
    }
}

void Container::paint(Canvas& c, const Theme& t) {
    (void)t;
    for (auto& child : children_) {
        child->paint(c, t);
    }
}

bool Container::dispatch_event(const Event& e) {
    // Reverse order so children in front (added later) get priority
    for (int i = int(children_.size()) - 1; i >= 0; --i) {
        if (children_[i]->dispatch_event(e)) return true;
    }
    return on_event(e);
}

Widget* Container::hit(Vec2 p) {
    // Check children in reverse order for proper z-ordering
    for (int i = int(children_.size()) - 1; i >= 0; --i) {
        Widget* hit = children_[i]->hit(p);
        if (hit) return hit;
    }
    return rect_.contains(p) ? this : nullptr;
}

// ---------------------------------------------------------------------------
// ScrollView
// ---------------------------------------------------------------------------
Size ScrollView::measure(const Constraints& c) {
    Size s{std::min(c.max_w, 400.f), std::min(c.max_h, 300.f)};
    if (s.w < c.min_w) s.w = c.min_w;
    if (s.h < c.min_h) s.h = c.min_h;
    return s;
}

void ScrollView::layout(Rect r) {
    rect_ = r;

    // Measure content with unconstrained height so each item gets full space.
    float content_w = r.w - 12; // reserve space for scrollbar
    Constraints content_c{0, content_w, 0, 1e9f};
    Size content_size = content_->measure(content_c);
    content_h_ = content_size.h;

    // Lay out at full natural height starting from the viewport top. During
    // paint we shift by -scroll_y_ via push_translate, so items appear to
    // scroll inside the clip rect.
    Rect content_rect{r.x + 6, r.y, content_w, content_h_};
    content_->layout(content_rect);

    // Clamp scroll offset now that we know the content size.
    float max_scroll = std::max(0.f, content_h_ - r.h);
    if (scroll_y_ > max_scroll) scroll_y_ = max_scroll;
}

void ScrollView::paint(Canvas& c, const Theme& t) {
    // Background
    c.fill_rounded_rect(rect_, t.radius * 0.7f, t.surface);
    c.stroke_rounded_rect(rect_, t.radius * 0.7f, 1.f, t.border);

    // Clip children to viewport
    Rect clip{rect_.x + 1, rect_.y + 1, rect_.w - 14, rect_.h - 2};
    c.push_clip(clip);

    // Translate for scroll offset
    c.push_translate({0, -scroll_y_});

    // Paint children at their layout position (already adjusted by layout)
    for (size_t i = 0; i < content_->child_count(); ++i) {
        Widget* child = content_->child(i);
        child->paint(c, t);
    }

    c.pop_translate();
    c.pop_clip();

    // Scrollbar
    if (content_h_ > rect_.h - 12) {
        float scrollbar_w = 6.f;
        float scrollbar_x = rect_.x + rect_.w - scrollbar_w - 2;
        float scroll_ratio = (rect_.h - 12) / content_h_;
        float thumb_h = std::max(20.f, (rect_.h - 12) * scroll_ratio);
        float thumb_y = rect_.y + 6 + (rect_.h - 12 - thumb_h) * scroll_y_ / std::max(1.f, content_h_ - (rect_.h - 12));

        c.fill_rounded_rect({scrollbar_x, thumb_y, scrollbar_w, thumb_h}, 3.f, t.border);
    }
}

bool ScrollView::dispatch_event(const Event& e) {
    using T = Event::Type;
    const bool is_mouse =
        e.type == T::MouseDown || e.type == T::MouseUp || e.type == T::MouseMove;

    if (e.type == T::MouseWheel) {
        return rect_.contains({e.x, e.y}) ? on_event(e) : false;
    }

    if (is_mouse) {
        // Scrollbar (and outside-of-viewport clicks) are handled locally.
        float scrollbar_x = rect_.x + rect_.w - 10;
        if (!rect_.contains({e.x, e.y})) {
            // Still forward MouseMove so children can clear hover state.
            if (e.type == T::MouseMove) {
                Event adj = e; adj.y += scroll_y_;
                content_->dispatch_event(adj);
            }
            return false;
        }
        if (e.x >= scrollbar_x) return on_event(e);
        Event adj = e;
        adj.y += scroll_y_;
        if (content_->dispatch_event(adj)) return true;
        return on_event(e);
    }

    // Keyboard / other: forward to content regardless of pointer position.
    return content_->dispatch_event(e);
}

bool ScrollView::on_event(const Event& e) {
    using T = Event::Type;
    switch (e.type) {
    case T::MouseWheel: {
        scroll_y_ -= e.wheel_dy * 30.f;
        float max_scroll = std::max(0.f, content_h_ - (rect_.h - 12));
        scroll_y_ = std::max(0.f, std::min(max_scroll, scroll_y_));
        invalidate();
        return true;
    }
    case T::MouseDown: {
        // Check scrollbar click
        float scrollbar_w = 6.f;
        float scrollbar_x = rect_.x + rect_.w - scrollbar_w - 2;
        float max_scroll = std::max(0.f, content_h_ - (rect_.h - 12));
        float thumb_h = std::max(20.f, (rect_.h - 12) * std::min(1.f, (rect_.h - 12) / std::max(1.f, content_h_)));
        float thumb_y = rect_.y + 6 + (rect_.h - 12 - thumb_h) * scroll_y_ / std::max(1.f, content_h_ - (rect_.h - 12));
        if (e.x >= scrollbar_x) {
            // Toggle page scroll
            if (e.y >= rect_.y + 6 && e.y <= rect_.y + rect_.h - 6) {
                // Check if clicking thumb
                if (e.y >= thumb_y && e.y <= thumb_y + thumb_h) {
                    // Start dragging (simplified - just page up/down on click)
                    if (e.y < thumb_y) {
                        scroll_y_ -= 50;
                    } else {
                        scroll_y_ += 50;
                    }
                    scroll_y_ = std::max(0.f, std::min(max_scroll, scroll_y_));
                    invalidate();
                    return true;
                }
                // Page up/down
                if (e.y < thumb_y) {
                    scroll_y_ -= std::max(1.f, (rect_.h - 12) * 0.8f);
                } else {
                    scroll_y_ += std::max(1.f, (rect_.h - 12) * 0.8f);
                }
                scroll_y_ = std::max(0.f, std::min(max_scroll, scroll_y_));
                invalidate();
                return true;
            }
        }
        // Check content area — adjust mouse Y for scroll offset
        if (e.x > rect_.x + 6) {
            Event adjusted = e;
            adjusted.y += scroll_y_;
            return content_->on_event(adjusted);
        }
        break;
    }
    default: break;
    }
    return false;
}

Widget* ScrollView::hit(Vec2 p) {
    if (!rect_.contains(p)) return nullptr;
    // Check scrollbar first
    float scrollbar_w = 6.f;
    if (p.x >= rect_.x + rect_.w - scrollbar_w - 2) {
        return this;
    }
    // Check content area — adjust for scroll offset
    if (p.x > rect_.x + 6) {
        Vec2 adjusted = {p.x, p.y + scroll_y_};
        Widget* hit = content_->hit(adjusted);
        if (hit) return hit;
    }
    return this;
}

// ---------------------------------------------------------------------------
// ComboBox
// ---------------------------------------------------------------------------
Size ComboBox::measure(const Constraints& c) {
    Size s{std::min(180.f, c.max_w), std::min(34.f, c.max_h)};
    if (s.w < c.min_w) s.w = c.min_w;
    if (s.h < c.min_h) s.h = c.min_h;
    return s;
}

void ComboBox::paint(Canvas& c, const Theme& t) {
    // Header only — dropdown renders in paint_overlay so it floats above
    // later-painted sibling widgets.
    c.fill_rounded_rect(rect_, t.radius * 0.7f, t.surface);
    Color border = (open_ || hover_ || focused_) ? t.primary : t.border;
    c.stroke_rounded_rect(rect_, t.radius * 0.7f, focused_ ? 2.f : 1.f, border);

    const Font& f = pick_font(t, false);
    if (f.valid()) {
        float baseline = rect_.y + (rect_.h + f.ascent() - f.descent()) * 0.5f;
        c.draw_text({rect_.x + 10.f, baseline},
                    selected_value_.empty() ? "Select..." : selected_value_,
                    f, t.text);
    }

    float cx = rect_.x + rect_.w - 20;
    float cy = rect_.y + rect_.h * 0.5f;
    float cs = 5.f;
    Path chevron;
    chevron.move_to({cx - cs, cy - cs * 0.4f});
    chevron.line_to({cx, cy + cs * 0.4f});
    chevron.line_to({cx + cs, cy - cs * 0.4f});
    c.fill_path(chevron, t.text_dim, FillRule::NonZero);
}

void ComboBox::paint_overlay(Canvas& c, const Theme& t) {
    if (!open_ || items_.empty()) return;

    Rect drop = drop_rect_();
    c.fill_rounded_rect(drop, t.radius * 0.7f, t.surface);
    c.stroke_rounded_rect(drop, t.radius * 0.7f, 1.f, t.border);

    const Font& f = pick_font(t, false);
    float item_h = 28.f;
    int max_items = int(drop.h / item_h);
    int n = std::min(int(items_.size()), max_items);
    for (int i = 0; i < n; ++i) {
        float iy = drop.y + item_h * i;
        if (i == selected_idx_) {
            c.fill_rounded_rect({drop.x, iy, drop.w, item_h}, 4.f, t.primary);
        } else if (i == hovered_item_) {
            c.fill_rounded_rect({drop.x, iy, drop.w, item_h}, 4.f, t.surface_hi);
        }
        if (f.valid()) {
            float baseline = iy + (item_h + f.ascent() - f.descent()) * 0.5f;
            Color fg = (i == selected_idx_) ? t.bg : t.text;
            c.draw_text({drop.x + 10.f, baseline}, items_[i].label, f, fg);
        }
    }
}

bool ComboBox::on_event(const Event& e) {
    using T = Event::Type;
    bool was_hover = hover_;
    bool was_open  = open_;
    int  was_hovered_item = hovered_item_;

    switch (e.type) {
    case T::MouseMove: {
        hover_ = rect_.contains({e.x, e.y});
        if (open_ && !items_.empty()) {
            Rect drop = drop_rect_();
            if (drop.contains({e.x, e.y})) {
                int idx = int((e.y - drop.y) / 28.f);
                hovered_item_ = (idx >= 0 && idx < int(items_.size())) ? idx : -1;
            } else {
                hovered_item_ = -1;
            }
        }
        break;
    }
    case T::MouseDown: {
        bool in_header = rect_.contains({e.x, e.y});
        if (open_) {
            Rect drop = drop_rect_();
            if (drop.contains({e.x, e.y})) {
                int item = int((e.y - drop.y) / 28.f);
                if (item >= 0 && item < int(items_.size())) {
                    selected_idx_   = item;
                    selected_value_ = items_[item].label;
                    close_();
                    invalidate_all_();
                    if (on_select_) on_select_(item, items_[item].label);
                    return true;
                }
            }
            // Click outside both drop and header → close, and consume the
            // click so we don't accidentally activate whatever is underneath.
            close_();
            invalidate_all_();
            return true;
        }
        if (in_header) {
            open_ = true;
            set_focused(true);
            invalidate_all_();
            return true;
        }
        break;
    }
    case T::KeyDown: {
        if (!focused_) break;
        if (!open_) {
            if (e.key == Key::Enter || e.key == Key::Space ||
                e.key == Key::Down  || e.key == Key::Up) {
                open_ = true;
                if (hovered_item_ < 0) {
                    hovered_item_ = selected_idx_ >= 0 ? selected_idx_ : 0;
                }
                invalidate_all_();
                return true;
            }
        } else {
            if (items_.empty()) break;
            int n = int(items_.size());
            if (e.key == Key::Down) {
                hovered_item_ = (hovered_item_ < 0) ? 0
                              : (hovered_item_ + 1) % n;
                invalidate_all_();
                return true;
            }
            if (e.key == Key::Up) {
                hovered_item_ = (hovered_item_ <= 0) ? n - 1
                              : hovered_item_ - 1;
                invalidate_all_();
                return true;
            }
            if (e.key == Key::Escape) {
                close_();
                invalidate_all_();
                return true;
            }
            if (e.key == Key::Enter || e.key == Key::Space) {
                int item = hovered_item_;
                if (item >= 0 && item < n) {
                    selected_idx_   = item;
                    selected_value_ = items_[item].label;
                    if (on_select_) on_select_(item, items_[item].label);
                }
                close_();
                invalidate_all_();
                return true;
            }
        }
        break;
    }
    default: break;
    }
    if (hover_ != was_hover || open_ != was_open ||
        hovered_item_ != was_hovered_item) {
        invalidate_all_();
    }
    return false;
}

// ---------------------------------------------------------------------------
// Divider
// ---------------------------------------------------------------------------
Size Divider::measure(const Constraints& c) {
    if (axis_ == Axis::Horizontal) {
        return {std::max(c.min_w, c.max_w == 1e9f ? 0.f : c.max_w),
                std::max(c.min_h, thickness_)};
    }
    return {std::max(c.min_w, thickness_),
            std::max(c.min_h, c.max_h == 1e9f ? 0.f : c.max_h)};
}

void Divider::paint(Canvas& c, const Theme& t) {
    Rect r = rect_;
    if (axis_ == Axis::Horizontal) {
        r.x += inset_; r.w -= inset_ * 2;
        r.y += (r.h - thickness_) * 0.5f;
        r.h = thickness_;
    } else {
        r.y += inset_; r.h -= inset_ * 2;
        r.x += (r.w - thickness_) * 0.5f;
        r.w = thickness_;
    }
    if (r.w > 0 && r.h > 0) c.fill_rect(r, t.border);
}

// ---------------------------------------------------------------------------
// RadioGroup
// ---------------------------------------------------------------------------
RadioGroup::RadioGroup(std::vector<std::string> labels) {
    items_.reserve(labels.size());
    for (auto& s : labels) items_.push_back({std::move(s), {}, false});
}

RadioGroup& RadioGroup::add(std::string label) {
    items_.push_back({std::move(label), {}, false});
    return *this;
}

RadioGroup& RadioGroup::set_value(int v) {
    if (v >= -1 && v < int(items_.size()) && v != value_) {
        value_ = v;
        if (on_change_) on_change_(v);
        invalidate();
    }
    return *this;
}

Size RadioGroup::measure(const Constraints& c) {
    float h = items_.empty() ? 0.f :
              float(items_.size()) * row_h_ + float(items_.size() - 1) * gap_;
    float max_w = 0.f;
    for (auto& it : items_) {
        float w = 22.f + row_h_ * float(it.label.size()) * 0.45f + 8.f;
        if (w > max_w) max_w = w;
    }
    Size s{std::min(max_w, c.max_w), std::min(h, c.max_h)};
    if (s.w < c.min_w) s.w = c.min_w;
    if (s.h < c.min_h) s.h = c.min_h;
    return s;
}

void RadioGroup::layout(Rect r) {
    rect_ = r;
    for (size_t i = 0; i < items_.size(); ++i) {
        items_[i].rect = {r.x, r.y + float(i) * (row_h_ + gap_), r.w, row_h_};
    }
}

void RadioGroup::paint(Canvas& c, const Theme& t) {
    const Font& f = pick_font(t, false);
    for (size_t i = 0; i < items_.size(); ++i) {
        const Item& it = items_[i];
        float cx = it.rect.x + 9.f;
        float cy = it.rect.y + it.rect.h * 0.5f;
        bool  sel = int(i) == value_;

        // Ring: outer filled circle in ring color, inner "hole" filled with
        // the ambient surface color. (Canvas has no stroke_circle primitive.)
        Color ring = sel ? t.primary : (it.hover ? t.primary : t.border);
        c.fill_circle({cx, cy}, 8.f, ring);
        c.fill_circle({cx, cy}, 6.7f, t.bg);
        if (sel) c.fill_circle({cx, cy}, 4.f, t.primary);

        if (f.valid()) {
            float baseline = it.rect.y + (it.rect.h + f.ascent() - f.descent()) * 0.5f;
            c.draw_text({it.rect.x + 24.f, baseline}, it.label, f, t.text);
        }
    }
}

bool RadioGroup::on_event(const Event& e) {
    using T = Event::Type;
    bool changed = false;
    switch (e.type) {
    case T::MouseMove: {
        for (auto& it : items_) {
            bool h = it.rect.contains({e.x, e.y});
            if (h != it.hover) { it.hover = h; changed = true; }
        }
        break;
    }
    case T::MouseDown: {
        for (size_t i = 0; i < items_.size(); ++i) {
            if (items_[i].rect.contains({e.x, e.y})) {
                if (int(i) != value_) {
                    value_ = int(i);
                    if (on_change_) on_change_(value_);
                }
                invalidate();
                return true;
            }
        }
        break;
    }
    default: break;
    }
    if (changed) invalidate();
    return false;
}

// ---------------------------------------------------------------------------
// Panel
// ---------------------------------------------------------------------------
static float panel_title_height_(const Theme& t, const std::string& title) {
    if (title.empty()) return 0.f;
    const Font& f = pick_font(t, true);
    return f.valid() ? (f.ascent() - f.descent() + 8.f) : 22.f;
}

Size Panel::measure(const Constraints& c) {
    float inner_w = c.max_w - padding_ * 2;
    float inner_h = c.max_h - padding_ * 2;
    Constraints cc{0, std::max(0.f, inner_w), 0, std::max(0.f, inner_h)};

    Size cs{0, 0};
    if (child_) cs = child_->measure(cc);

    float title_h = title_.empty() ? 0.f : 22.f;
    Size s{cs.w + padding_ * 2, cs.h + padding_ * 2 + title_h};
    s.w = std::min(s.w, c.max_w);
    s.h = std::min(s.h, c.max_h);
    if (s.w < c.min_w) s.w = c.min_w;
    if (s.h < c.min_h) s.h = c.min_h;
    return s;
}

void Panel::layout(Rect r) {
    rect_ = r;
    if (!child_) return;
    float title_h = title_.empty() ? 0.f : 22.f;
    Rect inner{
        r.x + padding_,
        r.y + padding_ + title_h,
        r.w - padding_ * 2,
        r.h - padding_ * 2 - title_h
    };
    child_->layout(inner);
}

void Panel::paint(Canvas& c, const Theme& t) {
    c.fill_rounded_rect(rect_, t.radius, t.surface);
    c.stroke_rounded_rect(rect_, t.radius, 1.f, t.border);

    if (!title_.empty()) {
        const Font& f = pick_font(t, true);
        float title_h = panel_title_height_(t, title_);
        if (f.valid()) {
            float baseline = rect_.y + padding_ * 0.5f + f.ascent();
            c.draw_text({rect_.x + padding_, baseline}, title_, f, t.text);
        }
        (void)title_h;
    }

    if (child_) child_->paint(c, t);
}

bool Panel::dispatch_event(const Event& e) {
    if (child_ && child_->dispatch_event(e)) return true;
    return on_event(e);
}

Widget* Panel::hit(Vec2 p) {
    if (!rect_.contains(p)) return nullptr;
    if (child_) if (Widget* h = child_->hit(p)) return h;
    return this;
}

// ---------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------
Tabs& Tabs::tab(std::string label, std::unique_ptr<Widget> content) {
    if (content) content->set_parent(this);
    tabs_.push_back({std::move(label), std::move(content), {}, false});
    return *this;
}

Tabs& Tabs::select(int i) {
    if (i >= 0 && i < int(tabs_.size()) && i != selected_) {
        selected_ = i;
        if (on_change_) on_change_(i);
        invalidate();
    }
    return *this;
}

Size Tabs::measure(const Constraints& c) {
    float inner_h = c.max_h - header_h_;
    Constraints cc{0, c.max_w, 0, std::max(0.f, inner_h)};
    Size cs{0, 0};
    if (selected_ >= 0 && selected_ < int(tabs_.size()) && tabs_[selected_].content) {
        cs = tabs_[selected_].content->measure(cc);
    }
    Size s{std::max(cs.w, c.min_w), cs.h + header_h_};
    s.w = std::min(s.w, c.max_w);
    s.h = std::min(s.h, c.max_h);
    if (s.w < c.min_w) s.w = c.min_w;
    if (s.h < c.min_h) s.h = c.min_h;
    return s;
}

void Tabs::layout(Rect r) {
    rect_ = r;
    // Header strip: evenly-sized tab rects.
    if (!tabs_.empty()) {
        float tw = r.w / float(tabs_.size());
        for (size_t i = 0; i < tabs_.size(); ++i) {
            tabs_[i].header = {r.x + float(i) * tw, r.y, tw, header_h_};
        }
    }
    // Content fills below the header.
    Rect content{r.x, r.y + header_h_, r.w, r.h - header_h_};
    if (selected_ >= 0 && selected_ < int(tabs_.size()) && tabs_[selected_].content) {
        tabs_[selected_].content->layout(content);
    }
}

void Tabs::paint(Canvas& c, const Theme& t) {
    const Font& f = pick_font(t, false);

    // Header background
    Rect hdr{rect_.x, rect_.y, rect_.w, header_h_};
    c.fill_rect(hdr, t.surface);
    // Baseline
    c.fill_rect({rect_.x, rect_.y + header_h_ - 1, rect_.w, 1.f}, t.border);

    for (size_t i = 0; i < tabs_.size(); ++i) {
        const Tab& tab = tabs_[i];
        bool sel = int(i) == selected_;
        if (tab.hover && !sel) {
            c.fill_rect(tab.header, t.surface_hi);
        }
        if (f.valid()) {
            float tw = f.measure(tab.label);
            float bx = tab.header.x + (tab.header.w - tw) * 0.5f;
            float baseline = tab.header.y +
                             (tab.header.h + f.ascent() - f.descent()) * 0.5f;
            Color fg = sel ? t.text : t.text_dim;
            c.draw_text({bx, baseline}, tab.label, f, fg);
        }
        if (sel) {
            // Underline
            c.fill_rect({tab.header.x + 12, tab.header.y + tab.header.h - 2.f,
                         tab.header.w - 24, 2.f}, t.primary);
        }
    }

    if (selected_ >= 0 && selected_ < int(tabs_.size()) && tabs_[selected_].content) {
        tabs_[selected_].content->paint(c, t);
    }
}

void Tabs::paint_overlay(Canvas& c, const Theme& t) {
    if (selected_ >= 0 && selected_ < int(tabs_.size()) && tabs_[selected_].content) {
        tabs_[selected_].content->paint_overlay(c, t);
    }
}

bool Tabs::dispatch_event(const Event& e) {
    using T = Event::Type;
    // Header interactions stay local.
    if (e.type == T::MouseMove || e.type == T::MouseDown || e.type == T::MouseUp) {
        Rect hdr{rect_.x, rect_.y, rect_.w, header_h_};
        if (hdr.contains({e.x, e.y})) {
            return on_event(e);
        }
    }
    // Forward to the active tab's content.
    if (selected_ >= 0 && selected_ < int(tabs_.size()) && tabs_[selected_].content) {
        if (tabs_[selected_].content->dispatch_event(e)) return true;
    }
    return on_event(e);
}

bool Tabs::on_event(const Event& e) {
    using T = Event::Type;
    bool changed = false;
    switch (e.type) {
    case T::MouseMove: {
        for (auto& tab : tabs_) {
            bool h = tab.header.contains({e.x, e.y});
            if (h != tab.hover) { tab.hover = h; changed = true; }
        }
        break;
    }
    case T::MouseDown: {
        for (size_t i = 0; i < tabs_.size(); ++i) {
            if (tabs_[i].header.contains({e.x, e.y})) {
                select(int(i));
                return true;
            }
        }
        break;
    }
    default: break;
    }
    if (changed) invalidate();
    return false;
}

Widget* Tabs::hit(Vec2 p) {
    if (!rect_.contains(p)) return nullptr;
    if (selected_ >= 0 && selected_ < int(tabs_.size()) && tabs_[selected_].content) {
        if (Widget* h = tabs_[selected_].content->hit(p)) return h;
    }
    return this;
}

} // namespace stilus
