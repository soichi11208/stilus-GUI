// stilus/widgets.hpp — core reusable widgets
#pragma once
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "widget.hpp"

namespace stilus {

// ----------------------------------------------------------------------------
// Label — non-interactive text.
// ----------------------------------------------------------------------------
class Label : public Widget {
public:
    Label(std::string text) : text_(std::move(text)) {}

    Label& color(Color c)        { color_ = c; has_color_ = true; return *this; }
    Label& font_size(float px)   { pixel_size_ = px; return *this; }
    Label& bold(bool b = true)   { bold_ = b;        return *this; }

    Size measure(const Constraints& c) override;
    void paint(Canvas& c, const Theme& t) override;

private:
    std::string text_;
    Color       color_{};
    bool        has_color_   = false;
    float       pixel_size_  = 0; // 0 = use theme font's size
    bool        bold_        = false;
};

// ----------------------------------------------------------------------------
// Button
// ----------------------------------------------------------------------------
class Button : public Widget {
public:
    Button(std::string label) : label_(std::move(label)) {}

    Button& on_click(std::function<void()> f) { on_click_ = std::move(f); return *this; }
    Button& primary(bool p = true) { primary_ = p; return *this; }

    Size measure(const Constraints& c) override;
    void paint(Canvas& c, const Theme& t) override;
    bool on_event(const Event& e) override;
    bool focusable() const override { return true; }

private:
    std::string           label_;
    std::function<void()> on_click_;
    bool                  primary_ = false;
    bool                  hover_   = false;
    bool                  active_  = false;
};

// ----------------------------------------------------------------------------
// Slider (horizontal)
// ----------------------------------------------------------------------------
class Slider : public Widget {
public:
    Slider(float min, float max, float initial)
        : min_(min), max_(max), value_(initial) {}

    Slider& on_change(std::function<void(float)> f) {
        on_change_ = std::move(f); return *this;
    }
    float value() const { return value_; }
    void  set_value(float v);

    Size measure(const Constraints& c) override;
    void paint(Canvas& c, const Theme& t) override;
    bool on_event(const Event& e) override;
    bool focusable() const override { return true; }

private:
    float min_, max_, value_;
    bool  dragging_ = false;
    std::function<void(float)> on_change_;
};

// ----------------------------------------------------------------------------
// TextInput — single-line
// ----------------------------------------------------------------------------
class TextInput : public Widget {
public:
    TextInput() = default;
    TextInput(std::string placeholder) : placeholder_(std::move(placeholder)) {}

    const std::string& text() const { return text_; }
    TextInput& set_text(std::string s) { text_ = std::move(s); return *this; }
    TextInput& on_change(std::function<void(const std::string&)> f) {
        on_change_ = std::move(f); return *this;
    }
    TextInput& on_submit(std::function<void(const std::string&)> f) {
        on_submit_ = std::move(f); return *this;
    }

    Size measure(const Constraints& c) override;
    void paint(Canvas& c, const Theme& t) override;
    bool on_event(const Event& e) override;
    bool focusable() const override { return true; }

private:
    std::string text_;
    std::string placeholder_;
    size_t      cursor_  = 0;    // byte index
    // IME preedit (composing text), drawn under the caret until the IME
    // commits. Empty when no preedit is active.
    std::string preedit_;
    int         preedit_cursor_ = -1;
    // Cached at paint time so on_event (which has no Theme) can map click
    // x-coords to a UTF-8 byte index.
    const Font* paint_font_ = nullptr;
    float       paint_text_x_ = 0;
    std::function<void(const std::string&)> on_change_;
    std::function<void(const std::string&)> on_submit_;
};

// ----------------------------------------------------------------------------
// CheckBox — toggled checkbox with text label.
// ----------------------------------------------------------------------------
class CheckBox : public Widget {
public:
    CheckBox(std::string text, bool checked = false)
        : text_(std::move(text)), checked_(checked) {}

    CheckBox& on_change(std::function<void(bool)> f) {
        on_change_ = std::move(f); return *this;
    }

    bool checked() const { return checked_; }
    CheckBox& checked(bool v) { checked_ = v; invalidate(); return *this; }

    Size measure(const Constraints& c) override;
    void paint(Canvas& c, const Theme& t) override;
    bool on_event(const Event& e) override;
    bool focusable() const override { return true; }

private:
    std::string           text_;
    bool                  checked_ = false;
    bool                  hover_   = false;
    std::function<void(bool)> on_change_;
};

// ----------------------------------------------------------------------------
// ProgressBar — horizontal progress indicator (0.0 — 1.0).
// ----------------------------------------------------------------------------
class ProgressBar : public Widget {
public:
    ProgressBar(float value = 0.f, float min = 0.f, float max = 1.f)
        : min_(min), max_(max), value_(clamped(value)) {}

