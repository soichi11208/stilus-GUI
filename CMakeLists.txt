cmake_minimum_required(VERSION 3.20)
project(stilus CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
set(STILUS_SOURCES
    src/window.cpp
    src/widget.cpp
    src/widgets.cpp
    src/render/soft_canvas.cpp
    src/render/path.cpp
    src/render/path_raster.cpp
    src/text/font.cpp
)

if(UNIX AND NOT APPLE)
    list(APPEND STILUS_SOURCES
        src/platform/wayland/wl_proto.cpp
        src/platform/wayland/wl_window.cpp
        src/platform/wayland/wl_factory.cpp
        src/platform/wayland/xkb_parse.cpp
        src/platform/x11/xcb_window.cpp
        src/platform/x11/xcb_factory.cpp
    )
endif()

add_library(stilus STATIC ${STILUS_SOURCES})
target_include_directories(stilus
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/xcb/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# Nothing to link for the Wayland backend — we speak the wire protocol
# ourselves and pass fds via SCM_RIGHTS. No libwayland-client dependency.

# X11 backend — statically link XCB libraries.
set(XCB_LIBS
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/xcb/lib/libxcb.a
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/xcb/lib/libxcb-render.a
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/xcb/lib/libxcb-shm.a
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/xcb/lib/libxcb-xkb.a
)
target_link_libraries(stilus PRIVATE ${XCB_LIBS} Xau Xdmcp)

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
add_executable(hello examples/hello.cpp)
target_link_libraries(hello PRIVATE stilus)

add_executable(input_probe examples/input_probe.cpp)
target_link_libraries(input_probe PRIVATE stilus)

add_executable(paint_demo examples/paint_demo.cpp)
target_link_libraries(paint_demo PRIVATE stilus)

add_executable(path_demo examples/path_demo.cpp)
target_link_libraries(path_demo PRIVATE stilus)

add_executable(xkb_test examples/xkb_test.cpp
               src/platform/wayland/xkb_parse.cpp)
target_include_directories(xkb_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(widget_demo examples/widget_demo.cpp)
target_link_libraries(widget_demo PRIVATE stilus)

add_executable(transform_demo examples/transform_demo.cpp)
target_link_libraries(transform_demo PRIVATE stilus)

add_executable(cjk_demo examples/cjk_demo.cpp)
target_link_libraries(cjk_demo PRIVATE stilus)

add_executable(backend_test examples/backend_test.cpp)
target_link_libraries(backend_test PRIVATE stilus)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
add_executable(test_x11_integration
    test/test_x11_integration.cpp
    test/integration/x11_thermal.cpp
    test/integration/x11_gpu.cpp
    test/integration/x11_display.cpp
)
target_include_directories(test_x11_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test)
target_link_libraries(test_x11_integration PRIVATE stilus)
