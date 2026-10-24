# lib/device/Cartridge

The cartridge behind a companion MCU. On a cart with its own SD card, `CartSDClient` browses and
reads that card over the companion link.

## Layout

| File | Defines |
|---|---|
| `CartSDClient.h`, `CartSDClient.cpp` | `CartSDClient`, the client for the cart's SD card server (`pico/intellivision` `cartsd.c`) |

## How it works

- `CartSDClient` sends the Fuji's own directory commands, and `NET_OPEN` (stream id 2),
  `NET_READ` and `NET_CLOSE` for files, each with a sequence tag as its last param. The cart
  echoes the tag as the last param of its reply, so a reply that arrives after we gave up on
  its request is dropped instead of being taken for the next one.

## How it fits

- Not a bus device: the companion is `FUJI_DEVICEID::DBC` and always speaks FujiBus, whatever the
  host bus. `CartSDClient` calls `writeFujiBusPacket()` and `readFujiBusPacket()` on
  `SystemBusBase` in [lib/bus/](../../bus/bus.h) itself, since it may read past stale replies.
- `cartSDFuji` in [lib/device/rs232/](../rs232/) wraps a `CartSDClient` in a
  `FileSystemCartSD` ([lib/FileSystem/](../../FileSystem/)) to serve the "SD" host. A ROM on
  that card mounts like any other: the Fuji reads it through the host and pushes it back.
- Covered by `cartsd_tests` (frames, tags and `FileSystemCartSD` against a fake cart).

## Build

Unguarded. ESP: globbed by `src/CMakeLists.txt` into every target. PC: in the common source list.
