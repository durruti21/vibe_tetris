// net.h - tiny non-blocking TCP helper used by the LAN versus mode.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tetris {

inline constexpr uint16_t kDefaultPort = 7777;

// A single-connection TCP socket. Works both as a listening host and as a
// connecting client. All calls are non-blocking after Host()/Connect().
class Net {
public:
    Net();
    ~Net();

    Net(const Net&) = delete;
    Net& operator=(const Net&) = delete;

    // Start listening for one incoming player.
    bool Host(uint16_t port);
    // Begin connecting to a host. Returns false if the address is invalid.
    bool Connect(const std::string& host, uint16_t port);
    // Service the socket: accept a peer, finish connecting, drain received data.
    void Poll();
    void Close();

    bool Listening() const;
    bool Connected() const;
    bool Failed() const { return failed_; }
    const std::string& Error() const { return error_; }
    const std::string& Peer() const { return peer_; }

    // Newline-delimited messages. SendLine queues the line and flushes as much
    // as the socket accepts; ReceiveLine returns one complete buffered line.
    bool SendLine(const std::string& line);
    bool ReceiveLine(std::string& line);

    // All non-loopback IPv4 addresses of this machine.
    static std::vector<std::string> LocalAddresses();

private:
    static constexpr uintptr_t kNoSocket = ~static_cast<uintptr_t>(0);

    void Fail(const std::string& message);
    bool Flush();
    void CloseSocket();

    uintptr_t listen_ = kNoSocket;
    uintptr_t socket_ = kNoSocket;
    bool connecting_ = false;
    bool failed_ = false;
    bool winsockReady_ = false;
    std::string error_;
    std::string peer_;
    std::string inbuf_;
    std::string outbuf_;
};

}  // namespace tetris
