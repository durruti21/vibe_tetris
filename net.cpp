// net.cpp - minimal non-blocking TCP implementation for LAN play.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "net.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cstdio>

#pragma comment(lib, "ws2_32.lib")

namespace tetris {
namespace {

SOCKET ToSocket(uintptr_t handle) { return static_cast<SOCKET>(handle); }
uintptr_t FromSocket(SOCKET s) { return static_cast<uintptr_t>(s); }
constexpr uintptr_t kNoSocket = ~static_cast<uintptr_t>(0);

std::string ErrorText(int code) {
    switch (code) {
        case WSAECONNREFUSED:
            return "Connection refused - is the host game running?";
        case WSAETIMEDOUT:
            return "Connection timed out";
        case WSAENETUNREACH:
            return "Network unreachable";
        case WSAEHOSTUNREACH:
            return "Host unreachable";
        case WSAEADDRINUSE:
            return "Port 7777 is already in use";
        case WSAECONNRESET:
            return "Connection reset by peer";
        default:
            break;
    }
    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "Network error %d", code);
    return buf;
}

bool SetNonBlocking(SOCKET s) {
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
}

void SetLowLatency(SOCKET s) {
    BOOL yes = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes),
               sizeof(yes));
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&yes),
               sizeof(yes));
}

std::string AddressOf(const sockaddr_in& addr) {
    char ip[INET_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip))) {
        return {};
    }
    return ip;
}

}  // namespace

Net::Net() {
    WSADATA wsa{};
    winsockReady_ = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    if (!winsockReady_) {
        failed_ = true;
        error_ = "Could not initialize WinSock";
    }
}

Net::~Net() {
    Close();
    if (winsockReady_) WSACleanup();
}

bool Net::Listening() const {
    return listen_ != kNoSocket;
}

bool Net::Connected() const {
    return socket_ != kNoSocket && !connecting_;
}

bool Net::Host(uint16_t port) {
    Close();
    if (!winsockReady_) {
        failed_ = true;
        error_ = "WinSock is not available";
        return false;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        Fail(ErrorText(WSAGetLastError()));
        return false;
    }

    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
               sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        const int code = WSAGetLastError();
        closesocket(s);
        Fail(ErrorText(code));
        return false;
    }
    if (listen(s, 1) == SOCKET_ERROR) {
        const int code = WSAGetLastError();
        closesocket(s);
        Fail(ErrorText(code));
        return false;
    }
    if (!SetNonBlocking(s)) {
        const int code = WSAGetLastError();
        closesocket(s);
        Fail(ErrorText(code));
        return false;
    }

    listen_ = FromSocket(s);
    failed_ = false;
    error_.clear();
    return true;
}

