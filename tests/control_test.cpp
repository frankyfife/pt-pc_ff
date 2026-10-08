#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "engine/platform/control_server.h"
#include "game/control_commands.h"

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
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

/* The control channel without a game (docs/control.md): the server over a real loopback connection (greeting, hello,
   lines, limits) and the command parsing and JSON the game's commands are built from. */

namespace {

int failures = 0;

void Check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    failures += ok ? 0 : 1;
}

using pt::ControlServer;
namespace control = pt::game::control;

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kNoSocket = INVALID_SOCKET;
void CloseSocket(Socket s) { closesocket(s); }
void NonBlocking(Socket s) {
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
}
#else
using Socket = int;
constexpr Socket kNoSocket = -1;
void CloseSocket(Socket s) { close(s); }
void NonBlocking(Socket s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
#endif

/* A client of the server under test. Both run on this thread: every wait polls the server too. */
struct Client {
    Client() = default;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Socket s = kNoSocket;
    std::string in;
    bool closed = false;

    bool Connect(int port) {
        s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<uint16_t>(port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            return false;
        }
        NonBlocking(s);
        return true;
    }
    /* sends all of text, letting the server read while the socket buffer is full */
    void Send(ControlServer& server, const ControlServer::Handler& handler, std::string_view text) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        size_t at = 0;
        while (at < text.size() && std::chrono::steady_clock::now() < deadline) {
            const int sent = static_cast<int>(send(s, text.data() + at, static_cast<int>(text.size() - at), 0));
            if (sent > 0) {
                at += static_cast<size_t>(sent);
            } else {
                server.Poll(handler);
                Pump();
            }
        }
    }
    void Pump() {
        char buffer[4096];
        for (;;) {
            const int got = static_cast<int>(recv(s, buffer, sizeof(buffer), 0));
            if (got > 0) {
                in.append(buffer, static_cast<size_t>(got));
                continue;
            }
            if (got == 0) closed = true;
            return;
        }
    }
    /* the next line, or "<none>" after the seconds (or once the server has closed the connection) */
    std::string Line(ControlServer& server, const ControlServer::Handler& handler, int seconds = 2) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        while (std::chrono::steady_clock::now() < deadline) {
            server.Poll(handler);
            Pump();
            const size_t end = in.find('\n');
            if (end != std::string::npos) {
                std::string line = in.substr(0, end);
                in.erase(0, end + 1);
                return line;
            }
            if (closed) break;
        }
        return "<none>";
    }
    bool ClosedByServer(ControlServer& server, const ControlServer::Handler& handler) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!closed && std::chrono::steady_clock::now() < deadline) {
            server.Poll(handler);
            Pump();
        }
        return closed;
    }
    ~Client() {
        if (s != kNoSocket) CloseSocket(s);
    }
};

void TestJson() {
    Check(control::JsonNumber(1.0) == "1", "json: whole number");
    Check(control::JsonNumber(-2.25) == "-2.25", "json: fraction");
    Check(control::JsonNumber(0.1f) == "0.10000000149011612", "json: a float widened keeps its exact value");
    Check(control::JsonNumber(std::nan("")) == "null" && control::JsonNumber(INFINITY) == "null", "json: not finite is null");
    Check(ControlServer::JsonString("a\"b\\c\nd\x01") == "\"a\\\"b\\\\c\\nd\\u0001\"", "json: string escapes");

    control::JsonWriter out;
    out.BeginObject(nullptr);
    out.Number("x", 1.5);
    out.Bool("on", true);
    out.String("name", "f040");
    out.BeginArray("demos");
    out.String(nullptr, "a");
    out.String(nullptr, "b");
    out.End();
    out.BeginArray("empty");
    out.End();
    out.Vector("eye", glm::vec3(0.0f, 1.5f, -2.0f));
    out.End();
    Check(out.Text() == R"({"x":1.5,"on":true,"name":"f040","demos":["a","b"],"empty":[],"eye":{"x":0,"y":1.5,"z":-2}})",
          "json: writer nests objects and arrays: " + out.Text());

    control::JsonWriter list;
    list.BeginArray(nullptr);
    list.BeginObject(nullptr);
    list.Number("id", 1);
    list.End();
    list.BeginObject(nullptr);
    list.Number("id", 2);
    list.End();
    list.End();
    Check(list.Text() == R"([{"id":1},{"id":2}])", "json: an array of objects: " + list.Text());

    const std::string info = control::Info("1.0.1");
    Check(info.starts_with(R"({"protocol":1,"game":"pt-port","version":"1.0.1","commands":["help",)"), "json: hello info");
}

