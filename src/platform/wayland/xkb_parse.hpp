// src/platform/wayland/xkb_parse.hpp
//
// Minimal XKB v1 keymap parser. The compositor sends the active keymap as a
// text file over wl_keyboard.keymap (via an fd passed in SCM_RIGHTS); we
// parse it ourselves so the library has no libxkbcommon dependency.
//
// Scope:
//   * Parses only the subset we need: the xkb_keycodes section (for the
//     `<NAME> = EVDEV_CODE;` map) and the xkb_symbols section's per-key
//     `key <NAME> { [ sym0, sym1, sym2, sym3 ], ... };` lists.
//   * Handles `include "..."` — we follow the chain up to a small depth, but
//     we don't actually read other files (the compositor already flattens
//     the keymap before sending it, so includes are effectively no-ops in
//     the text we receive from Wayland).
//   * Keysym → Unicode is resolved via a curated name table covering all
//     of US ASCII plus the common JIS layout additions (yen, voicedsound,
//     etc.) and the `U1234`/`U+1234` direct-codepoint form.
//
// Unknown keys simply fall through to the hardcoded US fallback in
// wl_keymap.hpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace stilus::wlw {

class XkbKeymap {
public:
    // Up to 4 levels per key: [0]=base, [1]=shift, [2]=level3 (AltGr),
    // [3]=shift+level3. Values are Unicode codepoints; 0 means "no symbol".
    struct Entry { uint32_t cp[4] = {0, 0, 0, 0}; };

    // Indexed by evdev keycode. Empty() => parse failed, use fallback.
    bool valid() const { return !table_.empty(); }

    // Return the Unicode codepoint for `evdev_code` given current modifier
    // state. Returns 0 for non-printable or unknown keys.
    uint32_t codepoint(uint32_t evdev_code, bool shift, bool level3, bool caps) const;

    // Parse the keymap text in-place. `text` does not need to be NUL-
    // terminated; `len` is the exact byte length. Returns true on success.
    bool parse(const char* text, size_t len);

private:
    std::vector<Entry> table_;
};

} // namespace stilus::wlw