bool Net::Connect(const std::string& host, uint16_t port) {
    Close();
    if (!winsockReady_) {
        failed_ = true;
        error_ = "WinSock is not available";
        return false;
    }
    if (host.empty()) {
        failed_ = true;
        error_ = "Enter the host address";
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char service[16]{};
    std::snprintf(service, sizeof(service), "%u", static_cast<unsigned>(port));

    addrinfo* result = nullptr;
    const int gai = getaddrinfo(host.c_str(), service, &hints, &result);
    if (gai != 0 || !result) {
        failed_ = true;
        error_ = "Invalid host address";
        return false;
    }

    SOCKET s = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (s == INVALID_SOCKET) {
        const int code = WSAGetLastError();
        freeaddrinfo(result);
        Fail(ErrorText(code));
        return false;
    }
    if (!SetNonBlocking(s)) {
        const int code = WSAGetLastError();
        closesocket(s);
        freeaddrinfo(result);
        Fail(ErrorText(code));
        return false;
    }

    const int rc = connect(s, result->ai_addr, static_cast<int>(result->ai_addrlen));
    if (rc == 0) {
        SetLowLatency(s);
        socket_ = FromSocket(s);
        peer_ = host;
        connecting_ = false;
        failed_ = false;
        error_.clear();
        freeaddrinfo(result);
        return true;
    }

    const int code = WSAGetLastError();
    freeaddrinfo(result);
    if (code != WSAEWOULDBLOCK && code != WSAEINPROGRESS && code != WSAEALREADY) {
        closesocket(s);
        Fail(ErrorText(code));
        return false;
    }

    socket_ = FromSocket(s);
    connecting_ = true;
    peer_ = host;
    failed_ = false;
    error_.clear();
    return true;
}

void Net::Poll() {
    if (!winsockReady_) return;

    // Host: accept one peer, then stop listening.
    if (listen_ != kNoSocket) {
        sockaddr_in addr{};
        int len = sizeof(addr);
        SOCKET accepted = accept(ToSocket(listen_), reinterpret_cast<sockaddr*>(&addr), &len);
        if (accepted != INVALID_SOCKET) {
            SOCKET listening = ToSocket(listen_);
            closesocket(listening);
            listen_ = kNoSocket;

            if (!SetNonBlocking(accepted)) {
                closesocket(accepted);
                Fail(ErrorText(WSAGetLastError()));
                return;
            }
            SetLowLatency(accepted);
            socket_ = FromSocket(accepted);
            connecting_ = false;
            failed_ = false;
            error_.clear();
            peer_ = AddressOf(addr);
        } else {
            const int code = WSAGetLastError();
            if (code != WSAEWOULDBLOCK && code != WSAEINPROGRESS) {
                Fail(ErrorText(code));
                return;
            }
        }
    }

    // Client: wait for the non-blocking connect to finish.
    if (connecting_ && socket_ != kNoSocket) {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(ToSocket(socket_), &writable);
        timeval tv{0, 0};
        const int ready = select(0, nullptr, &writable, nullptr, &tv);
        if (ready > 0 && FD_ISSET(ToSocket(socket_), &writable)) {
            int err = 0;
            int len = sizeof(err);
            getsockopt(ToSocket(socket_), SOL_SOCKET, SO_ERROR,
                       reinterpret_cast<char*>(&err), &len);
            if (err != 0) {
                const std::string text = ErrorText(err);
                CloseSocket();
                failed_ = true;
                error_ = text;
                connecting_ = false;
            } else {
                SetLowLatency(ToSocket(socket_));
                connecting_ = false;
                failed_ = false;
                error_.clear();
            }
        } else if (ready == SOCKET_ERROR) {
            Fail(ErrorText(WSAGetLastError()));
        }
    }

    if (socket_ == kNoSocket || connecting_) return;

    if (!Flush()) return;

    char buf[4096];
    for (;;) {
        const int n = recv(ToSocket(socket_), buf, sizeof(buf), 0);
        if (n > 0) {
            inbuf_.append(buf, static_cast<size_t>(n));
            if (inbuf_.size() > (1u << 20)) {
                Fail("Incoming message buffer overflow");
                return;
            }
        } else if (n == 0) {
            Fail("Connection closed by peer");
            return;
        } else {
            const int code = WSAGetLastError();
            if (code == WSAEWOULDBLOCK) break;
            Fail(ErrorText(code));
            return;
        }
    }
}

void Net::Close() {
    CloseSocket();
    connecting_ = false;
    failed_ = false;
    error_.clear();
    peer_.clear();
    inbuf_.clear();
    outbuf_.clear();
}

void Net::CloseSocket() {
    if (listen_ != kNoSocket) {
        closesocket(ToSocket(listen_));
        listen_ = kNoSocket;
    }
    if (socket_ != kNoSocket) {
        closesocket(ToSocket(socket_));
        socket_ = kNoSocket;
    }
}

void Net::Fail(const std::string& message) {
    CloseSocket();
    connecting_ = false;
    failed_ = true;
    error_ = message;
}

bool Net::SendLine(const std::string& line) {
    if (socket_ == kNoSocket || connecting_) return false;
    outbuf_ += line;
    if (outbuf_.empty() || outbuf_.back() != '\n') outbuf_ += '\n';
    if (outbuf_.size() > (1u << 20)) {
        Fail("Outgoing message buffer overflow");
        return false;
    }
    return Flush();
}

bool Net::ReceiveLine(std::string& line) {
    const size_t pos = inbuf_.find('\n');
    if (pos == std::string::npos) return false;
    line.assign(inbuf_, 0, pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    inbuf_.erase(0, pos + 1);
    return true;
}

bool Net::Flush() {
    while (!outbuf_.empty() && socket_ != kNoSocket) {
        const int n = send(ToSocket(socket_), outbuf_.data(),
                           static_cast<int>(outbuf_.size()), 0);
        if (n == SOCKET_ERROR) {
            const int code = WSAGetLastError();
            if (code == WSAEWOULDBLOCK) return true;
            Fail(ErrorText(code));
            return false;
        }
        if (n <= 0) {
            Fail("Connection closed while sending");
            return false;
        }
        outbuf_.erase(0, static_cast<size_t>(n));
    }
    return true;
}

std::vector<std::string> Net::LocalAddresses() {
    std::vector<std::string> addresses;
    char hostname[256]{};
    if (gethostname(hostname, sizeof(hostname)) != 0) return addresses;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(hostname, nullptr, &hints, &result) != 0) return addresses;

    for (addrinfo* it = result; it; it = it->ai_next) {
        if (!it->ai_addr || it->ai_addrlen < sizeof(sockaddr_in)) continue;
        auto* addr = reinterpret_cast<sockaddr_in*>(it->ai_addr);
        char ip[INET_ADDRSTRLEN]{};
        if (!inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip))) continue;
        const std::string text = ip;
        if (text == "127.0.0.1" || text == "0.0.0.0") continue;
        if (std::find(addresses.begin(), addresses.end(), text) == addresses.end()) {
            addresses.push_back(text);
        }
    }
    freeaddrinfo(result);
    return addresses;
}

}  // namespace tetris
