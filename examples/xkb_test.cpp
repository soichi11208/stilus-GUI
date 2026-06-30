// examples/xkb_test.cpp — sanity check for the XKB parser
#include <cstdio>
#include <string_view>
#include "../src/platform/wayland/xkb_parse.hpp"

static const char kJpKeymap[] =
    "// simulated compositor keymap dump\n"
    "xkb_keymap {\n"
    "xkb_keycodes \"evdev+aliases(qwerty)\" {\n"
    "  minimum = 8;\n"
    "  maximum = 255;\n"
    "  <AE01> = 10;\n"
    "  <AE02> = 11;\n"
    "  <AE13> = 132;\n"
    "  <AC01> = 38;\n"
    "  <AD11> = 34;\n"
    "  indicator 1 = \"Caps Lock\";\n"
    "};\n"
    "xkb_types \"complete\" {\n"
    "  virtual_modifiers NumLock,Alt,LevelThree;\n"
    "  type \"ONE_LEVEL\" { modifiers= none; };\n"
    "};\n"
    "xkb_compatibility \"complete\" {\n"
    "  virtual_modifiers NumLock;\n"
    "  interpret Any+AnyOf(all) { action= SetMods(modifiers=modMapMods); };\n"
    "};\n"
    "xkb_symbols \"pc+jp+inet(evdev)\" {\n"
    "  name[group1]=\"Japanese\";\n"
    "  key <AE01> { type[group1]=\"FOUR_LEVEL\", [ 1, exclam, onesuperior, exclamdown ] };\n"
    "  key <AE02> { [ 2, quotedbl ], [ NoSymbol, NoSymbol ] };\n"
    "  key <AE13> { symbols[group1] = [ yen, bar ] };\n"
    "  key <AC01> { [ a, A ] };\n"
    "  key <AD11> { [ at, grave ] };\n"
    "  modifier_map Shift  { <LFSH>, <RTSH> };\n"
    "  modifier_map Lock   { <CAPS> };\n"
    "  modifier_map Mod5   { <LVL3> };\n"
    "};\n"
    "};\n";

int main() {
    stilus::wlw::XkbKeymap km;
    if (!km.parse(kJpKeymap, sizeof(kJpKeymap) - 1)) {
        std::puts("parse failed"); return 1;
    }
    struct Case { uint32_t kernel_kc; bool sh; uint32_t want; const char* why; };
    Case cases[] = {
        {10, false, '1',    "AE01 base -> 1"},
        {10, true,  '!',    "AE01 shift -> !"},
        {11, true,  '"',    "AE02 shift -> quotedbl"},
        {132,false, 0x00A5, "AE13 base -> yen (U+00A5)"},
        {132,true,  '|',    "AE13 shift -> bar"},
        {38, false, 'a',    "AC01 base -> a"},
        {38, true,  'A',    "AC01 shift -> A"},
        {34, false, '@',    "AD11 base -> at (JP)"},
        {34, true,  '`',    "AD11 shift -> grave (JP)"},
    };
    int fails = 0;
    for (auto& c : cases) {
        uint32_t ev = c.kernel_kc - 8;
        uint32_t got = km.codepoint(ev, c.sh, false, false);
        bool ok = (got == c.want);
        std::printf("%s  kc=%u shift=%d  got=U+%04X want=U+%04X  %s\n",
                    ok ? "PASS" : "FAIL",
                    c.kernel_kc, (int)c.sh, got, c.want, c.why);
        if (!ok) ++fails;
    }
    return fails ? 2 : 0;
}
