# stilus

Linux 向けのミニマルで完全静的リンク可能な C++20 GUI ツールキット。GUI 関連の外部ライブラリに一切依存しない — Wayland バックエンドは `libwayland-client` を使わず自前でワイヤープロトコルを実装しており、X11 バックエンドはベンダリングした XCB を静的リンクしている。生成されるバイナリは GUI 関連の実行時依存を持たない。

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

## ビルド

```sh
meson setup build
meson compile -C build
```

C++20 対応コンパイラ、Meson、Ninja が必要。`pkg-config` で検出するような GUI 系の外部システムライブラリは不要 — XCB は `third_party/xcb` にベンダリングして静的リンク済み。システムから取得するのは XCB が推移的に必要とする `libXau`/`libXdmcp` (X11 認証) のみ。

### サンプル実行

```sh
./build/hello              # 最小構成のウィンドウ
./build/paint_demo         # プリミティブ + テキスト
./build/path_demo          # 任意パス塗り
./build/widget_demo        # リテインドモードのウィジェットツリー
./build/transform_demo     # アフィン変換 + パスクリップ + HiDPI
./build/cjk_demo           # フォントフォールバック + CJK 折返し
```

### テスト

```sh
meson compile -C build
meson test -C build --print-errorlogs        # 単体テスト + ゴールデンイメージテスト
./build/test_x11_integration                 # 実際にウィンドウを開く (要ディスプレイ、CIではxvfb-run経由)
```

- `test_unit` — window非依存の純粋ロジックのテスト (アフィン演算、パスのflatten、ダメージ領域の追跡、フォントの行折返し)。ディスプレイ接続不要。
- `test_golden` — 決定的なプリミティブをオフスクリーンに描画し、`test/golden/*.ppm` の参照画像と比較。描画に意図的な変更を加えた後は `--regen` で参照を再生成 (コミット前に必ず目視確認すること)。
- `test_x11_integration` — 実際に利用可能なディスプレイに対してウィンドウを開く。CI では `xvfb-run` 経由で実行。

CI (`.github/workflows/ci.yml`) は Ubuntu 上で push/PR ごとに上記すべてを実行する。

## インストール

```sh
meson setup build --prefix=/usr/local
meson compile -C build
meson install -C build
```

`libstilus.a`、`stilus/` 以下の公開ヘッダ、ベンダリングした XCB の静的アーカイブ (システムの `libxcb` と衝突しないよう `<libdir>/stilus/` 配下に設置)、そして `stilus.pc` (pkg-config) がインストールされる。

```sh
pkg-config --cflags --libs --static stilus
```

## ライセンス / 依存関係

stilus 本体は [WTFPL](LICENSE) (Do What The Fuck You Want To Public License) v2。ベンダリングしているサードパーティコードは以下の通り:

- `third_party/stb/stb_truetype.h` — Public Domain (MIT とのデュアルライセンス、ファイルヘッダ参照)
- `third_party/xcb/` — [XCB](https://xcb.freedesktop.org/)、MIT/X11 ライセンス

Copyleft (GPL/LGPL/MPL 等) の依存は使用しておらず、今後も追加しない方針。

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
third_party/       ベンダリングした stb + XCB (静的リンク)
examples/          実行可能なデモ
test/              単体テスト、ゴールデンイメージテスト、X11統合スモークテスト
```
