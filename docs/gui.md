# stilus GUI ガイド

C++ で stilus を使って GUI を書くための実用ドキュメント。API リファレンスと使い方の勘所を1本にまとめてある。ヘッダは `stilus/gui.hpp` 一枚で全て入る。

## 目次

1. [最小構成](#最小構成)
2. [ウィンドウとイベントループ](#ウィンドウとイベントループ)
3. [ウィジェットツリー](#ウィジェットツリー)
4. [レイアウト (Flex)](#レイアウト-flex)
5. [ウィジェット一覧](#ウィジェット一覧)
6. [イベント](#イベント)
7. [テーマとフォント](#テーマとフォント)
8. [アニメーション](#アニメーション)
9. [モーダル・ポップアップ・ダイアログ](#モーダルポップアップダイアログ)
10. [クリップボード](#クリップボード)
11. [座標系と HiDPI](#座標系と-hidpi)
12. [ハマりポイント集](#ハマりポイント集)

---

## 最小構成

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

`App::instance().run()` は全ての `Window` が閉じるまでブロックする。Window 一つの単純アプリならこれで十分。

ビルドは stilus 側の README「代替ビルド: システム libc (glibc)」を見て `libstilus.a` をインストールしてから `pkg-config --cflags --libs --static stilus` を使うか、あるいは stilus のチェックアウトを sibling ディレクトリに置いて直接ライブラリパスを指すのが手軽。

---

## ウィンドウとイベントループ

### `stilus::Window`

```cpp
Window(std::string_view title, int width, int height);
bool is_open() const;
void close();
void request_redraw();
int  width() const;                 // 論理ピクセル (HiDPI 非乗算)
int  height() const;
int  scale_factor() const;          // 現在の整数スケール
Theme& theme();                     // 描画に使う色/フォント
Widget* root();                     // set_root したもの
void set_root(std::unique_ptr<Widget>);
void on_frame(std::function<void(Canvas&)>);
void on_event(std::function<void(const Event&)>);
void request_animation_frame(std::function<void(float dt_sec)>);
```

### `stilus::App`

シングルトン。`App::instance().run()` がメインループ。プロセス内に複数 `Window` を持てるが、`App::run` を複数回呼ぶことはできない。

### 二種類の使い方

1. **イミディエイトモード**: `on_frame(...)` で毎フレーム自分で描く。ボードゲーム的な描画とか、独自 UI ならこちら。
2. **リテインドモード (ウィジェットツリー)**: `set_root(std::unique_ptr<Widget>)` で組んだツリーを渡す。フォームや業務アプリならこっち。

両方混ぜて使うこともできる (set_root した後で `on_event` を上書きした場合の挙動については [ハマりポイント集](#ハマりポイント集) を参照)。

---

## ウィジェットツリー

`Widget` を `unique_ptr` で親から所有する木構造。子は `Flex::add` などで所有権ごと渡す:

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

各ウィジェットのビルダーメソッド (`font_size(...)`, `bold()`, `on_click(...)` など) は `*this` を返すのでチェーンできるが、`std::make_unique` から入る場合は先に `unique_ptr` に代入してからメソッドを呼ぶのが素直。

---

## レイアウト (Flex)

stilus のレイアウトは Flexbox 相当の **Flex コンテナ** 一種類しかない。他のコンテナ (Panel, ScrollView, Tabs) も内部で Flex か、それに近い並べ方をしている。

### 生成

```cpp
auto row  = stilus::row();    // 横並び (Axis::Horizontal)
auto col  = stilus::column(); // 縦並び (Axis::Vertical)
// == std::make_unique<stilus::Flex>(Axis::Horizontal / Vertical)
```

### add 引数

```cpp
flex_ptr->add(std::unique_ptr<Widget> w,
              float flex  = 0,     // 0 = 固有サイズ、>0 = 残りを比率で分ける
              float fixed = -1);   // メイン軸のサイズを固定 (-1 = measure() を使う)
```

- **`flex=0` + `fixed=-1`**: ウィジェットの `measure()` が返す推奨サイズをそのまま使う。
- **`flex=1`**: 残りスペースを比率で分けて広げる。行/列内で1つだけ 1 にすれば右端 (下端) までフィルする。
- **`fixed=80`**: メイン軸を 80px 固定に。ラベル+入力欄の "ラベル列だけ80px にしたい" みたいなとき。
- **`flex=1` + `fixed>0`**: `fixed` が優先されて `flex` は無視される。

### プロパティ

```cpp
flex->gap(8);                           // 子同士の隙間
flex->padding(12);                      // コンテナ内側の余白
flex->cross(stilus::CrossAlign::Stretch // Start / Center / End / Stretch
);
```

`CrossAlign::Stretch` にすると、クロス軸方向で子が親いっぱいに広がる (テキスト入力欄を row の中で縦いっぱいに拡げるなど)。

### よくある構成

**ラベル + 入力の行:**
```cpp
auto r = stilus::row();
r->gap(10).cross(stilus::CrossAlign::Center);
r->add(std::make_unique<stilus::Label>("Name"), 0, 80);   // 80px 固定
r->add(std::make_unique<stilus::TextInput>(""),   1);      // 残りを埋める
```

**下段にボタンを寄せる (右寄せ):**
```cpp
auto btn_row = stilus::row();
btn_row->gap(8);
btn_row->add(std::make_unique<stilus::Spacer>(), 1);       // 左を全部食う
btn_row->add(std::make_unique<stilus::Button>("Cancel"), 0);
btn_row->add(std::make_unique<stilus::Button>("OK"),     0);
```

**2カラム:**
```cpp
auto grid = stilus::row();
grid->gap(12).cross(stilus::CrossAlign::Stretch);
grid->add(build_left_column(),  1);
grid->add(build_right_column(), 1);
```

---

## ウィジェット一覧

`stilus/widgets.hpp` に定義済みのもの。ビルダーメソッドはチェーン可能で、いずれも `*this` を返す。

### `Label`
```cpp
Label(std::string text);
Label& color(Color c);
Label& font_size(float px);
Label& bold(bool b = true);
void   text(std::string s);
```
折返し無し。改行 `\n` は入れられるが自動整形はしないので、長文は自前で分割する必要がある (`ui.cpp` の `wrap_text_naive` 参照)。

### `Button`
```cpp
Button(std::string label);
Button& on_click(std::function<void()>);
Button& primary(bool = true);   // 目立つ色に
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
TextInput();                             // プレースホルダなし
TextInput(std::string placeholder);
TextInput& set_text(std::string);
TextInput& on_change(std::function<void(const std::string&)>);
TextInput& on_submit(std::function<void(const std::string&)>);  // Enter 押下時
const std::string& text() const;
```
単一行のみ。IME (Wayland `zwp_text_input_v3`) 対応済み。

### `ComboBox`
```cpp
struct Item { std::string label; int index = -1; };

ComboBox(std::vector<Item> items);
ComboBox& on_select(std::function<void(int pos, const std::string& label)>);
ComboBox& select(int pos);           // 初期選択の設定 (on_select は呼ばれない)
ComboBox& set_items(std::vector<Item>);   // 一覧を差し替え (再スキャン後など)
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
ProgressBar& value(float v);   // 0..1 に clamp
float value() const;
```

### `RadioGroup`
```cpp
RadioGroup(std::vector<std::string> labels);   // 縦並び
RadioGroup& add(std::string label);
RadioGroup& on_change(std::function<void(int idx)>);
RadioGroup& set_value(int idx);   // これは on_change を発火する
int value() const;
```

### `Panel`
```cpp
Panel();
Panel& title(std::string);              // ボーダー上部にタイトル
Panel& padding(float);
Panel& child(std::unique_ptr<Widget>);  // 子は1つだけ
```
枠 + タイトル + 単一の子。中に Flex を入れて中身を組む。

### `Tabs`
```cpp
Tabs();
Tabs& tab(std::string label, std::unique_ptr<Widget> content);
Tabs& on_change(std::function<void(int)>);
Tabs& select(int);
int   selected() const;
```
複数タブを縦に切り替えるだけ。折り畳みタブや Reorderable などは無い。

### `ScrollView`
```cpp
ScrollView();
ScrollView& add(std::unique_ptr<Widget>);   // 内部の Flex(Vertical) に足される
```
縦スクロールのみ。マウスホイールとスクロールバードラッグに対応。中身の高さは自動計算。

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
Flex の中で `flex=1` と組み合わせて空きを確保するプレースホルダ。

### `Image`
```cpp
Image(PixelImage img);   // RGBA、ユーザ側で PixelImage を用意
```
PNG 読み込みなどは stilus は提供していないので、自前で `PixelImage` を作って渡す。

### `Container`
```cpp
Container();
Container& add(std::unique_ptr<Widget>);
```
Flex ではない汎用コンテナ。子を親の rect と同じ範囲に重ねて置く (Overlay 的な使い方向け)。

### `Flex` (直接)
```cpp
Flex(Axis axis);
Flex& add(std::unique_ptr<Widget>, float flex = 0, float fixed = -1);
Flex& gap(float);
Flex& padding(float);
Flex& cross(CrossAlign);
```
通常は `row()` / `column()` ヘルパー経由。

---

## イベント

### `Event` 構造体

```cpp
struct Event {
    enum class Type {
        None,
        MouseMove, MouseDown, MouseUp, MouseWheel,
        KeyDown, KeyUp, TextInput, Preedit,
        Resize, Close, Focus, Unfocus,
    };
    Type type;
    float x, y;                                // マウス座標 (論理ピクセル)
    MouseButton button;
    float wheel_dx, wheel_dy;
    Key key;
    KeyMods mods;
    uint32_t codepoint;                        // TextInput 時
    std::string text;                          // 単キー or IME コミット
    int preedit_cursor_begin, preedit_cursor_end;
    int width, height;                         // Resize 時
};
```

キー定数は `stilus::Key::A`, `Escape`, `Enter`, `Tab`, `Left/Right/Up/Down`, `PageUp/Down`, `Home/End`, `Shift/Ctrl/Alt/Super` などが `event.hpp` に定義済み。

### ハンドラの2種類

**ウィジェット継承の場合:**
```cpp
class MyWidget : public stilus::Widget {
    bool on_event(const stilus::Event& e) override {
        // 消費したら true。false を返すと親コンテナに委譲される。
        return handled;
    }
};
```

**Window 直接の場合:**
```cpp
win.on_event([&](const stilus::Event& e) {
    if (e.type == stilus::Event::Type::Close) win.close();
    // Widget ツリーにも配りたいなら、明示的に再ディスパッチする:
    if (win.root()) win.root()->dispatch_event(e);
});
```

### イベントディスパッチの流れ

1. Wayland/X11 バックエンドが `Window::on_event` に登録されたコールバックへ配送。
2. `Window::set_root` が有効な場合、内部で「ウィジェットツリーへ `dispatch_event` する」コールバックが自動登録される。
3. **`win.on_event(...)` を呼ぶと、この自動登録が上書きされる (チェーンではなく置換)。**
4. コンテナ (`Flex`, `Panel`, etc.) の `dispatch_event` が子へルーティング:
   - **MouseDown/MouseWheel**: マウス位置を含む最上位の子だけに配る。
   - **MouseMove/MouseUp**: すべての子に broadcast (hover 状態のクリアのため)。
   - **`wants_capture()` を返す子**: 位置に関わらず優先的にイベントを受け取る (ComboBox のドロップダウンなど)。
5. `on_event` が `true` を返すとルーティング終了。

### フォーカスと Tab キー

`Tab` は自動でフォーカスを次の focusable ウィジェットに移す。個別ウィジェットで `focusable() const override` を `true` にすると参加できる (デフォルトの `Widget` は false)。既存ウィジェットは `Button`, `TextInput`, `CheckBox`, `Slider`, `ComboBox`, `RadioGroup` が focusable。

`Shift+Tab` で逆順、`Enter/Space` で focused widget を活性化 (Button クリック、ComboBox 展開)。

---

## テーマとフォント

`Theme` 構造体は色と Font へのポインタを持つ:

```cpp
struct Theme {
    Color bg, surface, surface_hi, border, primary, accent, text, text_dim;
    float radius, padding, gap;
    std::shared_ptr<Font> font, font_bold;
};
```

初期化:
```cpp
win.theme().font = std::make_shared<stilus::Font>(stilus::Font::from_file(
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 15.0f));
win.theme().font_bold = std::make_shared<stilus::Font>(stilus::Font::from_file(
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 15.0f));
```

`font` が `null` だと文字が描画されないので必ずセットする。太字が要らないなら `font` と同じものを `font_bold` にも入れておけばよい。

### CJK / カラー絵文字のフォールバック

```cpp
auto font = stilus::Font::from_file("...DejaVuSans.ttf", 15.f);
auto cjk  = stilus::Font::from_file(
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 15.f, 0);
if (cjk.valid()) font.add_fallback(std::move(cjk));
auto emoji = stilus::Font::from_file(
    "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf", 15.f);
if (emoji.valid()) font.add_fallback(std::move(emoji));
```

`add_fallback` 順に、プライマリに無いコードポイントを次のフォントから引く。カラー絵文字は CBDT テーブルから RGBA 貼り付け。

### 色 (`Color`)

```cpp
Color::rgb(0xff8040);        // アルファ 1.0
Color::rgba(0xff804080);     // AARRGGBB は逆で 0xAABBGGRR ではない — 上位バイトが A
Color c { 1.f, 0.5f, 0.25f, 1.f };  // r, g, b, a すべて 0..1
```

---

## アニメーション

「次のフレームで呼んでほしい」ワンショットタイマ。連続アニメーションは自分で再要求する:

```cpp
std::function<void(float)> tick;
tick = [&](float dt /* seconds */) {
    // dt 分状態を進める
    my_pos_x += velocity_x * dt;
    if (moving) win.request_animation_frame(tick);
};
win.request_animation_frame(tick);
```

`request_animation_frame` は同時に redraw も要求するので、`tick` が状態を進めたあと `paint` が続けて呼ばれる。

---

## モーダル・ポップアップ・ダイアログ

**stilus はモーダルダイアログ・ファイルピッカー・アラート等を組み込みで持たない。** 必要ならウィジェットツリー上で自前実装する:

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
            c.fill_rect(rect_, Color{0,0,0,0.55f});   // 暗転
            modal_->paint(c, t);
            modal_->paint_overlay(c, t);
        }
    }
    bool dispatch_event(const Event& e) override {
        if (modal_) { modal_->dispatch_event(e); return true; }  // 下は触らせない
        return form_->dispatch_event(e);
    }
    // measure / layout / hit / child_count / child も忘れずに
};
```

`rufus-linux/src/ui.cpp` の `AppRoot` が完全な実装例。

### `Popup` クラス (別ウィンドウ)

`stilus::Popup` は Wayland `xdg_popup` (と X11 override-redirect) を薄くラップしたクラス。**別サーフェス**として親ウィンドウの上に浮かべる (右クリックメニュー的な使い方):

```cpp
stilus::Popup pop(win, /*anchor rect=*/{100, 200, 20, 20}, /*w=*/200, /*h=*/120);
pop.on_frame([](stilus::Canvas& c) { /* 独自描画 */ });
pop.on_event(...);
```

同じ Window 内のダイアログ用ではないので、フォームの上に載せる暗転オーバレイのようなものはウィジェットツリー側でやる方が素直。

---

## クリップボード

```cpp
stilus::App::instance().clipboard_set_text("hello");
std::string got = stilus::App::instance().clipboard_get_text();
```

Wayland `wl_data_device` と X11 `CLIPBOARD` selection (INCR プロトコル対応) の両方が実装済み。`get_text` は同期 API だが、内部でイベントループを短時間だけ回して選択所有者からのレスポンスを待つ。

---

## キーボード配列と IME (Wayland)

### 配列

コンポジタが送ってくる XKB キーマップテキストを自前でパースする (`src/platform/wayland/xkb_parse.cpp`)。JIS (日本語) は `yen` / `overline` / `voicedsound` / `kana_*` などを含めて対応済み。他の配列 (DE/FR/DVORAK 等) も、そのキーマップ内で使われている keysym 名がテーブルに載っていれば動く。未知の keysym は `UXXXX` 直値形式で救われる。パース失敗時のみ US ハードコード表にフォールバックする。

なお内部的には `wl_proto.cpp` の Display に `register_fd_message(object, opcode)` があり、`wl_keyboard.keymap` はそこで自動登録される。SCM_RIGHTS で来る fd を無関係なイベントに横取りされないよう、fd を要求する既知の (object, opcode) にだけ配る仕組み。自分でプロトコル拡張を叩く場合、その fd を受け取るイベントには同じ登録が必要。

### IME

`zwp_text_input_v3` に対応済み。`TextInput` ウィジェットがフォーカスを得ると自動で IME を enable し、失うと disable する。Preedit テキストは `text_dim` 色で描画され、コミットで本文に挿入される。

自分でカスタム text-input widget を書く場合は、`Widget::set_focused` をオーバーライドして状態が変わったら `notify_ime(true|false)` を呼ぶ:

```cpp
void set_focused(bool v) override {
    bool was = focused();
    Widget::set_focused(v);
    if (focused() != was) notify_ime(focused());
}
```

`notify_ime` は `invalidate` と同じく親をたどってウィンドウの `set_ime_enabled` に伝播する。

X11 バックエンドでは IME (XIM/IBus) は未対応。

---

## 座標系と HiDPI

- ウィジェットに渡る座標 (rect, event.x/y, Canvas API) は全て **論理ピクセル**。
- HiDPI ディスプレイでは stilus が自動で物理ピクセル倍のバッファを確保し、`Canvas` に scale 変換をかける。ユーザコードは倍率を気にしなくてよい。
- 現在のスケール値は `win.scale_factor()` (整数)。fractional scale (`wp_fractional_scale_v1`) にも対応済み。
- ウィジェットの `rect_` はウィンドウ左上原点の絶対座標 (親 rect を減算する必要なし)。ヒットテストや hover 判定はこの座標で `rect_.contains({e.x, e.y})` すれば OK。

---

## ハマりポイント集

### 1. `set_root()` の後に `on_event()` を呼ぶと自動ディスパッチが消える

`Window::set_root` は内部で `impl_->set_event_cb([root]...)` を登録する。その後で `win.on_event(...)` を呼ぶと **上書き** で、ウィジェットツリーへのディスパッチが消える。両方欲しいなら手で再ディスパッチ:

```cpp
win.set_root(std::move(root));
win.on_event([&](const stilus::Event& e) {
    if (e.type == stilus::Event::Type::Close) { win.close(); return; }
    if (e.type == stilus::Event::Type::KeyDown && e.key == stilus::Key::Escape)
        win.close();
    if (win.root()) win.root()->dispatch_event(e);   // ← これが要る
});
```

`examples/widget_demo.cpp` と `rufus-linux/src/ui.cpp` の両方でこの形。

### 2. Font がない (`Theme::font == nullptr`)

`Label`, `Button`, `CheckBox`, `TextInput`, `ComboBox`, `RadioGroup`, `Panel` タイトル、`Tabs` ラベル — 全てのテキスト描画は `Theme::font` が有効でないと表示されない (エラーは出ない、単に何も出ない)。Window 作成直後に必ずセット。

### 3. `wants_capture()` は再帰する

以前は `Flex::wants_capture()` が `false` 固定で、ネストされた `ComboBox` (`row` の中の `column` の中の `row` の中の ComboBox など) を開くとドロップダウンの選択肢クリックが親コンテナに握りつぶされていた。現在は `Flex` が子孫に問い合わせて伝播するよう修正済み。**自作コンテナ** を書く場合も同様のパターンで実装すること:

```cpp
bool wants_capture() const override {
    for (size_t i = 0; i < child_count(); ++i)
        if (child(i)->wants_capture()) return true;
    return false;
}
```

### 4. `dispatch_event` を素通しさせるかどうか

コンテナは自作するとき `dispatch_event` を「まず子に配って、消費されなければ自分の `on_event`」の順で書く。ただしマウスイベントは位置チェックが要る (親の外にはみ出た子に配ってはいけない)。単純なパススルーなら:

```cpp
bool dispatch_event(const Event& e) override {
    for (size_t i = child_count(); i-- > 0;) {
        if (child(i)->dispatch_event(e)) return true;
    }
    return on_event(e);
}
```

複雑な例は `Flex::dispatch_event` (`src/widget.cpp`) を読むと分かる。

### 5. ウィジェット生成順とビルダーメソッド

```cpp
auto btn = std::make_unique<stilus::Button>("OK");
btn->primary().on_click([]{ ... });   // OK、unique_ptr 経由
```

は動くが、

```cpp
auto btn = std::make_unique<stilus::Button>("OK").primary();
// エラー: unique_ptr にはメソッド primary() が無い
```

は動かない (`primary()` は Widget のメソッド)。一度 unique_ptr に受けてから `btn->` で辿ること。

### 6. `Container` と `Flex` の違い

`Container` は **子を重ねる** (絶対配置的)。並べたいなら `Flex` (`row()` / `column()`) を使う。名前が紛らわしいが用途が違う。

### 7. `Label` は折返さない

長文はユーザ側で改行するか、行分割ヘルパを噛ませる。`Label` に max_width の概念がない。

### 8. TextInput は数値バリデーションしない

`TextInput` に整数入力させたい場合は、`on_change` で自前検証するか、確定時 (`on_submit`) に parse する。SpinButton 相当のウィジェットは無い。

### 9. パフォーマンス

ソフトウェアラスタライザなので、フル画面再描画コストはそれなり (4K で 5-10ms 程度)。頻繁に更新するものは `invalidate(rect)` で部分再描画するか、`request_animation_frame` の周期を控えめに。

---

## 参考: 完動する例

- **`examples/widget_demo.cpp`**: Tabs / Panel / RadioGroup / ScrollView / ComboBox / CheckBox / Slider / TextInput / Button の全部盛り。
- **`examples/anim_demo.cpp`**: `request_animation_frame` を使った跳ねる円。
- **`examples/clipboard_demo.cpp`**: Ctrl+C / Ctrl+V でのクリップボード連携。
- **`examples/emoji_demo.cpp`**: フォントフォールバック + カラー絵文字。
- **`examples/transform_demo.cpp`**: アフィン変換と任意パスクリップ (低レベル Canvas API)。
- **`rufus-linux/`** (sibling リポジトリ): 実用アプリの例。フォーム主体で、自前 AppRoot によるモーダル・ファイルピッカー・確認ダイアログの実装、`std::thread` バックグラウンドジョブの UI 側統合 (`request_animation_frame` ポーリングパターン) を含む。
