// src/widget.cpp — base widget logic (invalidate, Flex layout)
#include "stilus/widget.hpp"
#include "platform/platform.hpp"

namespace stilus {

// Bridge to the window's redraw request without pulling in the full window
// type. WindowImpl is opaque to widgets.
void Widget::invalidate() { invalidate(rect_); }

void Widget::invalidate(Rect r) {
    if (root_notify_) {
        auto* w = static_cast<detail::WindowImpl*>(root_notify_);
        if (r.w > 0 && r.h > 0) w->add_damage(r);
        w->request_redraw();
    } else if (parent_) {
        parent_->invalidate(r);
    }
}

// ---------------------------------------------------------------------------
// Flex
// ---------------------------------------------------------------------------
Size Flex::measure(const Constraints& c) {
    const bool horz = (axis_ == Axis::Horizontal);
    float max_main  = (horz ? c.max_w : c.max_h) - pad_ * 2;
    float max_cross = (horz ? c.max_h : c.max_w) - pad_ * 2;

    float used_main = 0;
    float max_child_cross = 0;
    float total_flex = 0;
    size_t n = children_.size();

    std::vector<Size> sizes(n);

    // First pass: measure flex=0 / fixed children.
    for (size_t i = 0; i < n; ++i) {
        auto& ch = children_[i];
        if (ch.flex > 0) { total_flex += ch.flex; continue; }
        Constraints cc{0, max_main, 0, max_cross};
        if (ch.fixed >= 0) {
            if (horz) cc = cc.tighten_w(ch.fixed);
            else      cc = cc.tighten_h(ch.fixed);
        }
        sizes[i] = ch.w->measure(cc);
        used_main += horz ? sizes[i].w : sizes[i].h;
        float cc2 = horz ? sizes[i].h : sizes[i].w;
        if (cc2 > max_child_cross) max_child_cross = cc2;
    }

    // Add gaps
    if (n > 1) used_main += gap_ * (n - 1);

    // Second pass: allocate flex children with remaining space.
    float remaining = (max_main - used_main);
    if (remaining < 0) remaining = 0;
    for (size_t i = 0; i < n; ++i) {
        auto& ch = children_[i];
        if (ch.flex <= 0) continue;
        float main = remaining * (ch.flex / total_flex);
        Constraints cc{0, max_main, 0, max_cross};
        if (horz) cc = cc.tighten_w(main);
        else      cc = cc.tighten_h(main);
        sizes[i] = ch.w->measure(cc);
        float cc2 = horz ? sizes[i].h : sizes[i].w;
        if (cc2 > max_child_cross) max_child_cross = cc2;
    }

    // Sum for actual used main (in case flex children didn't take all)
    float actual_main = 0;
    for (auto& s : sizes) actual_main += horz ? s.w : s.h;
    if (n > 1) actual_main += gap_ * (n - 1);

    Size s;
    if (horz) {
        s.w = std::min(actual_main + pad_ * 2, c.max_w);
        s.h = std::min(max_child_cross + pad_ * 2, c.max_h);
    } else {
        s.w = std::min(max_child_cross + pad_ * 2, c.max_w);
        s.h = std::min(actual_main + pad_ * 2, c.max_h);
    }
    if (s.w < c.min_w) s.w = c.min_w;
    if (s.h < c.min_h) s.h = c.min_h;
    return s;
}

void Flex::layout(Rect r) {
    rect_ = r;
    if (children_.empty()) return;
    const bool horz = (axis_ == Axis::Horizontal);
    float inner_main  = (horz ? r.w : r.h) - pad_ * 2;
    float inner_cross = (horz ? r.h : r.w) - pad_ * 2;
    size_t n = children_.size();

    // Re-measure using final constraints so flex distribution is exact.
    std::vector<Size> sizes(n);
    float used = 0;
    float total_flex = 0;
    for (size_t i = 0; i < n; ++i) {
        auto& ch = children_[i];
        if (ch.flex > 0) { total_flex += ch.flex; continue; }
        Constraints cc{0, inner_main, 0, inner_cross};
        if (ch.fixed >= 0) {
            if (horz) cc = cc.tighten_w(ch.fixed);
            else      cc = cc.tighten_h(ch.fixed);
        }
        sizes[i] = ch.w->measure(cc);
        used += horz ? sizes[i].w : sizes[i].h;
    }
    if (n > 1) used += gap_ * (n - 1);
    float remaining = inner_main - used;
    if (remaining < 0) remaining = 0;
    for (size_t i = 0; i < n; ++i) {
        auto& ch = children_[i];
        if (ch.flex <= 0) continue;
        float main = remaining * (ch.flex / total_flex);
        Constraints cc{0, inner_main, 0, inner_cross};
        if (horz) cc = cc.tighten_w(main);
        else      cc = cc.tighten_h(main);
        sizes[i] = ch.w->measure(cc);
    }

    // Place children.
    float x0 = r.x + pad_, y0 = r.y + pad_;
    float cursor = horz ? x0 : y0;
    for (size_t i = 0; i < n; ++i) {
        float main_sz  = horz ? sizes[i].w : sizes[i].h;
        float cross_sz = horz ? sizes[i].h : sizes[i].w;
        float cross_pos = (horz ? y0 : x0);
        float cross_len = cross_sz;
        switch (cross_) {
            case CrossAlign::Start:                                                    break;
            case CrossAlign::Center:  cross_pos += (inner_cross - cross_sz) * 0.5f;   break;
            case CrossAlign::End:     cross_pos += (inner_cross - cross_sz);           break;
            case CrossAlign::Stretch: cross_len  = inner_cross;                        break;
        }
        Rect cr;
        if (horz) { cr = {cursor, cross_pos, main_sz, cross_len}; }
        else      { cr = {cross_pos, cursor, cross_len, main_sz}; }
        children_[i].w->layout(cr);
        cursor += main_sz + gap_;
    }
}

void Flex::paint(Canvas& c, const Theme& t) {
    for (auto& ch : children_) ch.w->paint(c, t);
}

bool Flex::on_event(const Event& e) { (void)e; return false; }

bool Flex::dispatch_event(const Event& e) {
    using T = Event::Type;

    // Any child with an open overlay (e.g. ComboBox dropdown) captures
    // all mouse events first, regardless of whether the pointer is inside
    // that child's layout rect.
    const bool is_mouse =
        e.type == T::MouseMove || e.type == T::MouseDown ||
        e.type == T::MouseUp   || e.type == T::MouseWheel;
    if (is_mouse) {
        for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
            if (it->w->wants_capture()) {
                if (it->w->dispatch_event(e)) return true;
            }
        }
    }

    // MouseMove and MouseUp must be broadcast to every child so they can
    // clear stale hover / active state when the pointer leaves their rect.
    // (Topmost routing would only ever notify the widget currently under the
    // pointer, so a button stays lit forever after the cursor leaves.)
    if (e.type == T::MouseMove || e.type == T::MouseUp) {
        bool consumed = false;
        for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
            if (it->w->wants_capture()) continue;
            if (it->w->dispatch_event(e)) consumed = true;
        }
        return consumed || on_event(e);
    }

    // MouseDown / MouseWheel: route to the topmost child whose rect contains p.
    if (e.type == T::MouseDown || e.type == T::MouseWheel) {
        Vec2 p{e.x, e.y};
        for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
            if (!it->w->wants_capture() && it->w->rect().contains(p)) {
                if (it->w->dispatch_event(e)) return true;
            }
        }
        return on_event(e);
    }

    // Keyboard / other: broadcast until consumed.
    for (auto& ch : children_) {
        if (ch.w->dispatch_event(e)) return true;
    }
    return on_event(e);
}

Widget* Flex::hit(Vec2 p) {
    if (!rect_.contains(p)) return nullptr;
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if (Widget* w = it->w->hit(p)) return w;
    }
    return this;
}

} // namespace stilus
