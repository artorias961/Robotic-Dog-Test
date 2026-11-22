// Test-001 Teensy1: every kPeriodMs, sends a numbered "ping" to Teensy2 over
// Ethernet (UDP, through the switch), waits for Teensy2's "pong", and logs
// the round-trip time. Every kSummaryEvery pings it logs how many came back.
// The LED on pin 13 toggles on every pong.
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/printk.h>

#include <cerrno>
#include <cstdint>
#include <optional>

#include "comms.hpp"

LOG_MODULE_REGISTER(teensy1, LOG_LEVEL_INF);

namespace {

constexpr std::uint8_t kMe = 1;
constexpr int kPeriodMs = 500;        // one ping every half second
constexpr int kReplyTimeoutMs = 100;  // a pong later than this counts as lost
constexpr std::uint32_t kSummaryEvery = 10;

static_assert(comms::same(CONFIG_NET_CONFIG_MY_IPV4_ADDR, comms::kTeensy1Ip),
              "Teensy1/prj.conf's CONFIG_NET_CONFIG_MY_IPV4_ADDR must equal "
              "kTeensy1Ip in common/comms.hpp");

const gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

struct Pong {
  comms::Packet packet;
  std::uint32_t rtt_us;  // ping sent -> pong received
};

// Waits up to kReplyTimeoutMs for the pong to ping number `seq`. Stray
// packets and late pongs to older pings are skipped.
std::optional<Pong> wait_for_pong(int sock, std::uint32_t seq) {
  const std::int64_t deadline = k_uptime_get() + kReplyTimeoutMs;
  for (;;) {
    const std::int64_t left = deadline - k_uptime_get();
    if (left <= 0 || !comms::readable(sock, static_cast<int>(left))) return std::nullopt;

    std::uint8_t buf[sizeof(comms::Packet) + 1];  // +1 so oversized packets fail parse()
    const auto n = zsock_recv(sock, buf, sizeof(buf), 0);
    const std::uint32_t now = k_cycle_get_32();
    if (n <= 0) continue;

    const auto p = comms::parse({buf, static_cast<std::size_t>(n)}, comms::Kind::Pong);
    if (!p || p->seq != seq) continue;
    // Unsigned subtraction stays correct when the 32-bit counter wraps.
    return Pong{*p, k_cyc_to_us_floor32(now - p->sent_cycles)};
  }
}

// Logs when the Ethernet link changes. Returns whether it is up now.
bool check_link(bool& was_up) {
  const bool up = comms::link_up();
  if (up && !was_up) LOG_INF("Ethernet link up");
  if (!up && was_up) LOG_WRN("Ethernet link down: check the cable and the switch");
  was_up = up;
  return up;
}

}  // namespace

int main() {
  gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

  char mac[18];
  comms::mac_string(mac);
  LOG_INF("Test-001 Teensy1 (ip %s, mac %s): pings Teensy2 at %s:%u every %d ms",
          comms::kTeensy1Ip, mac, comms::kTeensy2Ip, comms::kPort, kPeriodMs);

  const int sock = comms::open_socket();
  if (sock < 0) {
    LOG_ERR("could not open UDP port %u (errno %d)", comms::kPort, errno);
    return 0;
  }
  const net_sockaddr_in teensy2 = comms::endpoint(comms::kTeensy2Ip);

  bool link_was_up = false;
  if (!comms::link_up()) LOG_INF("waiting for the Ethernet link ...");

  std::uint32_t sent = 0;
  std::uint32_t pongs = 0;
  for (;;) {
    const std::int64_t start = k_uptime_get();

    if (check_link(link_was_up)) {
      comms::Packet ping{};
      ping.magic = comms::kMagic;
      ping.seq = ++sent;
      ping.kind = comms::Kind::Ping;
      ping.from = kMe;
      snprintk(ping.text, sizeof(ping.text), "ping #%u from Teensy1", ping.seq);
      ping.sent_cycles = k_cycle_get_32();  // last, right before sending

      if (zsock_sendto(sock, &ping, sizeof(ping), 0,
                       reinterpret_cast<const net_sockaddr*>(&teensy2), sizeof(teensy2)) < 0) {
        LOG_WRN("#%u send failed (errno %d)", ping.seq, errno);
      } else if (const auto pong = wait_for_pong(sock, ping.seq)) {
        ++pongs;
        gpio_pin_toggle_dt(&led);
        LOG_INF("#%u pong in %u us: \"%s\"", ping.seq, pong->rtt_us, pong->packet.text);
      } else {
        LOG_WRN("#%u no pong within %d ms", ping.seq, kReplyTimeoutMs);
      }

      if (sent % kSummaryEvery == 0)
        LOG_INF("summary: %u pings sent, %u pongs back, %u lost", sent, pongs, sent - pongs);
    }

    // Keep a steady period no matter how long the pong took.
    const std::int64_t elapsed = k_uptime_get() - start;
    if (elapsed < kPeriodMs) k_msleep(static_cast<std::int32_t>(kPeriodMs - elapsed));
  }
}
