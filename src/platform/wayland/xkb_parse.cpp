// src/platform/wayland/xkb_parse.cpp
#include "xkb_parse.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace stilus::wlw {

namespace {

// --- Keysym name → Unicode codepoint ---------------------------------------
//
// Covers the ASCII range (named forms like `exclam`, `underscore`, …) plus
// a handful of Japanese-layout names (`yen`, `overline`, `voicedsound`, …).
// Everything else we either resolve via the `UXXXX` direct form or return
// 0 to mean "no printable symbol at this level".
uint32_t keysym_to_cp(std::string_view n) {
    if (n.empty()) return 0;

    // Single-char names: the keysym name IS the ASCII character. XKB uses
    // this for a..z, A..Z, 0..9. (Punctuation keysyms all have names.)
    if (n.size() == 1) {
        unsigned char c = (unsigned char)n[0];
        if (c >= 0x20 && c < 0x7F) return c;
        return 0;
    }

    // Direct-Unicode form: U00A5, U+00A5. Some keymaps emit this for CJK
    // symbols that don't have a legacy name in keysymdef.h.
    if ((n[0] == 'U' || n[0] == 'u') && n.size() >= 2) {
        size_t i = 1;
        if (n[1] == '+') ++i;
        uint32_t cp = 0;
        bool any = false;
        for (; i < n.size(); ++i) {
            char c = n[i];
            int d;
            if      (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
            else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
            else { any = false; break; }
            cp = cp * 16 + uint32_t(d);
            any = true;
        }
        if (any && cp >= 0x20 && cp < 0x110000) return cp;
    }

    static const std::unordered_map<std::string_view, uint32_t> table = {
        {"space",          0x0020}, {"exclam",         0x0021},
        {"quotedbl",       0x0022}, {"numbersign",     0x0023},
        {"dollar",         0x0024}, {"percent",        0x0025},
        {"ampersand",      0x0026}, {"apostrophe",     0x0027},
        {"quoteright",     0x0027}, {"parenleft",      0x0028},
        {"parenright",     0x0029}, {"asterisk",       0x002A},
        {"plus",           0x002B}, {"comma",          0x002C},
        {"minus",          0x002D}, {"period",         0x002E},
        {"slash",          0x002F}, {"colon",          0x003A},
        {"semicolon",      0x003B}, {"less",           0x003C},
        {"equal",          0x003D}, {"greater",        0x003E},
        {"question",       0x003F}, {"at",             0x0040},
        {"bracketleft",    0x005B}, {"backslash",      0x005C},
        {"bracketright",   0x005D}, {"asciicircum",    0x005E},
        {"underscore",     0x005F}, {"grave",          0x0060},
        {"quoteleft",      0x0060}, {"braceleft",      0x007B},
        {"bar",            0x007C}, {"braceright",     0x007D},
        {"asciitilde",     0x007E},
        // Japanese-layout additions
        {"yen",            0x00A5}, {"overline",       0x203E},
        {"voicedsound",    0x309B}, {"semivoicedsound",0x309C},
        {"kana_fullstop",  0x3002}, {"kana_openingbracket", 0x300C},
        {"kana_closingbracket", 0x300D}, {"kana_comma", 0x3001},
        {"kana_conjunctive",    0x30FB},
        {"kana_middledot", 0x30FB},
        // Degrees/pilcrow/etc. show up occasionally
        {"section",        0x00A7}, {"degree",         0x00B0},
        {"paragraph",      0x00B6}, {"periodcentered", 0x00B7},
        // Explicit no-symbol markers
        {"NoSymbol",       0}, {"VoidSymbol", 0},
    };
    auto it = table.find(n);
    if (it != table.end()) return it->second;
    return 0;
}

// --- Tokenizer -------------------------------------------------------------
struct Tok {
    const char* p;
    const char* end;

    void ws() {
        while (p < end) {
            char c = *p;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++p; continue; }
            if (c == '/' && p + 1 < end) {
                if (p[1] == '/') {
                    while (p < end && *p != '\n') ++p;
                    continue;
                }
                if (p[1] == '*') {
                    p += 2;
                    while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) ++p;
                    if (p + 1 < end) p += 2;
                    continue;
                }
            }
            break;
        }
    }

