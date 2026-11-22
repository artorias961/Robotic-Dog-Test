# Test-001: Ethernet between two Teensy 4.1 boards

Two Teensy 4.1 boards talk over Ethernet through a switch, using Zephyr RTOS (v4.4) and C++20.

- **Teensy1** sends a numbered **ping** to Teensy2 every 500 ms. It logs how long each **pong** took to come back, and prints a summary every 10 pings.
- **Teensy2** answers every ping with a pong. It logs each ping it gets, and any that went missing.

On both boards, the LED on pin 13 toggles on every message that gets through.

```
 Teensy1 (192.168.1.101) ── ping #5 ──▶  switch  ── ping #5 ──▶ Teensy2 (192.168.1.102)
                         ◀── pong #5 ──          ◀── pong #5 ──
```

Messages are UDP packets on port 6000. Nothing in this test touches the robot's firmware. The addresses (.101, .102) stay clear of the robot's own addresses (master .1, legs .11 to .14), so the test can share the robot's switch.

## What you need

- 2 Teensy 4.1 boards, each with the [PJRC Ethernet kit](https://www.pjrc.com/store/ethernet_kit.html) (the jack plus the 6-pin header soldered to the Teensy's Ethernet pads)
- 1 Ethernet switch, powered on, and 2 Ethernet cables
- 2 micro-USB cables: these power the boards and carry the log output

A PC doesn't need to be on the switch.

## Files

| Path | What it is |
|---|---|
| `common/comms.hpp` | What both boards share: the IPs, the port, the packet layout, and socket helpers |
| `Teensy1/` | The Zephyr app for the board that sends pings |
| `Teensy2/` | The Zephyr app for the board that answers them |

Each board folder holds `CMakeLists.txt`, `prj.conf` (settings, including that board's IP), `app.overlay` (log output over USB), and `src/main.cpp`.

## Build

In PowerShell, set up Zephyr once per terminal:

```powershell
C:\zephyr\.venv\Scripts\Activate.ps1
$env:ZEPHYR_BASE = "C:\zephyr\zephyr"
cd C:\GitRepo\Robotic-Dog\submodule\Test\Teensy\Test-001
```

Then build both boards:

```powershell
west build -b teensy41 -s Teensy1 -d Teensy1\build
west build -b teensy41 -s Teensy2 -d Teensy2\build
```

After you edit a `prj.conf` or `app.overlay`, add `-p always` to rebuild from scratch.

Each build makes `Teensy1\build\zephyr\zephyr.hex` or `Teensy2\build\zephyr\zephyr.hex`.

## Flash

1. Open the Teensy Loader app: `C:\GitRepo\Robotic-Dog\tools\teensy_loader\teensy.exe`.
2. Plug in the first Teensy. In the app, choose **File > Open HEX File** and pick `Teensy1\build\zephyr\zephyr.hex`.
3. Press the button on the Teensy. If **Auto** mode is off, click **Program**, then **Reboot**.
4. Label that board "Teensy1". Repeat with the second board and `Teensy2\build\zephyr\zephyr.hex`.

## Run it

1. Plug each Teensy's Ethernet jack into the switch.
2. Open a serial monitor for each board, in two terminals. The legs firmware's monitor works, and it lists the ports to pick from:

   ```powershell
   python C:\GitRepo\Robotic-Dog\submodule\Legs\scripts\05_serial_monitor.py --wait 10
   ```

   Any serial terminal works too. The log goes out over the Teensy's USB port, so the baud rate doesn't matter.

Teensy1 should print:

```
<inf> teensy1: Test-001 Teensy1 (ip 192.168.1.101, mac 02:04:9f:..): pings Teensy2 at 192.168.1.102:6000 every 500 ms
<inf> teensy1: Ethernet link up
<inf> teensy1: #1 pong in 412 us: "pong #1 from Teensy2"
<inf> teensy1: #2 pong in 398 us: "pong #2 from Teensy2"
...
<inf> teensy1: summary: 10 pings sent, 10 pongs back, 0 lost
```

Teensy2 should print:

```
<inf> teensy2: Test-001 Teensy2 (ip 192.168.1.102, mac 02:04:9f:..): answers pings on UDP port 6000
<inf> teensy2: Ethernet link up
<inf> teensy2: #1 from 192.168.1.101: "ping #1 from Teensy1" -> pong sent
```

The times above are made-up examples. Your times will differ, and so will the last three bytes of each MAC, which come from the chip's unique ID.

The first lines appear right after boot, so they're easy to miss if the monitor opens late. To see them, start the monitor first, then plug in the board's USB cable.

### Optional: ping the boards from a PC

Connect the PC to the same switch and give its Ethernet adapter a fixed address, for example IP `192.168.1.50` with mask `255.255.255.0`. Zephyr answers ping on its own:

```powershell
ping 192.168.1.101
ping 192.168.1.102
```

## When it doesn't work

| What you see | What to check |
|---|---|
| No `Ethernet link up` line | The cable, and that the switch shows a link light for that port. Check the Ethernet kit's header solder joints. |
| Teensy1 shows `no pong within 100 ms` every time | Teensy2 must be flashed with the Teensy2 build and show `Ethernet link up`. Both boards must be on the same switch. |
| A few pongs lost right after a board starts | That's normal. The other board may still be booting, or its link may still be coming up. |
| Teensy2 shows `ping(s) missing` | Packets were lost between the boards: check the cables and the switch. |
| `static assertion failed ... must equal kTeensy1Ip` | A board's IP in its `prj.conf` doesn't match `common/comms.hpp`. Make them match. |

## Changing things

- **IP addresses:** change `kTeensy1Ip` and `kTeensy2Ip` in `common/comms.hpp`, and `CONFIG_NET_CONFIG_MY_IPV4_ADDR` in each board's `prj.conf`. The build fails if a board's two values disagree.
- **Ping rate or timeout:** change `kPeriodMs` and `kReplyTimeoutMs` at the top of `Teensy1/src/main.cpp`.
- **Packet contents:** change `Packet` in `common/comms.hpp`, then rebuild and flash both boards, since they must match.
