// src/platform/wayland/wl_proto.hpp
// Self-implemented Wayland wire protocol. No dependency on libwayland-client.
//
// Wayland wire format (little-endian on all supported platforms):
//   header: [sender_id:u32][opcode:u16 | size:u16]
//   args follow header packed; strings/arrays padded up to 4 bytes.
//
// Supported arg types here: i32, u32, string, object, new_id, array, fd.
// fds are not written into the byte stream; they travel out-of-band via
// SCM_RIGHTS on the AF_UNIX socket.
#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace stilus::wl {

using ObjectId = uint32_t;
constexpr ObjectId DISPLAY_ID = 1;

// ---- Well-known interfaces / opcodes --------------------------------------
// Only the pieces we actually use. Opcode order is part of the Wayland ABI
// and is stable across protocol versions (new methods append).

namespace wl_display_req { enum { sync = 0, get_registry = 1 }; }
namespace wl_display_evt { enum { error = 0, delete_id = 1 }; }

namespace wl_registry_req { enum { bind = 0 }; }
namespace wl_registry_evt { enum { global = 0, global_remove = 1 }; }

namespace wl_callback_evt { enum { done = 0 }; }

namespace wl_compositor_req { enum { create_surface = 0, create_region = 1 }; }

namespace wl_surface_req {
    enum { destroy = 0, attach = 1, damage = 2, frame = 3,
           set_opaque_region = 4, set_input_region = 5, commit = 6,
           set_buffer_transform = 7, set_buffer_scale = 8, damage_buffer = 9 };
}
namespace wl_surface_evt { enum { enter = 0, leave = 1 }; }

namespace wl_shm_req { enum { create_pool = 0 }; }
namespace wl_shm_evt { enum { format = 0 }; }

namespace wl_shm_pool_req { enum { create_buffer = 0, destroy = 1, resize = 2 }; }

namespace wl_buffer_evt { enum { release = 0 }; }

namespace xdg_wm_base_req {
    enum { destroy = 0, create_positioner = 1, get_xdg_surface = 2, pong = 3 };
}
namespace xdg_wm_base_evt { enum { ping = 0 }; }

namespace xdg_surface_req {
    enum { destroy = 0, get_toplevel = 1, get_popup = 2,
           set_window_geometry = 3, ack_configure = 4 };
}
namespace xdg_surface_evt { enum { configure = 0 }; }

namespace xdg_toplevel_req {
    enum { destroy = 0, set_parent = 1, set_title = 2, set_app_id = 3,
           show_window_menu = 4, move = 5, resize = 6,
           set_max_size = 7, set_min_size = 8,
           set_maximized = 9, unset_maximized = 10,
           set_fullscreen = 11, unset_fullscreen = 12,
           set_minimized = 13 };
}
namespace xdg_toplevel_evt {
    enum { configure = 0, close = 1, configure_bounds = 2, wm_capabilities = 3 };
}

namespace wl_seat_req {
    enum { get_pointer = 0, get_keyboard = 1, get_touch = 2, release = 3 };
}
namespace wl_seat_evt { enum { capabilities = 0, name = 1 }; }

// wl_seat.capabilities bits
namespace wl_seat_caps {
    constexpr uint32_t pointer  = 1 << 0;
    constexpr uint32_t keyboard = 1 << 1;
    constexpr uint32_t touch    = 1 << 2;
}

namespace wl_pointer_req { enum { set_cursor = 0, release = 1 }; }
namespace wl_pointer_evt {
    enum { enter = 0, leave = 1, motion = 2, button = 3,
           axis = 4, frame = 5, axis_source = 6, axis_stop = 7,
           axis_discrete = 8, axis_value120 = 9, axis_relative_direction = 10 };
}
namespace wl_pointer_btn_state {
    constexpr uint32_t released = 0;
    constexpr uint32_t pressed  = 1;
}
namespace wl_pointer_axis {
    constexpr uint32_t vertical_scroll   = 0;
    constexpr uint32_t horizontal_scroll = 1;
}

namespace wl_keyboard_req { enum { release = 0 }; }
namespace wl_keyboard_evt {
    enum { keymap = 0, enter = 1, leave = 2, key = 3,
           modifiers = 4, repeat_info = 5 };
}
namespace wl_keyboard_key_state {
    constexpr uint32_t released = 0;
    constexpr uint32_t pressed  = 1;
}

// xdg-decoration-unstable-v1 — lets the compositor draw the titlebar +
// minimize/maximize/close buttons instead of forcing CSD.
namespace zxdg_decoration_manager_v1_req {
    enum { destroy = 0, get_toplevel_decoration = 1 };
}
namespace zxdg_toplevel_decoration_v1_req {
    enum { destroy = 0, set_mode = 1, unset_mode = 2 };
}
namespace zxdg_toplevel_decoration_v1_evt {
    enum { configure = 0 };
}
namespace zxdg_toplevel_decoration_v1_mode {
    constexpr uint32_t client_side = 1;
    constexpr uint32_t server_side = 2;
}

