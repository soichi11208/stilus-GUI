// src/platform/wayland/wl_proto.cpp
#include "wl_proto.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace stilus::wl {

// ---------------------------------------------------------------------------
// Message
// ---------------------------------------------------------------------------
Message::Message(ObjectId sender, uint16_t opcode) {
    buf_.resize(8);
    uint32_t hdr0 = sender;
    __builtin_memcpy(buf_.data(), &hdr0, 4);
    // opcode and size get finalized in data()
    uint32_t hdr1 = uint32_t(opcode); // size=0 placeholder
    __builtin_memcpy(buf_.data() + 4, &hdr1, 4);
}

void Message::write_u32(uint32_t v) {
    size_t off = buf_.size();
    buf_.resize(off + 4);
    __builtin_memcpy(buf_.data() + off, &v, 4);
}

void Message::pad_to_4() {
    while (buf_.size() & 3u) buf_.push_back(0);
}

Message& Message::string(std::string_view s) {
    uint32_t len = uint32_t(s.size() + 1); // includes NUL terminator
    write_u32(len);
    size_t off = buf_.size();
    buf_.resize(off + s.size() + 1);
    __builtin_memcpy(buf_.data() + off, s.data(), s.size());
    buf_[off + s.size()] = 0;
    pad_to_4();
    return *this;
}

Message& Message::array(const void* data, uint32_t len) {
    write_u32(len);
    size_t off = buf_.size();
    buf_.resize(off + len);
    if (len) __builtin_memcpy(buf_.data() + off, data, len);
    pad_to_4();
    return *this;
}

const std::vector<uint8_t>& Message::data() {
    if (!finalized_) {
        finalized_ = true;
        uint32_t size = uint32_t(buf_.size());
        // hdr1 = opcode (low 16) | size (high 16)
        uint32_t hdr1;
        __builtin_memcpy(&hdr1, buf_.data() + 4, 4);
        hdr1 = (hdr1 & 0x0000ffffu) | (size << 16);
        __builtin_memcpy(buf_.data() + 4, &hdr1, 4);
    }
    return buf_;
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------
Display::Display()  = default;
Display::~Display() { disconnect(); }

bool Display::connect() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (!runtime) { std::fprintf(stderr, "XDG_RUNTIME_DIR not set\n"); return false; }
    const char* display = std::getenv("WAYLAND_DISPLAY");
    if (!display || !*display) display = "wayland-0";

    fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0) { std::perror("socket"); return false; }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    // Support absolute paths too (rare but allowed).
    if (display[0] == '/') {
        if (std::strlen(display) >= sizeof(addr.sun_path)) {
            std::fprintf(stderr, "WAYLAND_DISPLAY path too long\n"); return false;
        }
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", display);
    } else {
        if (std::strlen(runtime) + 1 + std::strlen(display) >= sizeof(addr.sun_path)) {
            std::fprintf(stderr, "socket path too long\n"); return false;
        }
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/%s", runtime, display);
    }

    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("connect");
        ::close(fd_); fd_ = -1;
        return false;
    }

    // wl_display lives at id=1 — no handler registered here; subclasses can.
    return true;
}

void Display::disconnect() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    for (int f : rx_fds_) ::close(f);
    rx_fds_.clear();
    rx_buf_.clear();
    handlers_.clear();
}

ObjectId Display::new_id() { return next_id_++; }

void Display::set_handler(ObjectId id, EventFn fn) {
    handlers_[id] = std::move(fn);
}

void Display::remove_handler(ObjectId id) { handlers_.erase(id); }

void Display::register_fd_message(ObjectId id, uint16_t opcode) {
    fd_messages_.insert((uint64_t(id) << 16) | opcode);
}

bool Display::send(Message& m) {
    if (fd_ < 0) return false;
    const auto& buf = m.data();
    const auto& fds = m.fds();

    iovec iov{};
    iov.iov_base = const_cast<uint8_t*>(buf.data());
    iov.iov_len  = buf.size();

    msghdr msg{};
    msg.msg_iov    = &iov;
    msg.msg_iovlen = 1;

    // CMSG buffer for fds.
    alignas(cmsghdr) char cmsgbuf[CMSG_SPACE(sizeof(int) * 16)] = {};
    if (!fds.empty()) {
        if (fds.size() > 16) {
            std::fprintf(stderr, "too many fds in one message\n"); return false;
        }
        msg.msg_control    = cmsgbuf;
        msg.msg_controllen = CMSG_SPACE(sizeof(int) * fds.size());
        cmsghdr* c = CMSG_FIRSTHDR(&msg);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type  = SCM_RIGHTS;
        c->cmsg_len   = CMSG_LEN(sizeof(int) * fds.size());
        __builtin_memcpy(CMSG_DATA(c), fds.data(), sizeof(int) * fds.size());
        msg.msg_controllen = c->cmsg_len;
    }

    ssize_t n;
    do { n = ::sendmsg(fd_, &msg, MSG_NOSIGNAL); }
    while (n < 0 && errno == EINTR);

    if (n < 0) { std::perror("sendmsg"); return false; }
    if (size_t(n) != buf.size()) {
        // Partial writes on SOCK_STREAM are possible; very rare on local sockets.
        std::fprintf(stderr, "partial sendmsg (%zd/%zu)\n", n, buf.size());
        return false;
    }
    return true;
}

