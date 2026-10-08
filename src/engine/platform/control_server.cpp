#include "engine/platform/control_server.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <random>

#include "engine/core/log.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace pt {
namespace {

constexpr std::string_view kGreeting = "pt-control 1\n";

uint64_t NowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

#ifdef _WIN32
bool Startup() {
    static const bool ok = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}
bool WouldBlock() {
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
void CloseSocket(uintptr_t s) { closesocket(static_cast<SOCKET>(s)); }
bool SetNonBlocking(uintptr_t s) {
    u_long on = 1;
    return ioctlsocket(static_cast<SOCKET>(s), FIONBIO, &on) == 0;
}
int LastError() { return WSAGetLastError(); }
#else
bool Startup() { return true; }
bool WouldBlock() { return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR; }
void CloseSocket(uintptr_t s) { close(static_cast<int>(s)); }
bool SetNonBlocking(uintptr_t s) {
    const int fd = static_cast<int>(s);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) == 0;
}
int LastError() { return errno; }
#endif

void NoDelay(uintptr_t s) {
    int on = 1;
#ifdef _WIN32
    setsockopt(static_cast<SOCKET>(s), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
#else
    setsockopt(static_cast<int>(s), IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
#endif
}

}

ControlServer::~ControlServer() {
    Stop();
}

bool ControlServer::Start(int port, std::string token, std::string info) {
    Stop();
    if (!Startup()) {
        LogError("control: no sockets (WSAStartup failed)");
        return false;
    }
    if (port < 0 || port > 65535) {
        LogError("control: port {} is not a TCP port", port);
        return false;
    }
#ifdef _WIN32
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        LogError("control: socket() failed ({})", LastError());
        return false;
    }
    /* no SO_REUSEADDR on Windows: there it would let another program take over the port; exclusive use instead */
    BOOL exclusive = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#else
    const int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) {
        LogError("control: socket() failed ({})", LastError());
        return false;
    }
    int reuse = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    const uintptr_t handle = static_cast<uintptr_t>(s);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(s, 4) != 0 || !SetNonBlocking(handle)) {
        LogError("control: cannot listen on 127.0.0.1:{} ({}); is another game or tool using the port?", port, LastError());
        CloseSocket(handle);
        return false;
    }
    sockaddr_in bound{};
#ifdef _WIN32
    int length = sizeof(bound);
#else
    socklen_t length = sizeof(bound);
#endif
    getsockname(s, reinterpret_cast<sockaddr*>(&bound), &length);
    listen_ = handle;
    port_ = ntohs(bound.sin_port);
    token_ = std::move(token);
    info_ = info.empty() ? "{}" : std::move(info);
    return true;
}

void ControlServer::Stop() {
    for (Client& client : clients_) {
        if (client.socket != kNoSocket) CloseSocket(client.socket);
    }
    clients_.clear();
    if (listen_ != kNoSocket) {
        CloseSocket(listen_);
        listen_ = kNoSocket;
    }
    port_ = 0;
}

void ControlServer::Poll(const Handler& handler) {
    if (listen_ == kNoSocket) return;
    /* the connections that are there first, so one that has just closed no longer counts when a new one comes in */
    for (Client& client : clients_) {
        if (!client.dead) Receive(client);
        if (!client.dead && !client.closing) Answer(client, handler);
        if (!client.authenticated && !client.closing && NowMs() - client.opened_ms > kHelloMs) {
            client.out += "err " + JsonString("no hello within 5 seconds") + "\n";
            Close(client, "no hello in time");
        }
        if (!client.dead) Send(client);
        if (client.closing && client.out.empty() && !client.dead) Shut(client);
    }
    std::erase_if(clients_, [](const Client& client) {
        if (!client.dead) return false;
        if (client.socket != kNoSocket) CloseSocket(client.socket);
        return true;
    });
    Accept();
}

void ControlServer::Accept() {
    for (;;) {
#ifdef _WIN32
        const SOCKET s = accept(static_cast<SOCKET>(listen_), nullptr, nullptr);
        if (s == INVALID_SOCKET) return;
#else
        const int s = accept(static_cast<int>(listen_), nullptr, nullptr);
        if (s < 0) return;
#endif
        /* refused connections linger for a moment; past this many, a new one is closed at once */
        if (clients_.size() >= kMaxClients * 4) {
            CloseSocket(static_cast<uintptr_t>(s));
            continue;
        }
        Client client;
        client.socket = static_cast<uintptr_t>(s);
        client.id = next_id_++;
        client.opened_ms = NowMs();
        SetNonBlocking(client.socket);
        NoDelay(client.socket);
        const size_t open = static_cast<size_t>(std::count_if(clients_.begin(), clients_.end(), [](const Client& c) { return !c.closing; }));
        if (open >= kMaxClients) {
            client.out = "err " + JsonString("too many clients") + "\n";
            client.closing = true;
            LogWarn("control: connection {} refused, already {} clients", client.id, open);
        } else {
            client.out = std::string(kGreeting);
            LogInfo("control: connection {} opened", client.id);
        }
        clients_.push_back(std::move(client));
    }
}

