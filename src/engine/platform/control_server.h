#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace pt {

/* The control channel (docs/control.md): a TCP server on 127.0.0.1 that a tool such as P.T. Playground connects to. Off
   unless asked for (--control, [extras] control in pt.ini). It never blocks: Poll() runs once per frame from the main
   loop, accepts, reads what has arrived, answers every complete line through the handler and sends what it can.

   A line protocol. The server greets with "pt-control 1"; the first line a client sends must be "hello <token>" with the
   token the game wrote to control.json next to pt.log, or the connection is closed. Every further line is one command;
   the answer is one line, "ok <json>" or "err <json string>". The server only carries lines: what a command may do is
   up to the handler, and the game's handler (src/game/control_commands.cpp) knows a fixed set of commands and runs no
   code it is sent. */
class ControlServer {
public:
    static constexpr int kProtocol = 1;
    static constexpr int kDefaultPort = 27510;
    static constexpr size_t kMaxClients = 4;
    static constexpr size_t kMaxLine = 64u << 10;
    static constexpr size_t kMaxPending = 16u << 20;
    static constexpr int kLinesPerPoll = 256;
    static constexpr uint64_t kLingerMs = 2000;
    static constexpr uint64_t kHelloMs = 5000;

    struct Reply {
        bool ok = true;
        std::string json = "null";
    };
    using Handler = std::function<Reply(std::string_view line)>;

    ControlServer() = default;
    ~ControlServer();
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    /* Listens on 127.0.0.1:port (0 lets the system choose; Port() then says which). info is the JSON object a good
       hello is answered with. */
    bool Start(int port, std::string token, std::string info);
    void Stop();
    bool Listening() const { return listen_ != kNoSocket; }
    int Port() const { return port_; }
    size_t Clients() const { return clients_.size(); }
    void Poll(const Handler& handler);

    static std::string NewToken();
    static std::string JsonString(std::string_view text);

private:
    static constexpr uintptr_t kNoSocket = ~uintptr_t(0);
    struct Client {
        uintptr_t socket = kNoSocket;
        std::string in;
        std::string out;
        bool authenticated = false;
        uint64_t opened_ms = 0;
        bool closing = false;
        bool shut = false;
        uint64_t shut_ms = 0;
        bool dead = false;
        uint32_t id = 0;
    };
    void Accept();
    void Receive(Client& client);
    void Answer(Client& client, const Handler& handler);
    void Send(Client& client);
    void Close(Client& client, std::string_view why);
    void Shut(Client& client);
    bool TokenMatches(std::string_view given) const;

    uintptr_t listen_ = kNoSocket;
    int port_ = 0;
    std::string token_;
    std::string info_ = "{}";
    std::vector<Client> clients_;
    uint32_t next_id_ = 1;
};

}
