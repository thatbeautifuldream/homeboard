# Homeboard

> A tiny self-hosted display for your home.

Homeboard aims to turn an ESP32 and an inexpensive LED matrix into a network-connected physical status board. It will eventually be controlled through a web interface and HTTP API served directly by the ESP32, with no separate server required.

Possible messages include:

```text
BUILD PASSED
SERVER DOWN
MEETING 10M
22°C
03:42
```

## Architecture

```text
Browser / API / Scripts
          │
          ▼
       ESP32
          │
          ▼
    MAX7219 Display
```

The ESP32 will eventually be responsible for:

```text
Wi-Fi
  │
  ├── mDNS
  │
  ├── Web UI
  │
  ├── HTTP API
  │
  └── Display Controller
            │
            ▼
         MAX7219
```

The personal device name is **MilluBoard**. A future networking milestone will advertise it over mDNS as `milluboard.local`, making it available at `http://milluboard.local`. Networking is not implemented yet.

## Project status

**Early development.** The current firmware only starts serial communication and prints a startup message. Wi-Fi, mDNS, the web interface, API, and display support are planned but not yet implemented.

## Hardware

- ESP32 development board
- MAX7219 4-in-1 32×8 LED matrix
- USB data cable
- Jumper wires

## Development

The firmware uses the Arduino framework with PlatformIO. To compile it without uploading:

```sh
pio run
```