    bool eof() { ws(); return p >= end; }
    char peek() { ws(); return p < end ? *p : 0; }
    bool eat(char c) {
        ws();
        if (p < end && *p == c) { ++p; return true; }
        return false;
    }

    // Returns an identifier, <keyname>, "string", or single punctuation.
    // Strings keep their surrounding quotes; keynames keep their angle
    // brackets. Numbers are returned as identifiers (digit-prefixed).
    std::string_view token() {
        ws();
        if (p >= end) return {};
        const char* s = p;
        char c = *p;
        if (c == '<') {
            ++p;
            while (p < end && *p != '>') ++p;
            if (p < end) ++p;
            return {s, size_t(p - s)};
        }
        if (c == '"') {
            ++p;
            while (p < end && *p != '"') ++p;
            if (p < end) ++p;
            return {s, size_t(p - s)};
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            while (p < end && (std::isalnum((unsigned char)*p) || *p == '_')) ++p;
            return {s, size_t(p - s)};
        }
        if (std::isdigit((unsigned char)c)) {
            while (p < end && (std::isalnum((unsigned char)*p) || *p == 'x' || *p == 'X')) ++p;
            return {s, size_t(p - s)};
        }
        ++p;
        return {s, 1};
    }

    // Skip tokens until a top-level `}` at nesting depth 0 is consumed.
    void skip_to_close_brace() {
        int depth = 1;
        while (!eof()) {
            std::string_view t = token();
            if (t.size() == 1) {
                if (t[0] == '{') ++depth;
                else if (t[0] == '}') { if (--depth == 0) return; }
            }
        }
    }
};

uint32_t parse_int(std::string_view s) {
    // xkb keycodes are decimal by convention but accept hex `0x..` just in case.
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        return (uint32_t)std::strtoul(std::string(s).c_str(), nullptr, 16);
    }
    return (uint32_t)std::strtoul(std::string(s).c_str(), nullptr, 10);
}

} // namespace

// ---------------------------------------------------------------------------
// XkbKeymap
// ---------------------------------------------------------------------------
uint32_t XkbKeymap::codepoint(uint32_t ev, bool shift, bool level3, bool caps) const {
    if (ev >= table_.size()) return 0;
    const Entry& e = table_[ev];

    // Level selection. XKB has per-key "type" definitions that pick the
    // level from modifier state; we flatten that down to: shift=>level 1,
    // AltGr=>level 2, both=>level 3. Caps Lock toggles the shift choice
    // only for alphabetic keys (we detect that by comparing the base/shift
    // codepoints — if the shifted version is the base's uppercase we treat
    // the key as alphabetic).
    int lv = (shift ? 1 : 0) + (level3 ? 2 : 0);
    uint32_t cp = e.cp[lv];

    if (caps && !level3) {
        uint32_t base = e.cp[0];
        uint32_t sh   = e.cp[1];
        bool alpha = base != 0 && sh != 0 &&
                     ((base >= 'a' && base <= 'z' && sh == base - 0x20) ||
                      (base >= 'A' && base <= 'Z' && sh == base + 0x20));
        if (alpha) cp = shift ? e.cp[0] : e.cp[1];
    }

    // Level-3 fallback: if no AltGr variant is defined, fall back to the
    // non-AltGr sibling. This matches xkbcommon's behavior.
    if (cp == 0 && lv >= 2) cp = e.cp[lv - 2];
    return cp;
}

// Sub-parsers --------------------------------------------------------------
//
// parse_keycodes_section: after `xkb_keycodes "name" {` has been seen,
// reads entries of the form `<NAME> = INT;` until the matching `}`.
static void parse_keycodes_section(
    Tok& t, std::unordered_map<std::string, uint32_t>& name_to_ev)
{
    while (!t.eof()) {
        std::string_view tok = t.token();
        if (tok.size() == 1 && tok[0] == '}') return;
        if (tok.empty()) return;

        if (tok.front() == '<' && tok.back() == '>') {
            // <NAME> = NUM ;
            if (!t.eat('=')) continue;
            std::string_view num = t.token();
            if (!num.empty() && std::isdigit((unsigned char)num[0])) {
                name_to_ev[std::string(tok)] = parse_int(num);
            }
            // consume trailing semicolon if present
            t.eat(';');
            continue;
        }
        // alias <A> = <B>; and other statements: skip until ;
        while (!t.eof()) {
            std::string_view s = t.token();
            if (s.size() == 1 && s[0] == ';') break;
            if (s.size() == 1 && s[0] == '}') return;
        }
    }
}

