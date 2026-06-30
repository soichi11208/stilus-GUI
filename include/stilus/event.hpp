// stilus/event.hpp - input/window events
#pragma once
#include <cstdint>
#include <string>

namespace stilus {

enum class MouseButton : uint8_t { Left, Right, Middle, X1, X2 };

enum class Key : uint32_t {
    Unknown = 0,
    // Letters (USB HID-ish usage, but we map to ascii for simplicity)
    A = 'a', B = 'b', C = 'c', D = 'd', E = 'e', F = 'f', G = 'g',
    H = 'h', I = 'i', J = 'j', K = 'k', L = 'l', M = 'm', N = 'n',
    O = 'o', P = 'p', Q = 'q', R = 'r', S = 's', T = 't', U = 'u',
    V = 'v', W = 'w', X = 'x', Y = 'y', Z = 'z',
    Num0='0',Num1='1',Num2='2',Num3='3',Num4='4',Num5='5',
    Num6='6',Num7='7',Num8='8',Num9='9',
    Space = ' ',
    Escape = 0x1000, Enter, Tab, Backspace, Delete,
    Left, Right, Up, Down,
    Home, End, PageUp, PageDown,
    Shift, Ctrl, Alt, Super,
};

struct KeyMods {
    bool shift = false;
    bool ctrl  = false;
    bool alt   = false;
    bool super = false;
};

struct Event {
    enum class Type {
        None,
        MouseMove, MouseDown, MouseUp, MouseWheel,
        KeyDown, KeyUp, TextInput, Preedit,
        Resize, Close, Focus, Unfocus,
    };
    Type type = Type::None;

    // Common (valid for mouse events)
    float x = 0, y = 0;

    // Mouse-specific
    MouseButton button = MouseButton::Left;
    float wheel_dx = 0, wheel_dy = 0;

    // Keyboard-specific
    Key     key = Key::Unknown;
    KeyMods mods{};
    uint32_t codepoint = 0; // for single-character TextInput
    // UTF-8 payload. Used for single-key TextInput (short) and IME
    // commit/preedit strings (potentially many bytes).
    std::string text;

    // Preedit-specific: byte offsets into `text` marking the IME selection.
    // -1 means "hide" — no visible cursor within the preedit.
    int preedit_cursor_begin = -1;
    int preedit_cursor_end   = -1;

    // Resize
    int width = 0, height = 0;
};

} // namespace stilus
