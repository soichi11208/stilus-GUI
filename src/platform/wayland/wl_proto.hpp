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
#include <unordered_set>
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

namespace xdg_positioner_req {
    enum { destroy = 0, set_size = 1, set_anchor_rect = 2,
           set_anchor = 3, set_gravity = 4,
           set_constraint_adjustment = 5, set_offset = 6 };
}
namespace xdg_positioner_anchor {
    constexpr uint32_t none         = 0;
    constexpr uint32_t top          = 1;
    constexpr uint32_t bottom       = 2;
    constexpr uint32_t left         = 3;
    constexpr uint32_t right        = 4;
    constexpr uint32_t top_left     = 5;
    constexpr uint32_t bottom_left  = 6;
    constexpr uint32_t top_right    = 7;
    constexpr uint32_t bottom_right = 8;
}
namespace xdg_positioner_gravity {
    constexpr uint32_t none         = 0;
    constexpr uint32_t top          = 1;
    constexpr uint32_t bottom       = 2;
    constexpr uint32_t left         = 3;
    constexpr uint32_t right        = 4;
    constexpr uint32_t top_left     = 5;
    constexpr uint32_t bottom_left  = 6;
    constexpr uint32_t top_right    = 7;
    constexpr uint32_t bottom_right = 8;
}

namespace xdg_popup_req {
    enum { destroy = 0, grab = 1, reposition = 2 };
}
namespace xdg_popup_evt {
    enum { configure = 0, popup_done = 1, repositioned = 2 };
}

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

// xdg_toplevel.resize edge values (spec: "resize edge" — bit union of
// top(1)/bottom(2)/left(4)/right(8) makes corners).
namespace xdg_toplevel_resize_edge {
    constexpr uint32_t none         = 0;
    constexpr uint32_t top          = 1;
    constexpr uint32_t bottom       = 2;
    constexpr uint32_t left         = 4;
    constexpr uint32_t top_left     = 5;
    constexpr uint32_t bottom_left  = 6;
    constexpr uint32_t right        = 8;
    constexpr uint32_t top_right    = 9;
    constexpr uint32_t bottom_right = 10;
}

// wp_cursor_shape_v1 — modern (2023+) way to request one of a fixed set of
// named cursor shapes without loading a cursor theme ourselves.
namespace wp_cursor_shape_manager_v1_req {
    enum { destroy = 0, get_pointer = 1, get_tablet_tool_v2 = 2 };
}
namespace wp_cursor_shape_device_v1_req {
    enum { destroy = 0, set_shape = 1 };
}
namespace wp_cursor_shape_device_v1_shape {
    constexpr uint32_t default_       = 1;
    constexpr uint32_t context_menu   = 2;
    constexpr uint32_t help           = 3;
    constexpr uint32_t pointer        = 4;
    constexpr uint32_t progress       = 5;
    constexpr uint32_t wait           = 6;
    constexpr uint32_t cell           = 7;
    constexpr uint32_t crosshair      = 8;
    constexpr uint32_t text           = 9;
    constexpr uint32_t vertical_text  = 10;
    constexpr uint32_t alias          = 11;
    constexpr uint32_t copy           = 12;
    constexpr uint32_t move           = 13;
    constexpr uint32_t no_drop        = 14;
    constexpr uint32_t not_allowed    = 15;
    constexpr uint32_t grab           = 16;
    constexpr uint32_t grabbing       = 17;
    constexpr uint32_t e_resize       = 18;
    constexpr uint32_t n_resize       = 19;
    constexpr uint32_t ne_resize      = 20;
    constexpr uint32_t nw_resize      = 21;
    constexpr uint32_t s_resize       = 22;
    constexpr uint32_t se_resize      = 23;
    constexpr uint32_t sw_resize      = 24;
    constexpr uint32_t w_resize       = 25;
    constexpr uint32_t ew_resize      = 26;
    constexpr uint32_t ns_resize      = 27;
    constexpr uint32_t nesw_resize    = 28;
    constexpr uint32_t nwse_resize    = 29;
    constexpr uint32_t col_resize     = 30;
    constexpr uint32_t row_resize     = 31;
    constexpr uint32_t all_scroll     = 32;
    constexpr uint32_t zoom_in        = 33;
    constexpr uint32_t zoom_out       = 34;
}

// wl_data_device_manager, wl_data_device, wl_data_source, wl_data_offer —
// the copy/paste (and drag-and-drop) machinery. We only wire up copy/paste.
namespace wl_data_device_manager_req {
    enum { create_data_source = 0, get_data_device = 1 };
}
namespace wl_data_device_req {
    enum { start_drag = 0, set_selection = 1, release = 2 };
}
namespace wl_data_device_evt {
    enum { data_offer = 0, enter = 1, leave = 2, motion = 3,
           drop = 4, selection = 5 };
}
namespace wl_data_source_req {
    enum { offer = 0, destroy = 1, set_actions = 2 };
}
namespace wl_data_source_evt {
    enum { target = 0, send = 1, cancelled = 2,
           dnd_drop_performed = 3, dnd_finished = 4, action = 5 };
}
namespace wl_data_offer_req {
    enum { accept = 0, receive = 1, destroy = 2, finish = 3, set_actions = 4 };
}
namespace wl_data_offer_evt {
    enum { offer = 0, source_actions = 1, action = 2 };
}

// wp_viewporter — lets us decouple the buffer size (in physical pixels)
// from the surface's logical size, which is what fractional scaling needs.
namespace wp_viewporter_req {
    enum { destroy = 0, get_viewport = 1 };
}
namespace wp_viewport_req {
    enum { destroy = 0, set_source = 1, set_destination = 2 };
}

// wp_fractional_scale_v1 — the compositor tells us a preferred scale as
// (real_scale * 120), i.e. 180 == 1.5x. Used together with wp_viewporter.
namespace wp_fractional_scale_manager_v1_req {
    enum { destroy = 0, get_fractional_scale = 1 };
}
namespace wp_fractional_scale_v1_req {
    enum { destroy = 0 };
}
namespace wp_fractional_scale_v1_evt {
    enum { preferred_scale = 0 };
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
inline float   fixed_to_float(int32_t v) { return float(v) / 256.0f; }
inline int32_t float_to_fixed(float   v) { return int32_t(v * 256.0f); }

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

    // Declare that (object, opcode) is expected to carry an out-of-band fd
    // (SCM_RIGHTS). Handlers are called with the correct fd for these; every
    // other message gets nfds=0. Without this, we can't tell fd-consuming
    // events from unrelated ones sitting next to them in the same recvmsg
    // batch, and the fd gets handed to whichever event dispatches first —
    // exactly the bug we hit with wl_keyboard.keymap.
    void register_fd_message(ObjectId id, uint16_t opcode);

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

    // (object_id << 16) | opcode  →  yes, this message wants an fd.
    std::unordered_set<uint64_t> fd_messages_;

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
