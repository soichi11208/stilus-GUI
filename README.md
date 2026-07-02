# stilus

Linux 向けのミニマルで完全静的リンク可能な C++20 GUI ツールキット。GUI 関連の外部ライブラリに一切依存しない — Wayland バックエンドは `libwayland-client` を使わず自前でワイヤープロトコルを実装しており、X11 バックエンドはベンダリングした XCB を静的リンクしている。

デフォルトのビルド (`--cross-file cross-musl.txt`) は **musl 上で完全静的リンク**され、生成されるバイナリは `ldd` が `not a dynamic executable` と返す — libc すら含めて動的依存が一切ない。カーネルの ABI さえ合えば、どの Linux ディストロでもそのままコピーして動く。

## ステータス

開発中。API はまだ安定していない。

## 機能

- **バックエンド**: Wayland (自前プロトコル実装、libwayland 不使用) と X11 (XCB をベンダリング + 静的リンク)。`WAYLAND_DISPLAY` の有無で実行時に自動選択。
- **描画**: ソフトウェアラスタライザ (XRGB8888)。矩形は解析的 AA、角丸矩形/円は SDF、任意パスは 8× 垂直スーパーサンプリングのスキャンライン塗り。
- **変換**: キャンバスの変換スタックで 2D アフィン変換 (平行移動/スケール/回転/シア) をフルサポート。
- **クリップ**: 矩形の高速パスに加え、8bit カバレッジマスクによる任意パスクリップ。
- **HiDPI**: 整数倍のバッファスケーリング (`wl_surface.set_buffer_scale` / 物理ピクセルバッファ)。フラクショナルスケールは未対応。
- **ウィンドウ装飾**: コンポジタが対応していれば `xdg-decoration-v1` によるサーバーサイド装飾 (KDE/KWin)。非対応 (GNOME/Mutter 等) の場合はクライアントサイド装飾 (CSD) にフォールバックし、タイトルバー・最小化/最大化/閉じるボタンを自前描画。
- **テキスト**: `stb_truetype` ベースのグリフ描画、フォントフォールバックチェーン (プライマリフォントに無いコードポイントを別フェース、例えば CJK フォントから補う)、`.ttc` のフェース選択、基本的な禁則処理 (行頭/行末禁則) 込みのソフト折返し。
- **IME**: Wayland 上で `zwp_text_input_v3` (preedit + commit) 対応。
- **ウィジェット**: `Canvas` のイミディエイトモード API の上に構築された、小規模なリテインドモードのウィジェットツリー (`src/widget.cpp`, `src/widgets.cpp`)。フォーカス/Tab 順対応。

## 非対応方針 (当面)

- Windows / macOS。Linux 専用として設計している。
- GPU レンダリング。すべて CPU ラスタライズ。
- 完全なテキスト整形 / BiDi (複雑な合字・右から左レイアウト)。CJK の行折返しは対応済みだが、アラビア語/ヘブライ語は非対応。

## ビルド (デフォルト: musl 完全静的)

```sh
export PATH="$PWD/tools:$PATH"
meson setup build-musl --cross-file cross-musl.txt --buildtype=release
meson compile -C build-musl
```