// parse_symbol_list: at a `[`, read comma-separated symbol names up to `]`.
// Writes up to 4 entries into `out`; extra entries are dropped.
static int parse_symbol_list(Tok& t, uint32_t out[4]) {
    if (!t.eat('[')) return 0;
    int n = 0;
    bool expect_comma = false;
    while (!t.eof()) {
        std::string_view s = t.token();
        if (s.size() == 1 && s[0] == ']') break;
        if (s.size() == 1 && s[0] == ',') { expect_comma = false; continue; }
        if (expect_comma) continue; // malformed: missing comma
        // A single symbol — but some keymaps write `a` or `a(foo)` (group
        // modifiers in parens); strip the paren part if we see it.
        if (n < 4) out[n] = keysym_to_cp(s);
        if (n < 4) ++n;
        expect_comma = true;
        // Swallow parenthesized decoration like `ISO_Level3_Latch(onesuperior)`.
        if (t.peek() == '(') {
            int depth = 0;
            while (!t.eof()) {
                char c = t.peek();
                if (c == '(') { ++depth; t.token(); }
                else if (c == ')') { --depth; t.token(); if (depth == 0) break; }
                else t.token();
            }
        }
    }
    return n;
}

// parse_key_body: inside `key <NAME> { HERE }`. Pulls out the first
// `[...]` or `symbols[...] = [...]` we find and stores it in `entry`.
// Later entries override earlier ones (matches XKB include semantics).
static void parse_key_body(Tok& t, XkbKeymap::Entry& entry) {
    while (!t.eof()) {
        std::string_view tok = t.token();
        if (tok.size() == 1 && tok[0] == '}') return;
        if (tok.size() == 1 && tok[0] == ',') continue;

        if (tok.size() == 1 && tok[0] == '[') {
            // Rewind one char — parse_symbol_list expects to eat its own `[`.
            --t.p;
            uint32_t tmp[4] = {0, 0, 0, 0};
            parse_symbol_list(t, tmp);
            for (int i = 0; i < 4; ++i) if (tmp[i]) entry.cp[i] = tmp[i];
            continue;
        }

        if (tok == "symbols") {
            // Expected form: `symbols[group1] = [ sym0, sym1, ... ]`.
            // Skip the [group...] subscript (balanced brackets) first so we
            // don't misread it as the actual symbol list.
            if (t.peek() == '[') {
                t.token();  // consume '['
                int depth = 1;
                while (!t.eof() && depth > 0) {
                    std::string_view s = t.token();
                    if (s.size() == 1) {
                        if (s[0] == '[') ++depth;
                        else if (s[0] == ']') --depth;
                    }
                }
            }
            t.eat('=');
            if (t.peek() == '[') {
                uint32_t tmp[4] = {0, 0, 0, 0};
                parse_symbol_list(t, tmp);
                for (int i = 0; i < 4; ++i) if (tmp[i]) entry.cp[i] = tmp[i];
            }
            continue;
        }

        // Unknown property (type=..., actions=..., repeat=..., etc). Inside
        // a `key {}` body, properties are comma-separated; the body ends at
        // `}`. Skip until we hit a top-level `,` or `}`, stepping over any
        // nested `[...]` or `{...}` contents.
        while (!t.eof()) {
            std::string_view s = t.token();
            if (s.size() == 1 && (s[0] == ',' || s[0] == '}')) {
                if (s[0] == '}') return;
                break;
            }
            if (s.size() == 1 && s[0] == '[') {
                int depth = 1;
                while (!t.eof() && depth > 0) {
                    std::string_view ss = t.token();
                    if (ss.size() == 1) {
                        if (ss[0] == '[') ++depth;
                        else if (ss[0] == ']') --depth;
                    }
                }
            } else if (s.size() == 1 && s[0] == '{') {
                t.skip_to_close_brace();
            }
        }
    }
}

