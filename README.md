# stilus

Linux向けのミニマルで完全静的リンク可能なC++20 GUIツールキットさ! GUI関連の外部ライブラリに一切依存しないZE☆Waylandバックエンドは`libwayland-client`を使わず自前でワイヤープロトコルを実装しており、X11バックエンドはベンダリングしたXCBを静的リンク!!。
デフォルトのビルド(`--cross-file cross-musl.txt`)はmuslで完全静的リンクなんで、libc含めて動的依存がないはず。カーネルのABIさえ合えば、どのディストロでも(たぶん)そのままコピーして動く。

## ステータス

開発中。APIはまだ安定してない。

## ドキュメント

- [**docs/gui.md**](docs/gui.md) — GUI アプリを書くためのガイド。ウィンドウ・ウィジェット一覧・レイアウト・イベント・テーマ・ハマりどころ。
- [**README_en.md**](README_en.md) — English version of this README

## 機能

バックエンド: Wayland(自前プロトコル実装、libwayland不使用)とX11(XCBをベンダリング+静的リンク)。`WAYLAND_DISPLAY`の有無で実行時に自動選択。
描画: ソフトウェアラスタライザ(XRGB8888)。矩形は解析的AA、角丸矩形/円はSDF、任意パスは8×垂直スーパーサンプリングのスキャンライン塗り。
変換: キャンバスの変換スタックで2Dアフィン変換(平行移動/スケール/回転/シア)をフルサポート。
クリップ: 矩形の高速パスに加え、8bitカバレッジマスクによる任意パスクリップ。
HiDPI: 整数倍のバッファスケーリング(`wl_surface.set_buffer_scale`/物理ピクセルバッファ)。フラクショナルスケールは未対応。
ウィンドウ装飾: コンポジタが対応していれば`xdg-decoration-v1`によるサーバーサイド装飾(KDE/KWin)。非対応(GNOME/Mutter等)の場合はクライアントサイド装飾(CSD)にフォールバックし、タイトルバー・最小化/最大化/閉じるボタンを自前描画。
テキスト: `stb_truetype`ベースのグリフ描画、フォントフォールバックチェーン(プライマリフォントに無いコードポイントを別フェース、例えばCJKフォントから補う)、`.ttc`のフェース選択、基本的な禁則処理(行頭/行末禁則)込みのソフト折返し。
IME: Wayland上で`zwp_text_input_v3`(preedit + commit)対応。
ウィジェット: `Canvas`のイミディエイトモードAPIの上に構築された、小規模なリテインドモードのウィジェットツリー(`src/widget.cpp`, `src/widgets.cpp`)。フォーカス/Tab順対応。

Linux専用として設計している。しばらくはwindowsやMacの正式対応の予定はない。
GPUレンダリングはめんどくさいのとか色々で実装してない。
アラビア語/ヘブライ語のテキストは対応する気はあるので暇ならやる。

## ビルド(デフォルト: musl完全静的)

```sh
export PATH="$PWD/tools:$PATH"
meson setup build-musl --cross-file cross-musl.txt --buildtype=release
meson compile -C build-musl
```