必要なもの: Meson、Ninja、そして [zig](https://ziglang.org/) (0.16.0 で動作確認済み)。zig を `x86_64-linux-musl` ターゲットの C/C++ クロスコンパイラとして使う (`tools/zig-musl-{cc,cxx,ar,ranlib}` が薄いラッパー)。XCB は `third_party/xcb-musl` に musl 向けを再ビルドしてベンダリング済みなので、`libXau`/`libXdmcp` を含めシステム側に何も要求しない。

```sh
ldd build-musl/widget_demo
# => not a dynamic executable
```

### サンプル実行

```sh
./build-musl/hello              # 最小構成のウィンドウ
./build-musl/paint_demo         # プリミティブ + テキスト
./build-musl/path_demo          # 任意パス塗り
./build-musl/widget_demo        # リテインドモードのウィジェットツリー
./build-musl/transform_demo     # アフィン変換 + パスクリップ + HiDPI
./build-musl/cjk_demo           # フォントフォールバック + CJK 折返し
./build-musl/emoji_demo         # カラー絵文字 (CBDT) レンダリング
```

### テスト

```sh
meson test -C build-musl --print-errorlogs    # 単体テスト + ゴールデンイメージ + フォント堅牢性テスト
./build-musl/test_x11_integration             # 実際にウィンドウを開く (要ディスプレイ、CIではxvfb-run経由)
```

- `test_unit` — window非依存の純粋ロジックのテスト (アフィン演算、パスのflatten、ダメージ領域の追跡、フォントの行折返し)。ディスプレイ接続不要。
- `test_golden` — 決定的なプリミティブをオフスクリーンに描画し、`test/golden/*.ppm` の参照画像と比較。描画に意図的な変更を加えた後は `--regen` で参照を再生成 (コミット前に必ず目視確認すること)。
- `test_font_robustness` — 破損/切り詰めフォントに対する耐性テスト (stb_truetype はバウンドチェックをしないため、`Font::from_memory` 側での検証をピン留めする)。
- `test_x11_integration` — 実際に利用可能なディスプレイに対してウィンドウを開く。CI では `xvfb-run` 経由で実行。

CI (`.github/workflows/ci.yml`) は Ubuntu 上で push/PR ごとに `musl-static` (このデフォルトビルド)・`build-and-test` (システム libc)・`sanitizers` (ASan+UBSan) の3ジョブを実行する。

### 代替ビルド: システム libc (glibc)

zig を使いたくない場合や、`meson install` したライブラリを既存の glibc プログラムにリンクしたい場合は、こちらを使う (musl でビルドした `.a` は libc 内部 ABI が違うため glibc プログラムへは直接リンクできない):

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

こちらも XCB は `third_party/xcb` にベンダリング済みで静的リンクされるが、libstdc++ / libc / libm は動的リンクのまま (`-static-libstdc++ -static-libgcc` のみ付与)。システムから取得するのは XCB が推移的に必要とする `libXau`/`libXdmcp` (X11 認証) のみ。

## インストール

他の glibc プログラムからリンクするライブラリとしてインストールする場合は、システム libc ビルド (`build`) を使う — musl ビルドの `.a` は libc 内部 ABI が異なるため、glibc プログラムへ直接リンクできない:

```sh
meson setup build --prefix=/usr/local
meson compile -C build
meson install -C build
```

`libstilus.a`、`stilus/` 以下の公開ヘッダ、ベンダリングした XCB の静的アーカイブ (システムの `libxcb` と衝突しないよう `<libdir>/stilus/` 配下に設置)、そして `stilus.pc` (pkg-config) がインストールされる。

```sh
pkg-config --cflags --libs --static stilus
```

musl ビルド (`build-musl`) は単体の完全静的バイナリを作って配布する用途向けで、`meson install` はせず生成された実行ファイルをそのままコピーして使う。

## ライセンス / 依存関係

stilus 本体は [WTFPL](LICENSE) (Do What The Fuck You Want To Public License) v2。ベンダリングしているサードパーティコードは以下の通り:

- `third_party/stb/stb_truetype.h`, `stb_image.h` — Public Domain (MIT とのデュアルライセンス、ファイルヘッダ参照)
- `third_party/xcb/` (glibc向け), `third_party/xcb-musl/` (musl向け) — [XCB](https://xcb.freedesktop.org/)、MIT/X11 ライセンス
- musl ビルドの libc 自体 ([musl libc](https://musl.libc.org/)、MIT ライセンス) は [zig](https://ziglang.org/) (Apache-2.0 ライセンス) にバンドルされたものを使用。どちらもリポジトリには含まれない (ビルド時にツールチェーンとして参照するのみ)

Copyleft (GPL/LGPL/MPL 等) の依存は使用しておらず、今後も追加しない方針。glibc 静的リンク (LGPL でグレーな領域) は避け、musl ビルドでは libc 自体を MIT ライセンスの musl に置き換えることでこれを回避している。

## リポジトリ構成

```
include/stilus/    公開API ヘッダ
src/               実装
  render/          ソフトウェアラスタライザ + キャンバス
  text/            フォント読み込み、グリフキャッシュ、行折返し
  platform/
    wayland/       自前 Wayland プロトコル実装
    x11/           XCB ベースのバックエンド
  widget.cpp, widgets.cpp
                   リテインドモードのウィジェットツリー
third_party/       ベンダリングした stb + XCB (glibc向け・musl向け、共に静的リンク)
tools/             zig を musl クロスコンパイラとして呼び出す薄いラッパースクリプト
cross-musl.txt     musl 完全静的ビルド用の Meson クロスファイル (デフォルトビルド)
examples/          実行可能なデモ
test/              単体テスト、ゴールデンイメージテスト、フォント堅牢性テスト、X11統合スモークテスト
```