// parse_symbols_section: after `xkb_symbols "name" {`.
static void parse_symbols_section(
    Tok& t,
    const std::unordered_map<std::string, uint32_t>& name_to_ev,
    std::vector<XkbKeymap::Entry>& table)
{
    while (!t.eof()) {
        std::string_view tok = t.token();
        if (tok.empty()) return;
        if (tok.size() == 1 && tok[0] == '}') return;

        if (tok == "key") {
            std::string_view name = t.token();
            if (!name.empty() && name.front() == '<' && name.back() == '>') {
                if (!t.eat('{')) continue;
                XkbKeymap::Entry e;
                auto it = name_to_ev.find(std::string(name));
                uint32_t ev = (it != name_to_ev.end()) ? it->second : 0;
                // xkb_keycodes stores kernel-level keycodes: evdev +
                // MIN_KEYCODE (8). Convert back to evdev for our table.
                if (ev >= 8) ev -= 8;
                parse_key_body(t, e);
                t.eat(';');
                if (ev > 0 && ev < 512) {
                    if (table.size() <= ev) table.resize(ev + 1);
                    // If any non-zero level, commit the entry (but don't
                    // overwrite an earlier valid entry with all-zero from
                    // a suppressed key).
                    bool any = e.cp[0] || e.cp[1] || e.cp[2] || e.cp[3];
                    if (any) table[ev] = e;
                }
            } else {
                // key.foo = ...; style — skip to ;
                while (!t.eof()) {
                    std::string_view s = t.token();
                    if (s.size() == 1 && (s[0] == ';' || s[0] == '}')) {
                        if (s[0] == '}') return;
                        break;
                    }
                }
            }
            continue;
        }

        // Unknown top-level statement (include, name, modifier_map, …): skip
        // until the next ;, or handle balanced { } blocks.
        if (tok.size() == 1 && tok[0] == '{') {
            t.skip_to_close_brace();
            continue;
        }
        while (!t.eof()) {
            std::string_view s = t.token();
            if (s.size() == 1 && s[0] == ';') break;
            if (s.size() == 1 && s[0] == '}') return;
            if (s.size() == 1 && s[0] == '{') { t.skip_to_close_brace(); break; }
        }
    }
}

bool XkbKeymap::parse(const char* text, size_t len) {
    if (!text || len == 0) return false;
    Tok t{text, text + len};

    std::unordered_map<std::string, uint32_t> name_to_ev;

    // We scan the whole file. xkb_keycodes must come before xkb_symbols so
    // symbols can resolve key names — a single pass is enough.
    while (!t.eof()) {
        std::string_view tok = t.token();
        if (tok.empty()) break;

        if (tok == "xkb_keycodes") {
            // optional "name" then {
            while (!t.eof()) {
                std::string_view s = t.token();
                if (s.size() == 1 && s[0] == '{') { break; }
                if (s.size() == 1 && s[0] == ';') { break; }
            }
            parse_keycodes_section(t, name_to_ev);
            continue;
        }
        if (tok == "xkb_symbols") {
            while (!t.eof()) {
                std::string_view s = t.token();
                if (s.size() == 1 && s[0] == '{') { break; }
                if (s.size() == 1 && s[0] == ';') { break; }
            }
            parse_symbols_section(t, name_to_ev, table_);
            continue;
        }
        // `xkb_keymap { … }` is a wrapper — skip the `{` but keep scanning.
        // The trailing `}` is treated as a stray close and ignored below.
        if (tok == "xkb_keymap") { t.eat('{'); continue; }

        // Any other top-level section (xkb_types, xkb_compatibility,
        // xkb_geometry, …): they're introduced by an identifier followed
        // by optional "name" then a `{ … }` block. Skip the whole block.
        if (tok == "xkb_types" || tok == "xkb_compatibility" ||
            tok == "xkb_geometry") {
            while (!t.eof()) {
                std::string_view s = t.token();
                if (s.size() == 1 && s[0] == '{') { t.skip_to_close_brace(); break; }
                if (s.size() == 1 && s[0] == ';') break;
            }
            continue;
        }
        // Stray `{` or `}` — skip.
        if (tok.size() == 1 && tok[0] == '{') { t.skip_to_close_brace(); continue; }
        // anything else: ignore
    }

    return !table_.empty();
}

} // namespace stilus::wlw
