#pragma once
// Test-001: what both Teensys must agree on - their IP addresses, the UDP
// port, and the packet - plus small socket helpers. Teensy1 and Teensy2 both
// include this file, so the two boards cannot drift apart.
//
// Uses Zephyr 4.4's namespaced network names (net_sockaddr_in, NET_AF_INET,
// net_htons, ...). The old POSIX-style names still work through Zephyr's
// compatibility mode, but that mode is going away.
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/printk.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>

namespace comms {

// ---- addresses ---------------------------------------------------------------
// Static IPs, no DHCP. .101 and .102 stay clear of the robot's own addresses
// (master .1, legs .11-.14), so this test can share the robot's switch.
// Each board's own IP is also set in its prj.conf; main.cpp fails the build if
// the two disagree.
inline constexpr const char* kTeensy1Ip = "192.168.1.101";
inline constexpr const char* kTeensy2Ip = "192.168.1.102";
inline constexpr std::uint16_t kPort = 6000;  // both boards send from and listen on this port

// ---- packet ------------------------------------------------------------------
inline constexpr std::uint32_t kMagic = 0x54303031;  // "T001": ignore anything else on the port

enum class Kind : std::uint8_t { Ping = 1, Pong = 2 };

// Sent as raw bytes. Both ends are the same chip and compiler, so the layout
// and byte order match; the static_asserts catch an accidental change.
struct Packet {
  std::uint32_t magic;        // kMagic
  std::uint32_t seq;          // ping number; the pong carries the same number back
  std::uint32_t sent_cycles;  // Teensy1's cycle counter at send; Teensy2 echoes it unchanged
  Kind kind;
  std::uint8_t from;          // board number: 1 or 2
  char text[42];              // readable message, always null-terminated
};
static_assert(std::is_trivially_copyable_v<Packet>);
static_assert(sizeof(Packet) == 56, "packet layout changed: rebuild and flash both boards");

// A received datagram as a Packet, if it is exactly one Packet of the wanted
// kind with the right magic.
inline std::optional<Packet> parse(std::span<const std::uint8_t> raw, Kind want) {
  if (raw.size() != sizeof(Packet)) return std::nullopt;
  Packet p;
  std::memcpy(&p, raw.data(), sizeof(p));
  if (p.magic != kMagic || p.kind != want) return std::nullopt;
  p.text[sizeof(p.text) - 1] = '\0';  // never trust the sender's terminator
  return p;
}

// ---- sockets -----------------------------------------------------------------
inline net_sockaddr_in endpoint(const char* ip) {
  net_sockaddr_in a{};
  a.sin_family = NET_AF_INET;
  a.sin_port = net_htons(kPort);
  zsock_inet_pton(NET_AF_INET, ip, &a.sin_addr);
  return a;
}

// UDP socket bound to kPort on this board, or -1.
inline int open_socket() {
  const int s = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
  if (s < 0) return -1;
  net_sockaddr_in local{};
  local.sin_family = NET_AF_INET;
  local.sin_addr.s_addr = NET_INADDR_ANY;
  local.sin_port = net_htons(kPort);
  if (zsock_bind(s, reinterpret_cast<net_sockaddr*>(&local), sizeof(local)) < 0) {
    zsock_close(s);
    return -1;
  }
  return s;
}

// Waits up to timeout_ms for something to read on sock.
inline bool readable(int sock, int timeout_ms) {
  zsock_pollfd pfd{.fd = sock, .events = ZSOCK_POLLIN, .revents = 0};
  return zsock_poll(&pfd, 1, timeout_ms) > 0 && (pfd.revents & ZSOCK_POLLIN);
}

// ---- link and identity -----------------------------------------------------------
// True while the Ethernet cable is plugged in and the switch port is up.
inline bool link_up() { return net_if_is_up(net_if_get_default()); }

// This board's MAC address, "aa:bb:cc:dd:ee:ff". Zephyr makes it from
// 02:04:9f plus three bytes of the chip's unique ID (nxp,unique-mac in the
// teensy41 board files), so each Teensy gets its own.
inline void mac_string(char (&out)[18]) {
  const net_linkaddr* l = net_if_get_link_addr(net_if_get_default());
  if (l == nullptr || l->len < 6) {
    snprintk(out, sizeof(out), "unknown");
    return;
  }
  snprintk(out, sizeof(out), "%02x:%02x:%02x:%02x:%02x:%02x", l->addr[0], l->addr[1],
           l->addr[2], l->addr[3], l->addr[4], l->addr[5]);
}

// Compile-time string compare, for checking prj.conf's IP against this file.
constexpr bool same(const char* a, const char* b) {
  while (*a != '\0' && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

}  // namespace comms
