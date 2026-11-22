// Test-001 Teensy2: listens on UDP port comms::kPort. Every "ping" that
// arrives over Ethernet is answered with a "pong" carrying the same number
// (and Teensy1's send time, unchanged, so Teensy1 can time the round trip).
// Logs each ping, and any pings that went missing on the way. The LED on
// pin 13 toggles on every ping answered.
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/printk.h>

#include <cerrno>
#include <cstdint>

#include "comms.hpp"

LOG_MODULE_REGISTER(teensy2, LOG_LEVEL_INF);

namespace {

constexpr std::uint8_t kMe = 2;
// recv waits this long at most, so link changes still get logged when no
// pings are arriving.
constexpr int kIdleCheckMs = 1000;

static_assert(comms::same(CONFIG_NET_CONFIG_MY_IPV4_ADDR, comms::kTeensy2Ip),
              "Teensy2/prj.conf's CONFIG_NET_CONFIG_MY_IPV4_ADDR must equal "
              "kTeensy2Ip in common/comms.hpp");

const gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

// Logs when the Ethernet link changes.
void check_link(bool& was_up) {
  const bool up = comms::link_up();
  if (up && !was_up) LOG_INF("Ethernet link up");
  if (!up && was_up) LOG_WRN("Ethernet link down: check the cable and the switch");
  was_up = up;
}

}  // namespace

int main() {
  gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

  char mac[18];
  comms::mac_string(mac);
  LOG_INF("Test-001 Teensy2 (ip %s, mac %s): answers pings on UDP port %u",
          comms::kTeensy2Ip, mac, comms::kPort);

  const int sock = comms::open_socket();
  if (sock < 0) {
    LOG_ERR("could not open UDP port %u (errno %d)", comms::kPort, errno);
    return 0;
  }

  bool link_was_up = false;
  if (!comms::link_up()) LOG_INF("waiting for the Ethernet link ...");

  std::uint32_t last_seq = 0;
  for (;;) {
    check_link(link_was_up);
    if (!comms::readable(sock, kIdleCheckMs)) continue;

    std::uint8_t buf[sizeof(comms::Packet) + 1];  // +1 so oversized packets fail parse()
    net_sockaddr_in from{};
    net_socklen_t from_len = sizeof(from);
    const auto n = zsock_recvfrom(sock, buf, sizeof(buf), 0,
                                  reinterpret_cast<net_sockaddr*>(&from), &from_len);
    if (n <= 0) continue;
    const auto ping = comms::parse({buf, static_cast<std::size_t>(n)}, comms::Kind::Ping);
    if (!ping) continue;

    // Answer first, log after, so logging never adds to the round trip.
    comms::Packet pong = *ping;  // keeps seq and sent_cycles
    pong.kind = comms::Kind::Pong;
    pong.from = kMe;
    snprintk(pong.text, sizeof(pong.text), "pong #%u from Teensy2", ping->seq);
    const bool sent = zsock_sendto(sock, &pong, sizeof(pong), 0,
                                   reinterpret_cast<const net_sockaddr*>(&from), from_len) >= 0;
    const int send_errno = errno;
    gpio_pin_toggle_dt(&led);

    char from_ip[NET_IPV4_ADDR_LEN];
    zsock_inet_ntop(NET_AF_INET, &from.sin_addr, from_ip, sizeof(from_ip));

    if (ping->seq < last_seq) {
      LOG_INF("Teensy1 restarted (ping numbers began again at #%u)", ping->seq);
    } else if (last_seq != 0 && ping->seq > last_seq + 1) {
      LOG_WRN("%u ping(s) missing before #%u", ping->seq - last_seq - 1, ping->seq);
    }
    last_seq = ping->seq;

    if (sent) {
      LOG_INF("#%u from %s: \"%s\" -> pong sent", ping->seq, from_ip, ping->text);
    } else {
      LOG_WRN("#%u from %s: pong send failed (errno %d)", ping->seq, from_ip, send_errno);
    }
  }
}