    ProgressBar& on_change(std::function<void(float)> f) {
        on_change_ = std::move(f); return *this;
    }

    float value() const { return value_; }
    ProgressBar& value(float v) {
        v = clamped(v);
        if (v != value_) { value_ = v; if (on_change_) on_change_(v); invalidate(); }
        return *this;
    }

    Size measure(const Constraints& c) override;
    void paint(Canvas& c, const Theme& t) override;

private:
    static float clamped(float v) {
        if (v < 0.f) v = 0.f;
        if (v > 1.f) v = 1.f;
        return v;
    }
    float min_, max_, value_;
    std::function<void(float)> on_change_;
};

// ----------------------------------------------------------------------------
// Image — displays a PixelImage.
// ----------------------------------------------------------------------------
class Image : public Widget {
public:
    explicit Image(PixelImage img) : img_(std::move(img)) {}

    Size measure(const Constraints& c) override;
    void paint(Canvas& c, const Theme& t) override;

private:
    PixelImage img_;
};

// ----------------------------------------------------------------------------
// Container — simple child container (fills its parent rect).
// ----------------------------------------------------------------------------
class Container : public Widget {
public:
    Container() = default;

    Container& add(std::unique_ptr<Widget> w) {
        w->set_parent(this);
        children_.push_back(std::move(w));
        return *this;
    }

    Size measure(const Constraints& c) override;
    void layout(Rect r) override;
    void paint(Canvas& c, const Theme& t) override;
    bool dispatch_event(const Event& e) override;
    Widget* hit(Vec2 p) override;

    size_t  child_count() const override { return children_.size(); }
    Widget* child(size_t i) override { return children_[i].get(); }

private:
    std::vector<std::unique_ptr<Widget>> children_;
};

// ----------------------------------------------------------------------------
// ScrollView — scrollable container with vertical scrolling.
// ----------------------------------------------------------------------------
class ScrollView : public Widget {
public:
    ScrollView() : content_(new Flex(Axis::Vertical)) {
        content_->set_parent(this);
    }

    ScrollView& add(std::unique_ptr<Widget> w) {
        content_->add(std::move(w));
        return *this;
    }

    Size measure(const Constraints& c) override;
    void layout(Rect r) override;
    void paint(Canvas& c, const Theme& t) override;
    bool dispatch_event(const Event& e) override;
    bool on_event(const Event& e) override;
    Widget* hit(Vec2 p) override;

    size_t  child_count() const override { return content_->child_count(); }
    Widget* child(size_t i) override { return content_->child(i); }

private:
    std::unique_ptr<Flex> content_;
    float scroll_y_ = 0;
    float content_h_ = 0;
};

// ----------------------------------------------------------------------------
// ComboBox — dropdown list with selectable items.
// ----------------------------------------------------------------------------
class ComboBox : public Widget {
public:
    struct Item {
        std::string label;
        int         index = -1;
    };

    explicit ComboBox(std::vector<Item> items)
        : items_(std::move(items)) {}

    ComboBox& on_select(std::function<void(int, const std::string&)> f) {
        on_select_ = std::move(f); return *this;
    }

    const std::string& text() const { return selected_value_; }
    int selected_index() const { return selected_idx_; }

    bool wants_capture() const override { return open_; }
    bool focusable() const override { return true; }

    Size measure(const Constraints& c) override;
    void paint(Canvas& c, const Theme& t) override;
    void paint_overlay(Canvas& c, const Theme& t) override;
    bool on_event(const Event& e) override;

private:
    Rect drop_rect_() const {
        float item_h = 28.f;
        float drop_y = rect_.y + rect_.h + 2;
        float drop_h = std::min(float(items_.size()) * item_h, 200.f);
        return {rect_.x, drop_y, rect_.w, drop_h};
    }
    // Invalidate the union of header + potential dropdown area so open/close
    // transitions don't leave stale pixels outside rect_.
    void invalidate_all_() {
        Rect r = rect_;
        if (!items_.empty()) {
            float item_h = 28.f;
            float drop_h = std::min(float(items_.size()) * item_h, 200.f);
            r.h += 2 + drop_h;
        }
        invalidate(r);
    }
    void close_() {
        if (open_) { open_ = false; hovered_item_ = -1; }
    }

    std::vector<Item>        items_;
    std::string              selected_value_;
    int                      selected_idx_ = -1;
    bool                     open_   = false;
    bool                     hover_  = false;
    int                      hovered_item_ = -1;
    std::function<void(int, const std::string&)> on_select_;
};

// ----------------------------------------------------------------------------
// Spacer — consumes available space along the parent Flex's main axis. Use
// with `flex=1` inside a Row/Column to push siblings apart.
// ----------------------------------------------------------------------------
class Spacer : public Widget {
public:
    Spacer() = default;
    explicit Spacer(float min_size) : min_size_(min_size) {}