void TestParsing() {
    const auto words = control::Words("  place 1 2\t3  ");
    Check(words.size() == 4 && words[0] == "place" && words[3] == "3", "words: split on spaces and tabs");
    Check(control::Words("").empty() && control::Words("   ").empty(), "words: an empty line has none");

    Check(control::ParseNumber("1.5") == 1.5f && control::ParseNumber("+2") == 2.0f && control::ParseNumber("-0.25") == -0.25f,
          "number: plain, plus sign, negative");
    Check(control::ParseNumber("1e3") == 1000.0f, "number: exponent");
    bool refused = true;
    for (const char* bad : {"", "abc", "1.5x", "nan", "inf", "-inf", "1e10", "0x10", "--1"}) {
        refused = refused && !control::ParseNumber(bad);
    }
    Check(refused, "number: refuses text, not finite, too large");

    Check(control::ParseSwitch("on") == true && control::ParseSwitch("1") == true && control::ParseSwitch("true") == true, "switch: on");
    Check(control::ParseSwitch("off") == false && control::ParseSwitch("0") == false && control::ParseSwitch("false") == false, "switch: off");
    Check(!control::ParseSwitch("yes") && !control::ParseSwitch(""), "switch: anything else is refused");

    const auto off = control::ParseNumberOrOff("off");
    const auto two = control::ParseNumberOrOff("2");
    Check(off && !*off && two && *two && **two == 2.0f && !control::ParseNumberOrOff("x"), "number or off");

    bool listed = true;
    for (const char* name : {"status", "place <x> <y> <z> [yaw]", "hover <height>|here|off", "camera off", "entities <stage> [prefix|*] [limit]"}) {
        bool found = false;
        for (const std::string& command : control::CommandList()) found = found || command == name;
        listed = listed && found;
    }
    Check(listed, "help lists the commands");
}

void TestServer() {
    ControlServer server;
    const std::string token = ControlServer::NewToken();
    Check(token.size() == 32 && token.find_first_not_of("0123456789abcdef") == std::string::npos, "token: 32 hex digits");
    Check(ControlServer::NewToken() != token, "token: a new one each time");
    if (!server.Start(0, token, R"({"protocol":1})")) {
        Check(false, "server: listens on 127.0.0.1");
        return;
    }
    Check(server.Listening() && server.Port() > 0, "server: listens on 127.0.0.1, port " + std::to_string(server.Port()));

    std::vector<std::string> seen;
    const ControlServer::Handler handler = [&](std::string_view line) {
        seen.emplace_back(line);
        if (line == "fail") return ControlServer::Reply{false, ControlServer::JsonString("failed")};
        return ControlServer::Reply{true, ControlServer::JsonString(line)};
    };

    {
        Client c;
        Check(c.Connect(server.Port()), "bad token: connects");
        Check(c.Line(server, handler) == "pt-control 1", "bad token: greeting");
        c.Send(server, handler, "hello " + std::string(token.size(), '0') + "\n");
        Check(c.Line(server, handler).starts_with("err "), "bad token: refused");
        Check(c.ClosedByServer(server, handler), "bad token: connection closed");
    }
    {
        Client c;
        c.Connect(server.Port());
        c.Line(server, handler);
        c.Send(server, handler, "status\n");
        Check(c.Line(server, handler).starts_with("err "), "no hello: a command first is refused");
        Check(c.ClosedByServer(server, handler) && seen.empty(), "no hello: closed, the handler never called");
    }
    {
        Client c;
        c.Connect(server.Port());
        c.Line(server, handler);
        const auto start = std::chrono::steady_clock::now();
        const std::string answer = c.Line(server, handler, 8);
        const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        Check(answer.starts_with("err ") && waited > 4.5 && c.ClosedByServer(server, handler), "no hello: closed after 5 seconds");
    }
    {
        Client c;
        c.Connect(server.Port());
        c.Line(server, handler);
        c.Send(server, handler, "hello " + token + "\n");
        Check(c.Line(server, handler) == R"(ok {"protocol":1})", "hello: answered with the info");
        c.Send(server, handler, "first\r\nsecond\n\nfail\n");
        const std::string a = c.Line(server, handler);
        const std::string b = c.Line(server, handler);
        const std::string f = c.Line(server, handler);
        Check(a == R"(ok "first")" && b == R"(ok "second")", "lines: answered in order, CR LF and LF");
        Check(f == R"(err "failed")", "lines: a failed command answers err");
        Check(seen.size() == 3, "lines: empty lines are skipped");
        c.Send(server, handler, "par");
        server.Poll(handler);
        c.Send(server, handler, "tial\n");
        Check(c.Line(server, handler) == R"(ok "partial")", "lines: a line in two pieces");

        std::vector<Client> more(4);
        int refused = 0;
        for (Client& other : more) {
            other.Connect(server.Port());
            if (other.Line(server, handler).starts_with("err ")) ++refused;
        }
        Check(refused == 1, "clients: the fifth is refused (" + std::to_string(refused) + ")");
    }
    {
        Client c;
        c.Connect(server.Port());
        c.Line(server, handler);
        c.Send(server, handler, "hello " + token + "\n");
        c.Line(server, handler);
        c.Send(server, handler, std::string(ControlServer::kMaxLine + 100, 'x'));
        Check(c.Line(server, handler).starts_with("err "), "limits: a line longer than 64 KiB is refused");
        Check(c.ClosedByServer(server, handler), "limits: and the connection closed");
    }
    server.Stop();
    Check(!server.Listening() && server.Clients() == 0, "server: stops");
}

}

int main() {
#ifdef _WIN32
    WSADATA data{};
    WSAStartup(MAKEWORD(2, 2), &data);
#else
    /* the client keeps sending after the server closed on it (the long line) */
    std::signal(SIGPIPE, SIG_IGN);
#endif
    TestJson();
    TestParsing();
    TestServer();
    std::printf("%s: %d failed\n", failures == 0 ? "all passed" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