// wl_output (core, version >= 2 needed for the scale event).
namespace wl_output_req { enum { release = 0 }; }
namespace wl_output_evt {
    enum { geometry = 0, mode = 1, done = 2, scale = 3,
           name = 4, description = 5 };
}

// text-input-unstable-v3 (Wayland protocol extension; 1 interface each)
namespace zwp_text_input_manager_v3_req {
    enum { destroy = 0, get_text_input = 1 };
}
namespace zwp_text_input_v3_req {
    enum { destroy = 0, enable = 1, disable = 2,
           set_surrounding_text = 3, set_text_change_cause = 4,
           set_content_type = 5, set_cursor_rectangle = 6,
           commit = 7 };
}
namespace zwp_text_input_v3_evt {
    enum { enter = 0, leave = 1,
           preedit_string = 2, commit_string = 3,
           delete_surrounding_text = 4, done = 5 };
}

// Convert Wayland fixed (24.8) to float.
inline float fixed_to_float(int32_t v) { return float(v) / 256.0f; }

// wl_shm format codes
namespace shm_format {
    constexpr uint32_t ARGB8888 = 0;
    constexpr uint32_t XRGB8888 = 1;
}

// ---- Argument writer -------------------------------------------------------
// A small builder that accumulates args into a byte buffer plus an optional
// fd to pass via SCM_RIGHTS.
class Message {
public:
    Message(ObjectId sender, uint16_t opcode);

    Message& i32(int32_t v)       { write_u32(uint32_t(v)); return *this; }
    Message& u32(uint32_t v)      { write_u32(v);           return *this; }
    Message& object(ObjectId id)  { write_u32(id);          return *this; }
    Message& new_id(ObjectId id)  { write_u32(id);          return *this; }
    Message& string(std::string_view s);
    Message& array(const void* data, uint32_t len);

    // Attach an fd to transmit out-of-band (SCM_RIGHTS). Multiple fds allowed.
    Message& fd(int fd) { fds_.push_back(fd); return *this; }

    const std::vector<uint8_t>& data();       // finalize size and return
    const std::vector<int>&     fds()  const { return fds_; }

private:
    void write_u32(uint32_t v);
    void pad_to_4();

    std::vector<uint8_t> buf_;
    std::vector<int>     fds_;
    bool finalized_ = false;
};

// ---- Display / connection --------------------------------------------------
class Display {
public:
    // Event handler signature: (object_id, opcode, payload, payload_len, fds, nfds)
    using EventFn = std::function<void(ObjectId, uint16_t,
                                       const uint8_t*, size_t,
                                       const int*, size_t)>;

    Display();
    ~Display();
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    // Connect to $WAYLAND_DISPLAY under $XDG_RUNTIME_DIR. Returns false on error.
    bool connect();
    void disconnect();

    int  fd() const { return fd_; }
    bool ok() const { return fd_ >= 0; }

    // Allocate a new client-side object id (odd-range is for clients? No — the
    // Wayland spec allocates 0x00000001..0xfeffffff to the client, 0xff000000+
    // to the server. We just start from 2 and increment).
    ObjectId new_id();

    // Register an object so incoming events get dispatched to `fn`.
    void set_handler(ObjectId id, EventFn fn);
    void remove_handler(ObjectId id);

    // Send a fully-built message (consumes fds into sendmsg).
    bool send(Message& m);

    // Block until at least one event is read and dispatched, or timeout.
    // Returns false on error/disconnect. timeout_ms < 0 means wait forever.
    bool read_dispatch(int timeout_ms = -1);

    // Drain buffered events without blocking.
    bool dispatch_pending();

    // Request a synchronous roundtrip to the server: creates a wl_callback
    // via sync, then reads until the callback fires.
    bool roundtrip();

private:
    bool recv_some();          // read one chunk into rx_buf_/rx_fds_
    bool dispatch_from_buf();  // pop as many complete messages as possible

    int fd_ = -1;
    ObjectId next_id_ = 2;
    std::unordered_map<ObjectId, EventFn> handlers_;

    std::vector<uint8_t> rx_buf_;
    std::vector<int>     rx_fds_;
};

// ---- Argument reader helpers ----------------------------------------------
// Payload walkers for use inside event handlers.
struct Reader {
    const uint8_t* p;
    size_t         n;

    uint32_t u32() {
        uint32_t v;
        __builtin_memcpy(&v, p, 4);
        p += 4; n -= 4;
        return v;
    }
    int32_t i32() { return int32_t(u32()); }
    std::string_view string() {
        uint32_t len = u32();            // includes trailing NUL
        const char* s = reinterpret_cast<const char*>(p);
        size_t slen = len == 0 ? 0 : len - 1;
        size_t padded = (len + 3) & ~uint32_t(3);
        p += padded; n -= padded;
        return { s, slen };
    }
    // Array: returns pointer+length, advances past padded block.
    std::pair<const uint8_t*, uint32_t> array() {
        uint32_t len = u32();
        const uint8_t* base = p;
        size_t padded = (len + 3) & ~uint32_t(3);
        p += padded; n -= padded;
        return { base, len };
    }
};

} // namespace stilus::wl
