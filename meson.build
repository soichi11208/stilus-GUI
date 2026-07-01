project(
  'stilus',
  'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=3',
    'werror=false',
  ],
)

add_project_arguments(
  '-Wpedantic', '-Wno-unused-parameter',
  language : 'cpp',
)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
# XCB prebuilt static archives (vendored, never shadow system libxcb).
# These are prebuilt, so we add them to the link command via add_project_link_arguments.
xcb_src_root = meson.project_source_root() / 'third_party' / 'xcb' / 'lib'
xcb_link_args = [
  xcb_src_root / 'libxcb.a',
  xcb_src_root / 'libxcb-render.a',
  xcb_src_root / 'libxcb-shm.a',
  xcb_src_root / 'libxcb-xfixes.a',
  xcb_src_root / 'libxcb-xkb.a',
]
add_project_link_arguments(xcb_link_args, language : 'cpp')

# Xau / Xdmcp — X11 auth libs that XCB transitively depends on. Only needed
# on Linux (not macOS).
x11_deps = []
if host_machine.system() == 'linux'
  xau_dep = dependency('xau', required: false, static: true)
  xdmcp_dep = dependency('xdmcp', required: false, static: true)
  if xau_dep.found() and xdmcp_dep.found()
    x11_deps = [xau_dep, xdmcp_dep]
  endif
endif

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
stilus_sources = [
  'src/window.cpp',
  'src/widget.cpp',
  'src/widgets.cpp',
  'src/render/soft_canvas.cpp',
  'src/render/path.cpp',
  'src/render/path_raster.cpp',
  'src/text/font.cpp',
]

# Platform backends — only on Linux (not macOS). Wayland needs nothing extra;
# X11 statically links the vendored XCB above plus libXau/libXdmcp.
if host_machine.system() == 'linux'
  stilus_sources += [
    'src/platform/wayland/wl_proto.cpp',
    'src/platform/wayland/wl_window.cpp',
    'src/platform/wayland/wl_popup.cpp',
    'src/platform/wayland/wl_factory.cpp',
    'src/platform/wayland/xkb_parse.cpp',
    'src/platform/x11/xcb_window.cpp',
    'src/platform/x11/xcb_factory.cpp',
  ]
endif

stilus = static_library(
  'stilus',
  stilus_sources,
  include_directories : include_directories(
    'include',
    'src',
    'third_party/xcb/include',
    'third_party',
  ),
  dependencies : x11_deps,
  install : true,
)

stilus_dep = declare_dependency(
  link_with : stilus,
  include_directories : include_directories('include'),
  dependencies : x11_deps,
)

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
examples = [
  'hello',
  'input_probe',
  'paint_demo',
  'path_demo',
  'widget_demo',
  'transform_demo',
  'cjk_demo',
  'anim_demo',
  'clipboard_demo',
  'popup_demo',
  'emoji_demo',
  'backend_test',
]

foreach ex : examples
  executable(ex, 'examples/' + ex + '.cpp', dependencies : stilus_dep, install : false)
endforeach

# xkb_test has a special source (xkb_parse.cpp compiled only for the test)
executable(
  'xkb_test',
  'examples/xkb_test.cpp',
  'src/platform/wayland/xkb_parse.cpp',
  include_directories : include_directories('src'),
  dependencies : stilus_dep,
  install : false,
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
test_inc = include_directories('test')

test_unit = executable(
  'test_unit',
  'test/test_unit.cpp',
  include_directories : include_directories('test', 'src'),
  dependencies : stilus_dep,
  install : false,
)
test('unit', test_unit)

golden_dir = meson.project_source_root() / 'test' / 'golden'
test_golden = executable(
  'test_golden',
  'test/test_golden.cpp',
  include_directories : include_directories('test', 'src'),
  dependencies : stilus_dep,
  cpp_args : ['-DSTILUS_TEST_GOLDEN_DIR="' + golden_dir + '"'],
  install : false,
)
test('golden', test_golden)

test_x11 = executable(
  'test_x11_integration',
  'test/test_x11_integration.cpp',
  'test/integration/x11_smoke.cpp',
  include_directories : include_directories('test', 'src'),
  dependencies : stilus_dep,
  install : false,
)
# Don't register as a Meson test — it needs a real display. Run manually or via
# xvfb-run in CI.

# ---------------------------------------------------------------------------
# pkg-config
# ---------------------------------------------------------------------------
# libstilus.a is static.  For the X11 path, downstream linkers need the
# vendored XCB archives (under <libdir>/stilus/) plus libXau/libXdmcp.
# We keep these in Libs.private so only static consumers see them.
stilus_xcb_libdir = '${libdir}/stilus'
xcb_lib_names = ['xcb', 'xcb-render', 'xcb-shm', 'xcb-xfixes', 'xcb-xkb']
xcclib = ''
foreach n : xcb_lib_names
  xcclib += ' -L' + stilus_xcb_libdir + ' -l' + n
endforeach
xcclib = xcclib.strip()

x11_priv = ''
if x11_deps.length() > 0
  x11_priv = ' -lXau -lXdmcp'
endif

configure_file(
  input : 'stilus.pc.in',
  output : 'stilus.pc',
  configuration : {
    'PREFIX'       : get_option('prefix'),
    'EXEC_PREFIX'  : '${prefix}',
    'LIBDIR'       : get_option('libdir'),
    'INCLUDEDIR'   : get_option('includedir'),
    'VERSION'      : meson.project_version(),
    'XCCLIB'       : xcclib,
    'X11_PRIV'     : x11_priv,
  },
  install : true,
  install_dir : get_option('libdir') / 'pkgconfig',
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
# Install vendored XCB archives under <libdir>/stilus/ so they never shadow
# a system libxcb.
stilus_lib_dir = get_option('libdir') / 'stilus'
foreach a : xcb_link_args
  install_data(a, install_dir : stilus_lib_dir)
endforeach

install_subdir('include/stilus', install_dir : get_option('includedir'))
