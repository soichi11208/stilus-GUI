// src/platform/wayland/wl_keymap.hpp
// Minimal Linux evdev keycode -> stilus::Key translation.
//
// Wayland's wl_keyboard.key carries raw evdev keycodes. We translate the
// small subset we expose in the public Key enum. Text input (UTF-32
// codepoints from a layout) is deferred; we either ship our own xkb parser
// later or optionally link xkbcommon. For now KeyDown/KeyUp is enough to
// drive shortcuts, navigation, and simple widgets.
#pragma once

#include <cstdint>
#include "stilus/event.hpp"

namespace stilus::wlw {

// Linux evdev codes we care about (from linux/input-event-codes.h).
enum : uint32_t {
    EV_ESC        = 1,
    EV_1 = 2,  EV_2,  EV_3,  EV_4,  EV_5,  EV_6,  EV_7,  EV_8,  EV_9,
    EV_0          = 11,
    EV_MINUS      = 12,
    EV_EQUAL      = 13,
    EV_BACKSPACE  = 14,
    EV_TAB        = 15,
    EV_Q = 16, EV_W, EV_E, EV_R, EV_T, EV_Y, EV_U, EV_I, EV_O, EV_P,
    EV_LBRACE     = 26,
    EV_RBRACE     = 27,
    EV_ENTER      = 28,
    EV_LCTRL      = 29,
    EV_A = 30, EV_S, EV_D, EV_F, EV_G, EV_H, EV_J, EV_K, EV_L,
    EV_SEMICOLON  = 39,
    EV_APOSTROPHE = 40,
    EV_GRAVE      = 41,
    EV_LSHIFT     = 42,
    EV_BACKSLASH  = 43,
    EV_Z = 44, EV_X, EV_C, EV_V, EV_B, EV_N, EV_M,
    EV_COMMA      = 51,
    EV_DOT        = 52,
    EV_SLASH      = 53,
    EV_RSHIFT     = 54,
    EV_LALT       = 56,
    EV_SPACE      = 57,
    EV_CAPSLOCK   = 58,
    EV_F1 = 59, EV_F2, EV_F3, EV_F4, EV_F5, EV_F6,
    EV_F7, EV_F8, EV_F9, EV_F10,
    EV_RCTRL      = 97,
    EV_RALT       = 100,
    EV_HOME       = 102,
    EV_UP         = 103,
    EV_PAGEUP     = 104,
    EV_LEFT       = 105,
    EV_RIGHT      = 106,
    EV_END        = 107,
    EV_DOWN       = 108,
    EV_PAGEDOWN   = 109,
    EV_INSERT     = 110,
    EV_DELETE     = 111,
    EV_LSUPER     = 125,
    EV_RSUPER     = 126,
};

inline Key evdev_to_key(uint32_t c) {
    switch (c) {
        case EV_A: return Key::A;  case EV_B: return Key::B;
        case EV_C: return Key::C;  case EV_D: return Key::D;
        case EV_E: return Key::E;  case EV_F: return Key::F;
        case EV_G: return Key::G;  case EV_H: return Key::H;
        case EV_I: return Key::I;  case EV_J: return Key::J;
        case EV_K: return Key::K;  case EV_L: return Key::L;
        case EV_M: return Key::M;  case EV_N: return Key::N;
        case EV_O: return Key::O;  case EV_P: return Key::P;
        case EV_Q: return Key::Q;  case EV_R: return Key::R;
        case EV_S: return Key::S;  case EV_T: return Key::T;
        case EV_U: return Key::U;  case EV_V: return Key::V;
        case EV_W: return Key::W;  case EV_X: return Key::X;
        case EV_Y: return Key::Y;  case EV_Z: return Key::Z;
        case EV_0: return Key::Num0; case EV_1: return Key::Num1;
        case EV_2: return Key::Num2; case EV_3: return Key::Num3;
        case EV_4: return Key::Num4; case EV_5: return Key::Num5;
        case EV_6: return Key::Num6; case EV_7: return Key::Num7;
        case EV_8: return Key::Num8; case EV_9: return Key::Num9;
        case EV_SPACE:     return Key::Space;
        case EV_ESC:       return Key::Escape;
        case EV_ENTER:     return Key::Enter;
        case EV_TAB:       return Key::Tab;
        case EV_BACKSPACE: return Key::Backspace;
        case EV_DELETE:    return Key::Delete;
        case EV_LEFT:      return Key::Left;
        case EV_RIGHT:     return Key::Right;
        case EV_UP:        return Key::Up;
        case EV_DOWN:      return Key::Down;
        case EV_HOME:      return Key::Home;
        case EV_END:       return Key::End;
        case EV_PAGEUP:    return Key::PageUp;
        case EV_PAGEDOWN:  return Key::PageDown;
        case EV_LSHIFT: case EV_RSHIFT: return Key::Shift;
        case EV_LCTRL:  case EV_RCTRL:  return Key::Ctrl;
        case EV_LALT:   case EV_RALT:   return Key::Alt;
        case EV_LSUPER: case EV_RSUPER: return Key::Super;
        default: return Key::Unknown;
    }
}

// US-layout codepoint for printable keys. Returns 0 for non-printable
// (modifiers, arrows, F-keys, etc). This is the placeholder until we parse
// the compositor's xkb keymap — it handles the ASCII set correctly on a US
// layout, which is the default on most desktops.
inline uint32_t evdev_to_codepoint(uint32_t c, bool shift) {
    switch (c) {
        // Letters
        case EV_A: return shift ? 'A' : 'a'; case EV_B: return shift ? 'B' : 'b';
        case EV_C: return shift ? 'C' : 'c'; case EV_D: return shift ? 'D' : 'd';
        case EV_E: return shift ? 'E' : 'e'; case EV_F: return shift ? 'F' : 'f';
        case EV_G: return shift ? 'G' : 'g'; case EV_H: return shift ? 'H' : 'h';
        case EV_I: return shift ? 'I' : 'i'; case EV_J: return shift ? 'J' : 'j';
        case EV_K: return shift ? 'K' : 'k'; case EV_L: return shift ? 'L' : 'l';
        case EV_M: return shift ? 'M' : 'm'; case EV_N: return shift ? 'N' : 'n';
        case EV_O: return shift ? 'O' : 'o'; case EV_P: return shift ? 'P' : 'p';
        case EV_Q: return shift ? 'Q' : 'q'; case EV_R: return shift ? 'R' : 'r';
        case EV_S: return shift ? 'S' : 's'; case EV_T: return shift ? 'T' : 't';
        case EV_U: return shift ? 'U' : 'u'; case EV_V: return shift ? 'V' : 'v';
        case EV_W: return shift ? 'W' : 'w'; case EV_X: return shift ? 'X' : 'x';
        case EV_Y: return shift ? 'Y' : 'y'; case EV_Z: return shift ? 'Z' : 'z';
        // Digit row
        case EV_1: return shift ? '!' : '1'; case EV_2: return shift ? '@' : '2';
        case EV_3: return shift ? '#' : '3'; case EV_4: return shift ? '$' : '4';
        case EV_5: return shift ? '%' : '5'; case EV_6: return shift ? '^' : '6';
        case EV_7: return shift ? '&' : '7'; case EV_8: return shift ? '*' : '8';
        case EV_9: return shift ? '(' : '9'; case EV_0: return shift ? ')' : '0';
        // Punctuation
        case EV_MINUS:      return shift ? '_' : '-';
        case EV_EQUAL:      return shift ? '+' : '=';
        case EV_LBRACE:     return shift ? '{' : '[';
        case EV_RBRACE:     return shift ? '}' : ']';
        case EV_BACKSLASH:  return shift ? '|' : '\\';
        case EV_SEMICOLON:  return shift ? ':' : ';';
        case EV_APOSTROPHE: return shift ? '"' : '\'';
        case EV_GRAVE:      return shift ? '~' : '`';
        case EV_COMMA:      return shift ? '<' : ',';
        case EV_DOT:        return shift ? '>' : '.';
        case EV_SLASH:      return shift ? '?' : '/';
        case EV_SPACE:      return ' ';
        default: return 0;
    }
}

// Encode UTF-32 codepoint into UTF-8. Writes up to 4 bytes + NUL into `out`
// (buffer must hold at least 5 bytes). Returns number of bytes written.
inline int utf8_encode(uint32_t cp, char* out) {
    if (cp < 0x80) {
        out[0] = char(cp); out[1] = 0; return 1;
    } else if (cp < 0x800) {
        out[0] = char(0xC0 | (cp >> 6));
        out[1] = char(0x80 | (cp & 0x3F));
        out[2] = 0; return 2;
    } else if (cp < 0x10000) {
        out[0] = char(0xE0 | (cp >> 12));
        out[1] = char(0x80 | ((cp >> 6) & 0x3F));
        out[2] = char(0x80 | (cp & 0x3F));
        out[3] = 0; return 3;
    } else if (cp < 0x110000) {
        out[0] = char(0xF0 | (cp >> 18));
        out[1] = char(0x80 | ((cp >> 12) & 0x3F));
        out[2] = char(0x80 | ((cp >> 6) & 0x3F));
        out[3] = char(0x80 | (cp & 0x3F));
        out[4] = 0; return 4;
    }
    out[0] = 0; return 0;
}

} // namespace stilus::wlw