必要なもの: Meson、Ninja、そして[zig](https://ziglang.org/)(0.16.0で動作確認済み)。zigを`x86_64-linux-musl`ターゲットのC/C++クロスコンパイラとして使う(`tools/zig-musl-{cc,cxx,ar,ranlib}`が薄いラッパー)。XCBは`third_party/xcb-musl`にmusl向けを再ビルドしてベンダリング済みなので、`libXau`/`libXdmcp`を含めシステム側に何も要求しない。

### サンプル実行

```sh
./build-musl/hello              # 最小構成のウィンドウ
./build-musl/paint_demo         # プリミティブ+テキスト
./build-musl/path_demo          # 任意パス塗り
./build-musl/widget_demo        # リテインドモードのウィジェットツリー
./build-musl/transform_demo     # アフィン変換+パスクリップ+HiDPI
./build-musl/cjk_demo           # フォントフォールバック+CJK折返し
./build-musl/emoji_demo         # カラー絵文字(CBDT)レンダリング
```

### テスト

```sh
meson test -C build-musl --print-errorlogs    # 単体テスト+ゴールデンイメージ+フォント堅牢性テスト
./build-musl/test_x11_integration             # 実際にウィンドウを開く(要ディスプレイ、CIではxvfb-run経由)
```

- `test_unit`—window非依存の純粋ロジックのテスト(アフィン演算、パスのflatten、ダメージ領域の追跡、フォントの行折返し)。ディスプレイ接続不要。
- `test_golden`—決定的なプリミティブをオフスクリーンに描画し、`test/golden/*.ppm`の参照画像と比較。描画に意図的な変更を加えた後は`--regen`で参照を再生成(コミット前に必ず目視確認すること)。
- `test_font_robustness`—破損/切り詰めフォントに対する耐性テスト(stb_truetypeはバウンドチェックをしないため、`Font::from_memory`側での検証をピン留めする)。
- `test_x11_integration`—実際に利用可能なディスプレイに対してウィンドウを開く。CIでは`xvfb-run`経由で実行。

CI(`.github/workflows/ci.yml`)はUbuntu上でpush/PRごとに`musl-static`(このデフォルトビルド)・`build-and-test`(システムlibc)・`sanitizers`(ASan+UBSan)の3ジョブを実行する。

### 代替ビルド: システム libc(glibc)

zigを使いたくない場合や、`meson install`したライブラリを既存のglibcプログラムにリンクしたい場合は、こちらを使う(muslでビルドした`.a`はlibc内部ABIが違うためglibcプログラムへは直接リンクできない):

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

こちらもXCBは`third_party/xcb`にベンダリング済みで静的リンクされるが、libstdc++/libc/libmは動的リンクのまま(`-static-libstdc++ -static-libgcc`のみ付与)。システムから取得するのはXCBが推移的に必要とする`libXau`/`libXdmcp`(X11認証)のみ。

## インストール

他のglibcプログラムからリンクするライブラリとしてインストールする場合は、システムlibcビルド(`build`)を使う—muslビルドの`.a`はlibc内部ABIが異なるため、glibcプログラムへ直接リンクできない:

```sh
meson setup build --prefix=/usr/local
meson compile -C build
meson install -C build
```

`libstilus.a`、`stilus/`以下の公開ヘッダ、ベンダリングしたXCBの静的アーカイブ(システムの`libxcb`と衝突しないよう`<libdir>/stilus/`配下に設置)、そして`stilus.pc`(pkg-config)がインストールされる。

```sh
pkg-config --cflags --libs --static stilus
```

muslビルド(`build-musl`)は単体の完全静的バイナリを作って配布する用途向けで、`meson install`はせず生成された実行ファイルをそのままコピーして使え。

## ライセンス / 依存関係

stilus本体は[WTFPL](LICENSE)(Do What The Fuck You Want To Public License)v2。ベンダリングしているサードパーティコードは以下の通り:

- `third_party/stb/stb_truetype.h`, `stb_image.h`—Public Domain(MITとのデュアルライセンス、ファイルヘッダ参照)
- `third_party/xcb/`(glibc向け), `third_party/xcb-musl/`(musl向け)—[XCB](https://xcb.freedesktop.org/)、MIT/X11ライセンス
- muslビルドのlibc自体([musl libc](https://musl.libc.org/)、MITライセンス)は[zig](https://ziglang.org/)(Apache-2.0ライセンス)にバンドルされたものを使用。どちらもリポジトリには含まれない(ビルド時にツールチェーンとして参照するのみ)

Copyleft(GPL/LGPL/MPL等)の依存は使用しておらず、今後も追加しない方針。glibc静的リンク(LGPLでグレーな領域)は避け、muslビルドではlibc自体をMITライセンスのmuslに置き換えることでこれを回避している。

## 謝辞

stilusが依存しているサードパーティライブラリ、およびその開発者に感謝します。

### stb (Public Domain / MIT)

フォントラスタライズ(`stb_truetype.h`)と画像読み込み(`stb_image.h`)ライブラリ。

- **著者:** Sean Barrett (2009–2024)
- **URL:** http://nothings.org/stb/
- **ライセンス:** Public Domain (Unlicense) もしくは MIT License (いずれか選択)

### XCB — X C Binding (MIT)

X11 ウィンドウシステムの低レベルプロトコルライブラリ。静的アーカイブとしてベンダリング。

- **著者:** Bart Massey, Jamey Sharp, Josh Triplett (2001–2006)
- **URL:** https://xcb.freedesktop.org/
- **ライセンス:** MIT License

### musl libc (MIT)

musl 完全静的ビルドで使用される C 標準ライブラリ。zig ツールチェーンにバンドルされたものを使用。

- **著者:** Rich Felker, et al.
- **URL:** https://musl.libc.org/
- **ライセンス:** MIT License

### zig (Apache-2.0)

musl クロスコンパイルの C/C++ ツールチェーンとして使用。

- **著者:** Andrew Kelley
- **URL:** https://ziglang.org/
- **ライセンス:** Apache License, Version 2.0

### X.Org Foundation (MIT / X11 License)

libXau, libXdmcp (X11 認証ライブラリ) の著作権元。XCB の推移的依存として使用。

- **URL:** https://xorg.freedesktop.org/
- **ライセンス:** X11 License (MIT 互換)

    

## リポジトリ構成

```
include/stilus/    公開APIヘッダ
src/               実装
  render/          ソフトウェアラスタライザ+キャンバス
  text/            フォント読み込み、グリフキャッシュ、行折返し
  platform/
    wayland/       自前Waylandプロトコル実装
    x11/           XCBベースのバックエンド
  widget.cpp, widgets.cpp
                   リテインドモードのウィジェットツリー
third_party/       ベンダリングしたstb+XCB(glibc向け・musl向け、共に静的リンク)
tools/             zigをmuslクロスコンパイラとして呼び出す薄いラッパースクリプト
cross-musl.txt     musl完全静的ビルド用のMesonクロスファイル(デフォルトビルド)
examples/          実行可能なデモ
test/              単体テスト、ゴールデンイメージテスト、フォント堅牢性テスト、X11統合スモークテスト
```