void ControlServer::Receive(Client& client) {
    char buffer[16384];
    for (;;) {
#ifdef _WIN32
        const int got = recv(static_cast<SOCKET>(client.socket), buffer, sizeof(buffer), 0);
#else
        const int got = static_cast<int>(recv(static_cast<int>(client.socket), buffer, sizeof(buffer), 0));
#endif
        if (got > 0) {
            if (!client.closing) client.in.append(buffer, static_cast<size_t>(got));
            const size_t last_line = client.in.rfind('\n');
            const size_t partial = last_line == std::string::npos ? client.in.size() : client.in.size() - last_line - 1;
            if (partial > kMaxLine || client.in.size() > kMaxPending) {
                client.out += "err " + JsonString("a line longer than 64 KiB, or far more sent than answered") + "\n";
                Close(client, "line too long");
                return;
            }
            continue;
        }
        if (got == 0) {
            if (!client.closing) LogInfo("control: connection {} closed", client.id);
            client.dead = true;
        } else if (!WouldBlock()) {
            client.dead = true;
        }
        return;
    }
}

void ControlServer::Answer(Client& client, const Handler& handler) {
    size_t start = 0;
    for (int handled = 0; handled < kLinesPerPoll && !client.closing; ++handled) {
        const size_t end = client.in.find('\n', start);
        if (end == std::string::npos) break;
        std::string_view line(client.in.data() + start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!client.authenticated) {
            constexpr std::string_view kHello = "hello ";
            if (line.starts_with(kHello) && TokenMatches(line.substr(kHello.size()))) {
                client.authenticated = true;
                client.out += "ok " + info_ + "\n";
                LogInfo("control: connection {} signed in", client.id);
            } else {
                client.out += "err " + JsonString("expected hello <token> (the token is in control.json next to pt.log)") + "\n";
                Close(client, "no valid hello");
            }
            continue;
        }
        if (line.empty()) continue;
        Reply reply;
        try {
            reply = handler ? handler(line) : Reply{false, JsonString("no handler")};
        } catch (const std::exception& e) {
            reply = Reply{false, JsonString(e.what())};
        }
        client.out += reply.ok ? "ok " : "err ";
        client.out += reply.json.empty() ? "null" : reply.json;
        client.out += '\n';
    }
    if (!client.closing) client.in.erase(0, start);
}

void ControlServer::Send(Client& client) {
    while (!client.out.empty()) {
        const size_t chunk = std::min<size_t>(client.out.size(), 1u << 20);
#ifdef _WIN32
        const int sent = send(static_cast<SOCKET>(client.socket), client.out.data(), static_cast<int>(chunk), 0);
#else
        const int sent = static_cast<int>(send(static_cast<int>(client.socket), client.out.data(), chunk, MSG_NOSIGNAL));
#endif
        if (sent < 0) {
            if (!WouldBlock()) client.dead = true;
            break;
        }
        client.out.erase(0, static_cast<size_t>(sent));
    }
    if (client.out.size() > kMaxPending) {
        LogWarn("control: connection {} does not read its answers; closed", client.id);
        client.dead = true;
    }
}

/* A closing connection first sends its last answer, then shuts its sending side and reads what the client still sends
   until the client closes or kLingerMs pass: closed with unread input, the socket would reset the connection and the
   client could lose that last answer. */
void ControlServer::Shut(Client& client) {
    if (!client.shut) {
#ifdef _WIN32
        shutdown(static_cast<SOCKET>(client.socket), SD_SEND);
#else
        shutdown(static_cast<int>(client.socket), SHUT_WR);
#endif
        client.shut = true;
        client.shut_ms = NowMs();
    } else if (NowMs() - client.shut_ms > kLingerMs) {
        client.dead = true;
    }
}

void ControlServer::Close(Client& client, std::string_view why) {
    if (!client.closing) LogWarn("control: connection {} closed: {}", client.id, why);
    client.closing = true;
    client.in.clear();
}

bool ControlServer::TokenMatches(std::string_view given) const {
    if (token_.empty() || given.size() != token_.size()) return false;
    unsigned char difference = 0;
    for (size_t i = 0; i < given.size(); ++i) difference |= static_cast<unsigned char>(given[i] ^ token_[i]);
    return difference == 0;
}

std::string ControlServer::NewToken() {
    std::random_device device;
    std::string token;
    for (int i = 0; i < 4; ++i) {
        char part[9];
        std::snprintf(part, sizeof(part), "%08x", static_cast<unsigned>(device()));
        token += part;
    }
    return token;
}

std::string ControlServer::JsonString(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out += '"';
    for (char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
                char escape[8];
                std::snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += escape;
            } else {
                out += c;
            }
        }
    }
    out += '"';
    return out;
}

}
