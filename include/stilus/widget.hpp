// stilus/widget.hpp — widget base + layout primitives
#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "canvas.hpp"
#include "event.hpp"
#include "geom.hpp"
#include "theme.hpp"

namespace stilus {

// Size constraints passed top-down during measurement.
struct Constraints {
    float min_w = 0, max_w = 1e9f;
    float min_h = 0, max_h = 1e9f;
    Constraints tighten_w(float w) const { return {w, w, min_h, max_h}; }
    Constraints tighten_h(float h) const { return {min_w, max_w, h, h};  }
    Constraints loosen()          const { return {0,0,1e9f,1e9f}; }
};

struct Size { float w = 0, h = 0; };

// Base widget. Widgets own children via unique_ptr.
class Widget {
public:
    virtual ~Widget() = default;

    // Measurement: given max w/h, return preferred size.
    virtual Size measure(const Constraints& c) = 0;

    // Layout: the assigned rect (absolute in the window coord system).
    // Default: just store and recurse via layout_ on children (subclasses
    // are free to override for custom layout).
    virtual void layout(Rect r) { rect_ = r; }

    // Paint.
    virtual void paint(Canvas& c, const Theme& t) = 0;

    // Second paint pass for overlays (dropdowns, tooltips, menus). Runs after
    // the whole tree's paint() so overlays draw above later-painted siblings.
    // Default: recurse into children.
    virtual void paint_overlay(Canvas& c, const Theme& t) {
        for (size_t i = 0; i < child_count(); ++i) {
            if (Widget* w = child(i)) w->paint_overlay(c, t);
        }
    }

    // Event handling. Return true to consume.
    virtual bool on_event(const Event& e) { (void)e; return false; }

    // Request a repaint (propagates up; root dispatches to window).
    // The rect-taking overload reports the widget's damage area so the
    // window backend can do a partial redraw. The no-arg form uses this
    // widget's current layout rect.
    void invalidate();
    void invalidate(Rect r);

    // Accessors
    Rect   rect() const { return rect_; }
    Widget* parent() const { return parent_; }

    // Children helpers. Default widget has no children; containers override.
    virtual size_t  child_count() const { return 0; }
    virtual Widget* child(size_t)       { return nullptr; }

    // Routing helper: walk children to dispatch. Containers override.
    virtual bool dispatch_event(const Event& e) { return on_event(e); }

    // Return true if this widget should receive all mouse events regardless
    // of whether the pointer is inside rect_ (e.g. an open dropdown overlay).
    virtual bool wants_capture() const { return false; }

    // Focus protocol. `focusable()` marks widgets that participate in Tab
    // cycling (TextInput, Button, CheckBox, …). Containers leave the default
    // false but still forward focus events to children. `set_focused(true)`
    // on a non-focusable widget is a no-op.
    virtual bool focusable() const { return false; }
    bool focused() const { return focused_; }
    virtual void set_focused(bool v) {
        if (!focusable()) return;
        if (focused_ != v) { focused_ = v; invalidate(); }
    }

    // Tab/focus traversal: gather all focusable descendants in tree order.
    // The caller cycles through the resulting vector to change focus.
    virtual void collect_focusable(std::vector<Widget*>& out) {
        if (focusable()) out.push_back(this);
        for (size_t i = 0; i < child_count(); ++i) {
            if (Widget* c = child(i)) c->collect_focusable(out);
        }
    }

    // Hit-testing: return deepest widget whose rect contains the point.
    virtual Widget* hit(Vec2 p) {
        return rect_.contains(p) ? this : nullptr;
    }

    // Root/window linkage
    void set_root_notify(void* wnd_impl) { root_notify_ = wnd_impl; }
    void set_parent(Widget* p) { parent_ = p; }

protected:
    Rect    rect_{};
    Widget* parent_      = nullptr;
    void*   root_notify_ = nullptr; // WindowImpl ptr (opaque); used by invalidate()
    bool    focused_     = false;
};

// ---- Containers -----------------------------------------------------------
enum class Axis : uint8_t { Horizontal, Vertical };
enum class CrossAlign : uint8_t { Start, Center, End, Stretch };

struct FlexChild {
    std::unique_ptr<Widget> w;
    float flex  = 0; // 0 = fixed to preferred size; >0 = share of remaining
    float fixed = -1; // override fixed size along main axis; -1 = use measure
};

class Flex : public Widget {
public:
    Flex(Axis axis) : axis_(axis) {}

    Flex& add(std::unique_ptr<Widget> w, float flex = 0, float fixed = -1) {
        w->set_parent(this);
        children_.push_back({std::move(w), flex, fixed});
        return *this;
    }

    Flex& gap(float g) { gap_ = g; return *this; }
    Flex& padding(float p) { pad_ = p; return *this; }
    Flex& cross(CrossAlign a) { cross_ = a; return *this; }

    Size measure(const Constraints& c) override;
    void layout(Rect r) override;
    void paint(Canvas& c, const Theme& t) override;
    bool on_event(const Event& e) override;
    bool dispatch_event(const Event& e) override;
    Widget* hit(Vec2 p) override;

    // True if this Flex or ANY descendant currently wants capture (e.g. an
    // open ComboBox nested a few levels down). Without this, an ancestor
    // Flex's mouse routing only ever checks its own direct children for
    // wants_capture(), so a capturing widget more than one level deep is
    // invisible to it — clicks landing in the overlay (which can extend
    // outside every ancestor's own laid-out rect) never reach the widget
    // that opened it. Recursing here makes the existing direct-children
    // check in dispatch_event correct at every nesting depth.
    bool wants_capture() const override {
        for (auto& ch : children_) if (ch.w->wants_capture()) return true;
        return false;
    }

    size_t  child_count() const override { return children_.size(); }
    Widget* child(size_t i) override { return children_[i].w.get(); }

private:
    Axis                   axis_;
    float                  gap_  = 0;
    float                  pad_  = 0;
    CrossAlign             cross_ = CrossAlign::Stretch;
    std::vector<FlexChild> children_;
};

inline std::unique_ptr<Flex> row()    { return std::make_unique<Flex>(Axis::Horizontal); }
inline std::unique_ptr<Flex> column() { return std::make_unique<Flex>(Axis::Vertical);   }

} // namespace stilus