    Size measure(const Constraints& c) override {
        return {std::max(min_size_, c.min_w), std::max(min_size_, c.min_h)};
    }
    void paint(Canvas&, const Theme&) override {}

private:
    float min_size_ = 0.f;
};

// ----------------------------------------------------------------------------
// Divider — thin horizontal or vertical separator line.
// ----------------------------------------------------------------------------
class Divider : public Widget {
public:
    Divider(Axis axis = Axis::Horizontal, float thickness = 1.f)
        : axis_(axis), thickness_(thickness) {}

    Divider& inset(float px) { inset_ = px; return *this; }

    Size measure(const Constraints& c) override;
    void paint(Canvas& c, const Theme& t) override;

private:
    Axis  axis_;
    float thickness_;
    float inset_ = 0.f;
};

// ----------------------------------------------------------------------------
// RadioGroup — single-selection group of labelled radio buttons (vertical).
// ----------------------------------------------------------------------------
class RadioGroup : public Widget {
public:
    RadioGroup() = default;
    explicit RadioGroup(std::vector<std::string> labels);

    RadioGroup& add(std::string label);
    RadioGroup& on_change(std::function<void(int)> f) {
        on_change_ = std::move(f); return *this;
    }
    RadioGroup& gap(float g) { gap_ = g; return *this; }

    int  value() const { return value_; }
    RadioGroup& set_value(int v);

    Size measure(const Constraints& c) override;
    void layout(Rect r) override;
    void paint(Canvas& c, const Theme& t) override;
    bool on_event(const Event& e) override;

private:
    struct Item {
        std::string label;
        Rect        rect{};
        bool        hover = false;
    };
    std::vector<Item>          items_;
    int                        value_ = -1;
    float                      gap_   = 6.f;
    float                      row_h_ = 24.f;
    std::function<void(int)>   on_change_;
};

// ----------------------------------------------------------------------------
// Panel — bordered, optionally-titled box that wraps a single child.
// ----------------------------------------------------------------------------
class Panel : public Widget {
public:
    Panel() = default;

    Panel& title(std::string s)               { title_ = std::move(s); return *this; }
    Panel& padding(float p)                   { padding_ = p;          return *this; }
    Panel& child(std::unique_ptr<Widget> w)   {
        if (w) w->set_parent(this);
        child_ = std::move(w);
        return *this;
    }

    Size measure(const Constraints& c) override;
    void layout(Rect r) override;
    void paint(Canvas& c, const Theme& t) override;
    bool dispatch_event(const Event& e) override;
    void paint_overlay(Canvas& c, const Theme& t) override {
        if (child_) child_->paint_overlay(c, t);
    }
    Widget* hit(Vec2 p) override;

    size_t  child_count() const override { return child_ ? 1 : 0; }
    Widget* child(size_t)       override { return child_.get(); }

private:
    std::string             title_;
    std::unique_ptr<Widget> child_;
    float                   padding_ = 12.f;
};

// ----------------------------------------------------------------------------
// Tabs — tabbed section switcher. Header row on top, content below.
// ----------------------------------------------------------------------------
class Tabs : public Widget {
public:
    Tabs() = default;

    Tabs& tab(std::string label, std::unique_ptr<Widget> content);
    Tabs& on_change(std::function<void(int)> f) {
        on_change_ = std::move(f); return *this;
    }

    int   selected() const { return selected_; }
    Tabs& select(int i);

    Size measure(const Constraints& c) override;
    void layout(Rect r) override;
    void paint(Canvas& c, const Theme& t) override;
    void paint_overlay(Canvas& c, const Theme& t) override;
    bool on_event(const Event& e) override;
    bool dispatch_event(const Event& e) override;
    Widget* hit(Vec2 p) override;

    size_t  child_count() const override { return tabs_.size(); }
    Widget* child(size_t i) override { return tabs_[i].content.get(); }

    // Focus cycling should only visit widgets in the visible tab.
    void collect_focusable(std::vector<Widget*>& out) override {
        if (selected_ >= 0 && selected_ < int(tabs_.size())
            && tabs_[selected_].content) {
            tabs_[selected_].content->collect_focusable(out);
        }
    }

private:
    struct Tab {
        std::string             label;
        std::unique_ptr<Widget> content;
        Rect                    header{};
        bool                    hover = false;
    };
    std::vector<Tab>         tabs_;
    int                      selected_ = 0;
    float                    header_h_ = 36.f;
    std::function<void(int)> on_change_;
};

} // namespace stilus
