# Homeboard

> A tiny self-hosted display for your home.

<img width="1036" height="696" alt="image" src="https://github.com/user-attachments/assets/7fd983f0-c86e-4a47-b152-4608f0f8906b" />

<img width="960" height="1280" alt="WhatsApp Image 2026-08-22 at 12 18 28 AM" src="https://github.com/user-attachments/assets/7832ebaa-e9bc-4462-9bca-5aefaaf790c4" />


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

The current firmware is a standalone ESP32 dashboard. It joins the configured home Wi-Fi, advertises `milluboard.local` over mDNS, and serves a small web UI and JSON API. If the home network is unavailable, it creates a fallback access point named **MilluBoard**. Display support is planned but not yet implemented.

### Try it

1. Create a local `.env` file (already ignored by Git):

   ```sh
   WIFI_SSID=your-network-name
   WIFI_PASSWORD=your-network-password
   API_TOKEN=use-a-long-random-token-here
   ```

2. Upload the first firmware over USB:

   ```sh
   ./scripts/flash.py usb
   ```

   USB is only required for bootstrap or recovery. After this firmware is running on the board, use OTA flashing:

   ```sh
   ./scripts/flash.py ota
   ```

3. From any device on the same home network, open `http://milluboard.local`.

Shell environment variables with the same names override `.env`, which is useful for CI or temporary networks. The build stops with an error if any required value is missing. Credentials are compiled into the firmware but are never stored in tracked project files.

If the ESP32 cannot join the configured network within 15 seconds, join its **MilluBoard** fallback network using password `milluboard`, then open `http://192.168.4.1`. Use `./scripts/flash.py ota --host 192.168.4.1` to flash OTA while connected to the fallback network.

The dashboard reports live chip status, counts unique dashboard clients active within the last 30 seconds, stores a temporary message, and toggles the common GPIO 2 onboard LED. API endpoints require a bearer token. Interactive Scalar documentation is available at `http://milluboard.local/docs`, with the OpenAPI document at `/openapi.json`.

```text
GET  /api/v1/status
POST /api/v1/display/message   (text/plain body, 1-80 characters)
POST /api/v1/led
```

Send the token with every API request:

```sh
curl -H "Authorization: Bearer $API_TOKEN" \
  http://milluboard.local/api/v1/status
```

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
