// examples/widget_demo.cpp — widget tree / layout demo
#include <cstdio>
#include <memory>
#include "stilus/gui.hpp"

static std::unique_ptr<stilus::Widget> build_controls_tab() {
    auto col = stilus::column();
    col->padding(14).gap(12).cross(stilus::CrossAlign::Stretch);

    // Buttons row
    auto btn_row = stilus::row();
    btn_row->gap(10).cross(stilus::CrossAlign::Stretch);

    auto b_primary = std::make_unique<stilus::Button>("Primary");
    b_primary->primary()
             .on_click([]{ std::puts("primary clicked"); std::fflush(stdout); });
    btn_row->add(std::move(b_primary), 1);

    auto b_default = std::make_unique<stilus::Button>("Default");
    b_default->on_click([]{ std::puts("default clicked"); std::fflush(stdout); });
    btn_row->add(std::move(b_default), 1);

    auto b_danger = std::make_unique<stilus::Button>("Danger");
    b_danger->on_click([]{ std::puts("danger clicked"); std::fflush(stdout); });
    btn_row->add(std::move(b_danger), 1);

    col->add(std::move(btn_row), 0);
    col->add(std::make_unique<stilus::Divider>(), 0);

    // Slider row
    auto slider_row = stilus::row();
    slider_row->gap(12).cross(stilus::CrossAlign::Center);
    slider_row->add(std::make_unique<stilus::Label>("Volume"), 0, 80);

    auto slider = std::make_unique<stilus::Slider>(0.f, 1.f, 0.35f);
    slider->on_change([](float v) {
        std::printf("volume=%.2f\n", v); std::fflush(stdout);
    });
    slider_row->add(std::move(slider), 1);
    slider_row->add(std::make_unique<stilus::Label>("0.35"), 0, 60);
    col->add(std::move(slider_row), 0);

    // CheckBoxes
    auto cb_row = stilus::row();
    cb_row->gap(16);
    auto cb1 = std::make_unique<stilus::CheckBox>("Autosave", true);
    auto cb2 = std::make_unique<stilus::CheckBox>("Notify",   false);
    auto cb3 = std::make_unique<stilus::CheckBox>("Verbose",  false);
    cb1->on_change([](bool v){ std::printf("autosave=%d\n", v); std::fflush(stdout); });
    cb2->on_change([](bool v){ std::printf("notify=%d\n",   v); std::fflush(stdout); });
    cb3->on_change([](bool v){ std::printf("verbose=%d\n",  v); std::fflush(stdout); });
    cb_row->add(std::move(cb1), 0);
    cb_row->add(std::move(cb2), 0);
    cb_row->add(std::move(cb3), 0);
    col->add(std::move(cb_row), 0);

    col->add(std::make_unique<stilus::Divider>(), 0);

    // Radio group inside a titled panel
    auto radios = std::make_unique<stilus::RadioGroup>(
        std::vector<std::string>{"Light", "Dark", "High contrast"});
    radios->set_value(1);
    radios->on_change([](int i){ std::printf("theme=%d\n", i); std::fflush(stdout); });

    auto panel = std::make_unique<stilus::Panel>();
    panel->title("Appearance").padding(12).child(std::move(radios));
    col->add(std::move(panel), 0);

    return col;
}

