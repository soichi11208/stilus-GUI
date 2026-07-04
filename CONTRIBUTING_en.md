# Contributing Guide

Thank you for your interest in contributing to stilus!
Please take a quick look at the following guidelines before you start.

## Design Philosophy

stilus is designed based on the following principles:

- **Eliminating Dependencies**: We aim to operate independently of any specific distribution by minimizing library dependencies and preferring vendoring or custom implementations (e.g., custom Wayland protocol) whenever possible.
- **Full Static Linking**: By combining musl libc and the Zig toolchain, we produce binaries that require no dynamic libraries at runtime.
- **Simplicity**: Rather than being a heavyweight framework, stilus focuses on providing low-level rendering capabilities and a lightweight widget tree.
- **Linux Only**: For the time being, there are no plans to support Windows or macOS (though it's not that we don't want to).

## Development Setup

The development environment is, naturally, assumed to be Linux.

While I use Debian 13, any distribution should work.

### Required Tools

- **Zig (v0.16.0 or later)**: Used as the cross-compiler for musl.
  - Download the binary from [ziglang.org](https://ziglang.org/download/) and add it to your PATH.
- **Meson**: Build definition system.
  - Install via `pip install meson` or your OS package manager (e.g., `apt install meson`).
- **Ninja**: High-speed build tool.
  - Install via `apt install ninja-build` etc.

If you are using a non-Debian distribution... well, Google it.

### Build Instructions

The default assumption is a full static build using musl.

```sh
export PATH="$PWD/tools:$PATH"
meson setup build-musl --cross-file cross-musl.txt --buildtype=release
meson compile -C build-musl
```

## Testing and Verification

### Running Tests

```sh
meson test -C build-musl --print-errorlogs
```

*Note: `test_x11_integration` requires a physical display. In CI or headless environments, please run it via `xvfb-run`.*

### Golden Image Tests

If you modify the rendering logic, you must update the reference images (`test/golden/*.ppm`).

1. Run the tests with the `--regen` flag to regenerate the images.
2. **Important**: Always visually verify that the regenerated images are correct.
3. After verification, include the updated images in your commit.

## Contribution Rules

### Don't Break the Build

**Do not push code or open pull requests in a state where the build fails or existing tests are broken.**
After making changes, always ensure that `meson compile` and `meson test` complete successfully.

### Adding Dependencies

- **Do not introduce Copyleft (GPL/LGPL/MPL, etc.) libraries as a general rule.**
- Only libraries with permissive licenses (MIT, BSD, Apache-2.0, Public Domain, etc.) are accepted.

## Coding Guidelines

The following style is recommended:

- **Language Standard**: C++20.
- **Naming Conventions**:
  - **Classes/Structs**: `PascalCase` (e.g., `Window`, `WidgetTree`)
  - **Functions/Methods**: `snake_case` (e.g., `request_redraw()`, `set_root()`)
  - **Member Variables**: `snake_case_` (ending with an underscore. e.g., `impl_`, `root_`)
  - **Local Variables**: `snake_case` (e.g., `cur`, `next`)
- **Formatting**:
  - Indentation: 4 spaces.
  - Braces `{` should be placed at the end of the line (K&R / Egyptian style).
- **Design Patterns**:
  - Use the `detail` namespace or the Pimpl pattern (`Impl` struct) to hide internal implementations.

## Vibe Coding

- It is not prohibited; I use it myself.
- However, I repeat: do not leave the build in a broken state.
- Using settings that allow your code to be used as training data is fine.
- That said, please avoid cases where the resulting code is dysfunctional due to outdated/insufficient training data or fails to adhere to the coding guidelines. If that happens, the responsibility lies with you—not Anthropic, OpenAI, MoonshotAI, Zai, Alibaba, or DeepMind.

## Future Outlook / Welcomed Contributions

The following implementations or fixes are welcome:

- **RTL Text Support**: Support for right-to-left languages such as Arabic and Hebrew.
- **Performance Improvements**: Optimization of the software rasterizer.
- **Bug Fixes**: Fixes for rendering glitches in edge cases or memory leaks.

If you have any questions, please reach out via Issues or Pull Requests.
