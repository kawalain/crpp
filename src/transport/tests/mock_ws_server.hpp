#pragma once
// A tiny WebSocket server on 127.0.0.1 for transport tests (Windows only).
// Not a general implementation: server frames are unmasked with payloads of at
// most 65535 bytes, client frames are unmasked on read.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mock {

inline std::string base64(const unsigned char* d, std::size_t n) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  for (std::size_t i = 0; i < n; i += 3) {
    const unsigned v = static_cast<unsigned>(d[i]) << 16 |
                       (i + 1 < n ? static_cast<unsigned>(d[i + 1]) << 8 : 0u) |
                       (i + 2 < n ? static_cast<unsigned>(d[i + 2]) : 0u);
    o += t[v >> 18 & 63];
    o += t[v >> 12 & 63];
    o += i + 1 < n ? t[v >> 6 & 63] : '=';
    o += i + 2 < n ? t[v & 63] : '=';
  }
  return o;
}

inline std::string accept_key(const std::string& key) {
  const std::string s = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0);
  BCRYPT_HASH_HANDLE hash = nullptr;
  BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0);
  BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(s.data())), static_cast<ULONG>(s.size()), 0);
  unsigned char out[20];
  BCryptFinishHash(hash, out, 20, 0);
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(alg, 0);
  return base64(out, 20);
}

struct Conn {
  SOCKET sock = INVALID_SOCKET;
  std::string request;

  bool send_all(const void* p, std::size_t n) {
    const char* c = static_cast<const char*>(p);
    while (n > 0) {
      const int k = ::send(sock, c, static_cast<int>(n), 0);
      if (k <= 0) return false;
      c += k;
      n -= static_cast<std::size_t>(k);
    }
    return true;
  }
  bool recv_all(void* p, std::size_t n) {
    char* c = static_cast<char*>(p);
    while (n > 0) {
      const int k = ::recv(sock, c, static_cast<int>(n), 0);
      if (k <= 0) return false;
      c += k;
      n -= static_cast<std::size_t>(k);
    }
    return true;
  }

  // Value of a request header, or "<absent>".
  std::string header(const std::string& name) const {
    std::size_t p = request.find("\r\n" + name + ":");
    if (p == std::string::npos) return "<absent>";
    p += 3 + name.size();
    while (p < request.size() && request[p] == ' ') ++p;
    return request.substr(p, request.find("\r\n", p) - p);
  }
  std::string request_line() const { return request.substr(0, request.find("\r\n")); }

  // Completes the handshake; protocol is echoed unless it is empty.
  void accept_ws(const std::string& protocol = "cloudroom-v2") {
    std::string r = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                    "Sec-WebSocket-Accept: " + accept_key(header("Sec-WebSocket-Key")) + "\r\n";
    if (!protocol.empty()) r += "Sec-WebSocket-Protocol: " + protocol + "\r\n";
    r += "\r\n";
    send_all(r.data(), r.size());
  }
  void reply(int status) {
    const std::string r = "HTTP/1.1 " + std::to_string(status) + " X\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    send_all(r.data(), r.size());
  }
  bool send_frame(unsigned char op, const std::string& payload) {
    std::string f;
    f += static_cast<char>(0x80 | op);
    if (payload.size() < 126) {
      f += static_cast<char>(payload.size());
    } else {
      f += static_cast<char>(126);
      f += static_cast<char>(payload.size() >> 8);
      f += static_cast<char>(payload.size() & 0xFF);
    }
    f += payload;
    return send_all(f.data(), f.size());
  }
  bool read_frame(unsigned char& op, std::string& payload) {
    unsigned char h[2];
    if (!recv_all(h, 2)) return false;
    op = h[0] & 0x0F;
    std::size_t len = h[1] & 0x7F;
    if (len == 126) {
      unsigned char e[2];
      if (!recv_all(e, 2)) return false;
      len = static_cast<std::size_t>(e[0]) << 8 | e[1];
    }
    unsigned char mask[4] = {};
    const bool masked = (h[1] & 0x80) != 0;
    if (masked && !recv_all(mask, 4)) return false;
    payload.assign(len, '\0');
    if (len > 0 && !recv_all(payload.data(), len)) return false;
    if (masked) for (std::size_t i = 0; i < len; ++i) payload[i] = static_cast<char>(payload[i] ^ mask[i & 3]);
    return true;
  }
  void drop() {
    if (sock != INVALID_SOCKET) {
      closesocket(sock);
      sock = INVALID_SOCKET;
    }
  }
};

class Server {
 public:
  using Script = std::move_only_function<void(Conn&) const>;

  explicit Server(Script script) : script_(std::move(script)) {
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    listener_ = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    bind(listener_, reinterpret_cast<sockaddr*>(&a), sizeof a);
    listen(listener_, 8);
    int len = sizeof a;
    getsockname(listener_, reinterpret_cast<sockaddr*>(&a), &len);
    port_ = ntohs(a.sin_port);
    acceptor_ = std::thread([this] { accept_loop(); });
  }
  ~Server() {
    stopping_ = true;
    closesocket(listener_);
    {
      std::lock_guard lock(mutex_);
      for (SOCKET s : live_) shutdown(s, SD_BOTH);
    }
    acceptor_.join();
    for (auto& t : workers_) t.join();
  }
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  [[nodiscard]] int port() const { return port_; }
  [[nodiscard]] int connections() const { return connections_; }

 private:
  void accept_loop() {
    while (!stopping_) {
      SOCKET s = accept(listener_, nullptr, nullptr);
      if (s == INVALID_SOCKET) return;
      ++connections_;
      {
        std::lock_guard lock(mutex_);
        live_.push_back(s);
      }
      workers_.emplace_back([this, s] {
        Conn c;
        c.sock = s;
        char ch;
        while (c.request.find("\r\n\r\n") == std::string::npos && ::recv(s, &ch, 1, 0) == 1) c.request += ch;
        if (c.request.find("\r\n\r\n") != std::string::npos) script_(c);
        c.drop();
      });
    }
  }

  Script script_;
  SOCKET listener_ = INVALID_SOCKET;
  int port_ = 0;
  std::atomic<bool> stopping_{false};
  std::atomic<int> connections_{0};
  std::thread acceptor_;
  std::vector<std::thread> workers_;
  std::mutex mutex_;
  std::vector<SOCKET> live_;
};

}  // namespace mock