static std::unique_ptr<stilus::Widget> build_forms_tab() {
    auto col = stilus::column();
    col->padding(14).gap(12).cross(stilus::CrossAlign::Stretch);

    // ComboBox
    auto combo_row = stilus::row();
    combo_row->gap(12).cross(stilus::CrossAlign::Center);
    combo_row->add(std::make_unique<stilus::Label>("Language"), 0, 80);
    std::vector<stilus::ComboBox::Item> items = {
        {"English", 0}, {"日本語", 1}, {"Français", 2}, {"Deutsch", 3}
    };
    auto combo = std::make_unique<stilus::ComboBox>(std::move(items));
    combo->on_select([](int idx, const std::string& s) {
        std::printf("lang[%d]: %s\n", idx, s.c_str()); std::fflush(stdout);
    });
    combo_row->add(std::move(combo), 1);
    col->add(std::move(combo_row), 0);

    // Text input
    auto input_row = stilus::row();
    input_row->gap(12).cross(stilus::CrossAlign::Center);
    input_row->add(std::make_unique<stilus::Label>("Name"), 0, 80);
    auto input = std::make_unique<stilus::TextInput>("Your name...");
    input->on_submit([](const std::string& s) {
        std::printf("name=%s\n", s.c_str()); std::fflush(stdout);
    });
    input_row->add(std::move(input), 1);
    col->add(std::move(input_row), 0);

    auto div = std::make_unique<stilus::Divider>();
    div->inset(4);
    col->add(std::move(div), 0);

    // Progress bars with labels
    auto pb_col = stilus::column();
    pb_col->gap(6);
    pb_col->add(std::make_unique<stilus::Label>("Download"), 0);
    pb_col->add(std::make_unique<stilus::ProgressBar>(0.72f), 0);
    pb_col->add(std::make_unique<stilus::Label>("CPU"), 0);
    pb_col->add(std::make_unique<stilus::ProgressBar>(0.31f), 0);

    auto panel = std::make_unique<stilus::Panel>();
    panel->title("Status").padding(12).child(std::move(pb_col));
    col->add(std::move(panel), 0);

    col->add(std::make_unique<stilus::Spacer>(), 1);
    return col;
}

static std::unique_ptr<stilus::Widget> build_list_tab() {
    auto col = stilus::column();
    col->padding(14).gap(8).cross(stilus::CrossAlign::Stretch);
    col->add(std::make_unique<stilus::Label>("Scroll with mouse wheel."), 0);

    auto scroll = std::make_unique<stilus::ScrollView>();
    auto inner = stilus::column();
    inner->gap(6).cross(stilus::CrossAlign::Stretch);
    for (int i = 0; i < 24; i++) {
        auto lbl = std::make_unique<stilus::Label>("Item " + std::to_string(i));
        lbl->color(i % 3 == 0 ? stilus::Color::rgb(0x4a90d9)
                              : stilus::Color::rgb(0xcdd0d4));
        inner->add(std::move(lbl), 0);
    }
    scroll->add(std::move(inner));
    col->add(std::move(scroll), 1);
    return col;
}

int main() {
    stilus::Window win("widget-demo", 640, 560);
    if (!win.is_open()) return 1;

    auto& t = win.theme();
    t.font      = std::make_shared<stilus::Font>(stilus::Font::from_file(
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16.0f));
    t.font_bold = std::make_shared<stilus::Font>(stilus::Font::from_file(
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 16.0f));

    auto root = stilus::column();
    root->padding(16).gap(10).cross(stilus::CrossAlign::Stretch);

    auto title = std::make_unique<stilus::Label>("Widget demo");
    title->font_size(22).bold();
    root->add(std::move(title), 0);

    auto subtitle = std::make_unique<stilus::Label>(
        "Tabs · Panel · RadioGroup · Divider · ComboBox · ScrollView");
    subtitle->color(stilus::Color::rgb(0x9aa0a6));
    root->add(std::move(subtitle), 0);

    auto tabs = std::make_unique<stilus::Tabs>();
    tabs->tab("Controls", build_controls_tab())
         .tab("Forms",    build_forms_tab())
         .tab("List",     build_list_tab())
         .on_change([](int i){ std::printf("tab=%d\n", i); std::fflush(stdout); });
    root->add(std::move(tabs), 1);

    auto footer = std::make_unique<stilus::Label>(
        "ESC to quit · click tabs to switch");
    footer->color(stilus::Color::rgb(0x6a7078));
    root->add(std::move(footer), 0);

    win.set_root(std::move(root));

    win.on_event([&](const stilus::Event& e) {
        if (e.type == stilus::Event::Type::KeyDown &&
            e.key  == stilus::Key::Escape) win.close();
        if (e.type == stilus::Event::Type::Close) win.close();
        if (win.root()) win.root()->dispatch_event(e);
    });

    return stilus::App::instance().run();
}