bool Display::recv_some() {
    uint8_t   chunk[4096];
    int       fdbuf[16];
    alignas(cmsghdr) char cmsgbuf[CMSG_SPACE(sizeof(fdbuf))] = {};

    iovec iov{};
    iov.iov_base = chunk;
    iov.iov_len  = sizeof(chunk);

    msghdr msg{};
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsgbuf;
    msg.msg_controllen = sizeof(cmsgbuf);

    ssize_t n;
    do { n = ::recvmsg(fd_, &msg, MSG_CMSG_CLOEXEC); }
    while (n < 0 && errno == EINTR);

    if (n == 0) return false;  // peer closed
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        std::perror("recvmsg");
        return false;
    }

    rx_buf_.insert(rx_buf_.end(), chunk, chunk + n);

    for (cmsghdr* c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
            size_t nfd = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            const int* fds = reinterpret_cast<const int*>(CMSG_DATA(c));
            for (size_t i = 0; i < nfd; ++i) rx_fds_.push_back(fds[i]);
        }
    }
    return true;
}

bool Display::dispatch_from_buf() {
    bool dispatched = false;
    while (rx_buf_.size() >= 8) {
        uint32_t hdr0, hdr1;
        __builtin_memcpy(&hdr0, rx_buf_.data(),     4);
        __builtin_memcpy(&hdr1, rx_buf_.data() + 4, 4);

        ObjectId sender = hdr0;
        uint16_t opcode = uint16_t(hdr1 & 0xffffu);
        uint16_t size   = uint16_t(hdr1 >> 16);
        if (size < 8) {
            std::fprintf(stderr, "bad wayland message size=%u\n", size);
            return false;
        }
        if (rx_buf_.size() < size) break; // incomplete, wait for more

        const uint8_t* payload    = rx_buf_.data() + 8;
        size_t         payload_ln = size - 8;

        auto it = handlers_.find(sender);
        if (it != handlers_.end()) {
            // Only messages explicitly declared as fd-carrying receive fds;
            // everything else gets nfds=0. This matters when a single
            // recvmsg batches an fd-carrying event (e.g. wl_keyboard.keymap)
            // together with unrelated events — without this filtering, the
            // first-dispatched event eats the fd meant for the later one.
            uint64_t key = (uint64_t(sender) << 16) | opcode;
            if (fd_messages_.count(key) && !rx_fds_.empty()) {
                int fd = rx_fds_.front();
                rx_fds_.erase(rx_fds_.begin());
                it->second(sender, opcode, payload, payload_ln, &fd, 1);
            } else {
                it->second(sender, opcode, payload, payload_ln, nullptr, 0);
            }
        } else {
            // Default: warn about unhandled object events. Useful during bring-up.
            // For unknown globals we will just ignore: comment out if noisy.
            // std::fprintf(stderr, "unhandled event: obj=%u op=%u size=%u\n",
            //              sender, opcode, size);
        }

        rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + size);
        dispatched = true;
    }
    return dispatched;
}

bool Display::dispatch_pending() {
    return dispatch_from_buf();
}

bool Display::read_dispatch(int timeout_ms) {
    if (dispatch_from_buf()) return true;

    pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;
    int pr;
    do { pr = ::poll(&pfd, 1, timeout_ms); }
    while (pr < 0 && errno == EINTR);

    if (pr < 0) { std::perror("poll"); return false; }
    if (pr == 0) return true; // timeout, no event but not an error

    if (pfd.revents & (POLLHUP | POLLERR)) return false;
    if (!recv_some()) return false;
    dispatch_from_buf();
    return true;
}

bool Display::roundtrip() {
    // Allocate a callback id, send wl_display.sync, wait for wl_callback.done.
    ObjectId cb = new_id();
    bool done = false;
    set_handler(cb, [&](ObjectId, uint16_t op, const uint8_t*, size_t,
                        const int*, size_t) {
        if (op == wl_callback_evt::done) done = true;
    });

    Message m(DISPLAY_ID, wl_display_req::sync);
    m.new_id(cb);
    if (!send(m)) return false;

    while (!done) {
        if (!read_dispatch(-1)) { remove_handler(cb); return false; }
    }
    remove_handler(cb);
    return true;
}

} // namespace stilus::wl
