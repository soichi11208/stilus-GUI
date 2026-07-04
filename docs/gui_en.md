# stilus GUI Guide

Practical documentation for writing GUIs with stilus in C++. Combines API reference and usage gotchas in one document. Everything is available through the single header `stilus/gui.hpp`.

## Table of Contents

1. [Minimal Example](#minimal-example)
2. [Windows and the Event Loop](#windows-and-the-event-loop)
3. [Widget Tree](#widget-tree)
4. [Layout (Flex)](#layout-flex)
5. [Widget Reference](#widget-reference)
6. [Events](#events)
7. [Theme and Fonts](#theme-and-fonts)
8. [Animation](#animation)
9. [Modals, Popups, Dialogs](#modals-popups-dialogs)
10. [Clipboard](#clipboard)
11. [Coordinate System and HiDPI](#coordinate-system-and-hidpi)
12. [Gotchas](#gotchas)

---

## Minimal Example

```cpp
#include "stilus/gui.hpp"

int main() {
    stilus::Window win("hello", 480, 320);
    if (!win.is_open()) return 1;

    win.on_event([&](const stilus::Event& e) {
        if (e.type == stilus::Event::Type::Close) win.close();
    });

    win.on_frame([&](stilus::Canvas& c) {
        c.fill_rounded_rect({40, 40, 200, 80}, 12.f,
                            stilus::Color::rgb(0x4a90e2));
    });

    return stilus::App::instance().run();
}
```

`App::instance().run()` blocks until all `Window`s are closed. Good enough for a single-window app.

To build, install `libstilus.a` (see the "Alternative build: system libc (glibc)" section in the project README) and use `pkg-config --cflags --libs --static stilus`, or just point to a sibling stilus checkout.

---

## Windows and the Event Loop

### `stilus::Window`

```cpp
Window(std::string_view title, int width, int height);
bool is_open() const;
void close();
void request_redraw();
int  width() const;                 // logical pixels (not multiplied by HiDPI)
int  height() const;
int  scale_factor() const;          // current integer scale
Theme& theme();                     // colors/fonts used for drawing
Widget* root();                     // whatever was set_root'd
void set_root(std::unique_ptr<Widget>);
void on_frame(std::function<void(Canvas&)>);
void on_event(std::function<void(const Event&)>);
void request_animation_frame(std::function<void(float dt_sec)>);
```

### `stilus::App`

Singleton. `App::instance().run()` is the main loop. You can have multiple `Window`s in one process, but `App::run` can't be called more than once.

### Two usage modes

1. **Immediate mode**: draw everything yourself each frame in `on_frame(...)`. Good for board games or custom UIs.
2. **Retained mode (widget tree)**: pass a widget tree via `set_root(std::unique_ptr<Widget>)`. Good for forms and business apps.

You can mix both (see [Gotchas](#gotchas) for the behavior when overriding `on_event` after `set_root`).

---

## Widget Tree

`Widget`s form a tree owned by `unique_ptr` from parent to child. Children are added with their ownership transferred via `Flex::add` etc.:

```cpp
auto col = stilus::column();               // == std::make_unique<Flex>(Vertical)
col->padding(16).gap(10).cross(stilus::CrossAlign::Stretch);

auto title = std::make_unique<stilus::Label>("Hello");
title->font_size(22).bold();
col->add(std::move(title), /*flex=*/0);

col->add(std::make_unique<stilus::Divider>(), 0);

auto btn = std::make_unique<stilus::Button>("Click me");
btn->on_click([]{ std::puts("clicked"); });
col->add(std::move(btn), 0);

win.set_root(std::move(col));
```

Builder methods (`font_size(...)`, `bold()`, `on_click(...)` etc.) return `*this` so they can be chained, but when starting from `std::make_unique`, it's cleaner to assign to a `unique_ptr` first and then call methods via `->`.

---

## Layout (Flex)

stilus has only one layout container, **Flex**, which is equivalent to Flexbox. Other containers (Panel, ScrollView, Tabs) use Flex internally or a similar arrangement.

### Construction

```cpp
auto row  = stilus::row();    // horizontal (Axis::Horizontal)
auto col  = stilus::column(); // vertical (Axis::Vertical)
// == std::make_unique<stilus::Flex>(Axis::Horizontal / Vertical)
```

### add parameters

```cpp
flex_ptr->add(std::unique_ptr<Widget> w,
              float flex  = 0,     // 0 = intrinsic size, >0 = distribute remaining space proportionally
              float fixed = -1);   // fixed main-axis size (-1 = use measure())
```

- **`flex=0` + `fixed=-1`**: Uses the widget's preferred size from `measure()`.
- **`flex=1`**: Expands to fill remaining space proportionally. If only one child has `flex=1`, it fills to the right (or bottom).
- **`fixed=80`**: Fixes the main-axis size to 80px. Useful when you want "label column all 80px wide".
- **`flex=1` + `fixed>0`**: `fixed` takes priority and `flex` is ignored.

### Properties

```cpp
flex->gap(8);                           // spacing between children
flex->padding(12);                      // inner padding of the container
flex->cross(stilus::CrossAlign::Stretch // Start / Center / End / Stretch
);
```

`CrossAlign::Stretch` makes children expand to fill the parent in the cross-axis direction (e.g., stretching text inputs vertically inside a row).

### Common patterns

**Label + input row:**
```cpp
auto r = stilus::row();
r->gap(10).cross(stilus::CrossAlign::Center);
r->add(std::make_unique<stilus::Label>("Name"), 0, 80);   // 80px fixed
r->add(std::make_unique<stilus::TextInput>(""),   1);      // fill rest
```

**Buttons aligned right:**
```cpp
auto btn_row = stilus::row();
btn_row->gap(8);
btn_row->add(std::make_unique<stilus::Spacer>(), 1);       // spacer eats the left side
btn_row->add(std::make_unique<stilus::Button>("Cancel"), 0);
btn_row->add(std::make_unique<stilus::Button>("OK"),     0);
```

**Two columns:**
```cpp
auto grid = stilus::row();
grid->gap(12).cross(stilus::CrossAlign::Stretch);
grid->add(build_left_column(),  1);
grid->add(build_right_column(), 1);
```

---

## Widget Reference

All widgets are declared in `stilus/widgets.hpp`. Builder methods are chainable and return `*this`.

### `Label`
```cpp
Label(std::string text);
Label& color(Color c);
Label& font_size(float px);
Label& bold(bool b = true);
void   text(std::string s);
```
No wrapping. Newlines `\n` work but there's no auto-formatting; split long text manually (see `wrap_text_naive` in `ui.cpp`).

### `Button`
```cpp
Button(std::string label);
Button& on_click(std::function<void()>);
Button& primary(bool = true);   // prominent color
```

### `CheckBox`
```cpp
CheckBox(std::string text, bool checked = false);
CheckBox& on_change(std::function<void(bool)>);
CheckBox& checked(bool v);
bool checked() const;
```

### `TextInput`
```cpp
TextInput();                             // no placeholder
TextInput(std::string placeholder);
TextInput& set_text(std::string);
TextInput& on_change(std::function<void(const std::string&)>);
TextInput& on_submit(std::function<void(const std::string&)>);  // on Enter
const std::string& text() const;
```
Single line only. IME (Wayland `zwp_text_input_v3`) supported.

### `ComboBox`
```cpp
struct Item { std::string label; int index = -1; };

ComboBox(std::vector<Item> items);
ComboBox& on_select(std::function<void(int pos, const std::string& label)>);
ComboBox& select(int pos);           // set initial selection (doesn't fire on_select)
ComboBox& set_items(std::vector<Item>);   // replace items (e.g. after re-scan)
const std::string& text() const;
int selected_index() const;
```

### `Slider`
```cpp
Slider(float min, float max, float initial);
Slider& on_change(std::function<void(float)>);
float value() const;
void  set_value(float);
```

### `ProgressBar`
```cpp
ProgressBar(float value = 0.f, float min = 0.f, float max = 1.f);
ProgressBar& value(float v);   // clamped to 0..1
float value() const;
```

### `RadioGroup`
```cpp
RadioGroup(std::vector<std::string> labels);   // vertical layout
RadioGroup& add(std::string label);
RadioGroup& on_change(std::function<void(int idx)>);
RadioGroup& set_value(int idx);   // fires on_change
int value() const;
```

### `Panel`
```cpp
Panel();
Panel& title(std::string);              // title above border
Panel& padding(float);
Panel& child(std::unique_ptr<Widget>);  // single child only
```
Frame + title + single child. Put a Flex inside to arrange contents.

### `Tabs`
```cpp
Tabs();
Tabs& tab(std::string label, std::unique_ptr<Widget> content);
Tabs& on_change(std::function<void(int)>);
Tabs& select(int);
int   selected() const;
```
Switches between tabs vertically. No collapsible tabs or reordering.

### `ScrollView`
```cpp
ScrollView();
ScrollView& add(std::unique_ptr<Widget>);   // added to internal Flex(Vertical)
```
Vertical scroll only. Mouse wheel and scrollbar dragging supported. Content height is auto-calculated.

### `Divider`
```cpp
Divider(Axis axis = Horizontal, float thickness = 1.f);
Divider& inset(float px);
```

### `Spacer`
```cpp
Spacer();
Spacer(float min_size);
```
Placeholder for reserving space in Flex layouts, typically used with `flex=1`.

### `Image`
```cpp
Image(PixelImage img);   // RGBA, prepare PixelImage on your end
```
stilus doesn't provide PNG loading; create `PixelImage` yourself.

### `Container`
```cpp
Container();
Container& add(std::unique_ptr<Widget>);
```
Generic non-Flex container. Children are stacked overlapping the same area as the parent (useful for overlay-like usage).

### `Flex` (direct)
```cpp
Flex(Axis axis);
Flex& add(std::unique_ptr<Widget>, float flex = 0, float fixed = -1);
Flex& gap(float);
Flex& padding(float);
Flex& cross(CrossAlign);
```
Usually used via the `row()` / `column()` helpers.

---

## Events

### `Event` struct

```cpp
struct Event {
    enum class Type {
        None,
        MouseMove, MouseDown, MouseUp, MouseWheel,
        KeyDown, KeyUp, TextInput, Preedit,
        Resize, Close, Focus, Unfocus,
    };
    Type type;
    float x, y;                                // mouse position (logical pixels)
    MouseButton button;
    float wheel_dx, wheel_dy;
    Key key;
    KeyMods mods;
    uint32_t codepoint;                        // for TextInput
    std::string text;                          // single key or IME commit
    int preedit_cursor_begin, preedit_cursor_end;
    int width, height;                         // for Resize
};
```

Key constants (`stilus::Key::A`, `Escape`, `Enter`, `Tab`, `Left/Right/Up/Down`, `PageUp/Down`, `Home/End`, `Shift/Ctrl/Alt/Super`, etc.) are defined in `event.hpp`.

### Two kinds of handlers

**In a widget subclass:**
```cpp
class MyWidget : public stilus::Widget {
    bool on_event(const stilus::Event& e) override {
        // return true if consumed, false to delegate to parent
        return handled;
    }
};
```

**Directly on Window:**
```cpp
win.on_event([&](const stilus::Event& e) {
    if (e.type == stilus::Event::Type::Close) win.close();
    // to also dispatch to the widget tree:
    if (win.root()) win.root()->dispatch_event(e);
});
```

### Event dispatch flow

1. Wayland/X11 backends deliver events to the callback registered via `Window::on_event`.
2. When `Window::set_root` is active, an internal callback that dispatches to the widget tree is auto-registered.
3. **Calling `win.on_event(...) overwrites this auto-registration** (it's a replacement, not chaining).
4. Container (`Flex`, `Panel`, etc.) `dispatch_event` routes to children:
   - **MouseDown/MouseWheel**: Delivered only to the topmost child containing the mouse position.
   - **MouseMove/MouseUp**: Broadcast to all children (to clear hover states).
   - **Children returning `wants_capture()`**: Receive events regardless of position (e.g., ComboBox dropdown).
5. `on_event` returning `true` stops routing.

### Focus and Tab key

`Tab` automatically moves focus to the next focusable widget. Override `focusable() const` to return `true` to participate (default `Widget` returns `false`). Built-in focusable widgets: `Button`, `TextInput`, `CheckBox`, `Slider`, `ComboBox`, `RadioGroup`.

`Shift+Tab` reverses direction. `Enter`/`Space` activates the focused widget (e.g., clicks Button, opens ComboBox).

---

## Theme and Fonts

The `Theme` struct holds colors and pointers to `Font`:

```cpp
struct Theme {
    Color bg, surface, surface_hi, border, primary, accent, text, text_dim;
    float radius, padding, gap;
    std::shared_ptr<Font> font, font_bold;
};
```

Initialization:
```cpp
win.theme().font = std::make_shared<stilus::Font>(stilus::Font::from_file(
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 15.0f));
win.theme().font_bold = std::make_shared<stilus::Font>(stilus::Font::from_file(
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 15.0f));
```

If `font` is `null`, text won't render. Always set it. If you don't need bold, just assign the same font to `font_bold`.

### CJK / Color emoji fallback

```cpp
auto font = stilus::Font::from_file("...DejaVuSans.ttf", 15.f);
auto cjk  = stilus::Font::from_file(
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 15.f, 0);
if (cjk.valid()) font.add_fallback(std::move(cjk));
auto emoji = stilus::Font::from_file(
    "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf", 15.f);
if (emoji.valid()) font.add_fallback(std::move(emoji));
```

`add_fallback` chains fonts in order; codepoints missing from the primary are looked up in fallback faces. Color emoji are rendered via CBDT table as RGBA.

### Color

```cpp
Color::rgb(0xff8040);        // alpha = 1.0
Color::rgba(0xff804080);     // AARRGGBB (NOT AABBGGRR — high byte is alpha)
Color c { 1.f, 0.5f, 0.25f, 1.f };  // r, g, b, a all 0..1
```

---

## Animation

A one-shot timer: "call me on the next frame". For continuous animation, re-request:

```cpp
std::function<void(float)> tick;
tick = [&](float dt /* seconds */) {
    // advance state by dt
    my_pos_x += velocity_x * dt;
    if (moving) win.request_animation_frame(tick);
};
win.request_animation_frame(tick);
```

`request_animation_frame` also requests a redraw, so after `tick` advances state, `paint` will be called.

---

## Modals, Popups, Dialogs

**stilus has no built-in modal dialogs, file pickers, alerts, etc.** Implement them yourself on top of the widget tree:

```cpp
class AppRoot : public stilus::Widget {
    std::unique_ptr<Widget> form_;
    std::unique_ptr<Widget> modal_;
public:
    void show_modal(std::unique_ptr<Widget> w) { modal_ = std::move(w); invalidate(); }
    void close_modal() { modal_.reset(); invalidate(); }

    void paint(Canvas& c, const Theme& t) override { form_->paint(c, t); }
    void paint_overlay(Canvas& c, const Theme& t) override {
        form_->paint_overlay(c, t);
        if (modal_) {
            c.fill_rect(rect_, Color{0,0,0,0.55f});   // dim background
            modal_->paint(c, t);
            modal_->paint_overlay(c, t);
        }
    }
    bool dispatch_event(const Event& e) override {
        if (modal_) { modal_->dispatch_event(e); return true; }  // block interaction below
        return form_->dispatch_event(e);
    }
    // also implement measure / layout / hit / child_count / child
};
```

See `rufus-linux/src/ui.cpp` for a complete `AppRoot` implementation.

### `Popup` class (separate window)

`stilus::Popup` is a thin wrapper around Wayland `xdg_popup` (and X11 override-redirect). It floats as a **separate surface** above the parent window (use case: right-click menus):

```cpp
stilus::Popup pop(win, /*anchor rect=*/{100, 200, 20, 20}, /*w=*/200, /*h=*/120);
pop.on_frame([](stilus::Canvas& c) { /* custom drawing */ });
pop.on_event(...);
```

For in-window dialogs (dim overlay on top of a form), stick to the widget tree approach.

---

## Clipboard

```cpp
stilus::App::instance().clipboard_set_text("hello");
std::string got = stilus::App::instance().clipboard_get_text();
```

Both Wayland `wl_data_device` and X11 `CLIPBOARD` selection (with INCR protocol support) are implemented. `get_text` is a synchronous API but internally runs the event loop briefly to wait for the selection owner's response.

---

## Keyboard Layout and IME (Wayland)

### Layout

The XKB keymap text sent by the compositor is parsed in-house (`src/platform/wayland/xkb_parse.cpp`). JIS (Japanese) is supported, including `yen`, `overline`, `voicedsound`, `kana_*`, etc. Other layouts (DE/FR/DVORAK etc.) work if their keysym names are in the lookup table; unknown keysyms fall back to `UXXXX` literal format. On parse failure only, a hardcoded US table is used.

Internally, `Display` in `wl_proto.cpp` has `register_fd_message(object, opcode)` which auto-registers `wl_keyboard.keymap`. This mechanism prevents fd-passing events from being intercepted by unrelated handlers — fds are delivered only to known (object, opcode) pairs. If you roll your own protocol extension that receives an fd, register it the same way.

### IME

`zwp_text_input_v3` is supported. The `TextInput` widget automatically enables IME on focus and disables on blur. Preedit text is drawn in `text_dim` color; commit inserts into the text.

To write a custom text-input widget, override `Widget::set_focused` and call `notify_ime(true|false)` on state change:

```cpp
void set_focused(bool v) override {
    bool was = focused();
    Widget::set_focused(v);
    if (focused() != was) notify_ime(focused());
}
```

`notify_ime` propagates up the parent chain to `set_ime_enabled` on the window, just like `invalidate`.

IME (XIM/IBus) is not supported on the X11 backend.

---

## Coordinate System and HiDPI

- Coordinates passed to widgets (rect, event.x/y, Canvas API) are all **logical pixels**.
- On HiDPI displays, stilus automatically allocates a buffer at physical pixel scale and applies a scale transform to the `Canvas`. User code doesn't need to worry about the scale factor.
- Current scale value: `win.scale_factor()` (integer). Fractional scale (`wp_fractional_scale_v1`) is also supported.
- A widget's `rect_` is in absolute coordinates relative to the window origin (no need to subtract parent rect). Hit-testing and hover detection just needs `rect_.contains({e.x, e.y})`.

---

## Gotchas

### 1. Calling `on_event()` after `set_root()` kills auto-dispatch

`Window::set_root` internally registers `impl_->set_event_cb([root]...)`. If you later call `win.on_event(...)`, it **overwrites** this, so the widget tree no longer receives events. If you need both, re-dispatch manually:

```cpp
win.set_root(std::move(root));
win.on_event([&](const stilus::Event& e) {
    if (e.type == stilus::Event::Type::Close) { win.close(); return; }
    if (e.type == stilus::Event::Type::KeyDown && e.key == stilus::Key::Escape)
        win.close();
    if (win.root()) win.root()->dispatch_event(e);   // ← required
});
```

Both `examples/widget_demo.cpp` and `rufus-linux/src/ui.cpp` use this pattern.

### 2. Null Font (`Theme::font == nullptr`)

`Label`, `Button`, `CheckBox`, `TextInput`, `ComboBox`, `RadioGroup`, `Panel` title, and `Tabs` labels — all text rendering depends on `Theme::font`. If it's null, nothing appears (no error, just blank). Always set it right after creating a Window.

### 3. `wants_capture()` is recursive

Previously `Flex::wants_capture()` was hardcoded to `false`, so deeply nested `ComboBox`es (e.g., ComboBox inside a row inside a column inside a row) would have their dropdown clicks swallowed by parent containers. This is now fixed: `Flex` queries descendants and propagates. If you write a **custom container**, implement the same pattern:

```cpp
bool wants_capture() const override {
    for (size_t i = 0; i < child_count(); ++i)
        if (child(i)->wants_capture()) return true;
    return false;
}
```

### 4. Pass-through in `dispatch_event`

When writing a custom container, `dispatch_event` should follow "deliver to children first, fall back to own `on_event` if unconsumed". Mouse events need position checks (don't deliver to children outside the parent). A simple pass-through:

```cpp
bool dispatch_event(const Event& e) override {
    for (size_t i = child_count(); i-- > 0;) {
        if (child(i)->dispatch_event(e)) return true;
    }
    return on_event(e);
}
```

For a complex example, read `Flex::dispatch_event` in `src/widget.cpp`.

### 5. Widget construction order and builder methods

```cpp
auto btn = std::make_unique<stilus::Button>("OK");
btn->primary().on_click([]{ ... });   // OK, via unique_ptr
```

works, but:

```cpp
auto btn = std::make_unique<stilus::Button>("OK").primary();
// error: unique_ptr has no method primary()
```

doesn't (`primary()` is a Widget method). Assign to a `unique_ptr` first and use `->`.

### 6. `Container` vs `Flex`

`Container` **stacks children** (absolutely positioned). Use `Flex` (`row()` / `column()`) for sequential layout. The names are confusing but the use cases are different.

### 7. `Label` doesn't wrap

Long text needs manual newlines or a word-wrapping helper. `Label` has no `max_width` concept.

### 8. `TextInput` doesn't validate

For numeric input, validate in `on_change` or parse on `on_submit`. There's no spin button widget.

### 9. Performance

Since stilus uses a software rasterizer, full-screen redraws are not free (~5-10ms at 4K). For frequently updated areas, call `invalidate(rect)` for partial redraws, or keep `request_animation_frame` rate conservative.

---

## Reference: Working examples

- **`examples/widget_demo.cpp`**: All-in-one: Tabs / Panel / RadioGroup / ScrollView / ComboBox / CheckBox / Slider / TextInput / Button.
- **`examples/anim_demo.cpp`**: Bouncing ball using `request_animation_frame`.
- **`examples/clipboard_demo.cpp`**: Ctrl+C / Ctrl+V clipboard integration.
- **`examples/emoji_demo.cpp`**: Font fallback + color emoji.
- **`examples/transform_demo.cpp`**: Affine transforms and arbitrary path clipping (low-level Canvas API).
- **`rufus-linux/`** (sibling repo): Real-world app example. Form-based, with custom AppRoot for modals, file picker, confirmation dialogs, and `std::thread` background job integration (polling via `request_animation_frame`).
