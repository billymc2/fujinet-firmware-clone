# lib/device/Cartridge

The cartridge on the far side of a companion MCU: mounted ROMs are pushed into it over the bus's
`DBCLink` as DBC `NET_OPEN`, `NET_WRITE` blocks and `NET_CLOSE`.

## Layout

| File | Defines |
|---|---|
| `Cartridge.h`, `Cartridge.cpp` | `Cartridge`: the link, `Source`, `accepted()`, `write_blocks()` |
| `StreamCartridge.h`, `StreamCartridge.cpp` | `StreamCartridge : Cartridge` |
| `SegmentCartridge.h`, `SegmentCartridge.cpp` | `SegmentCartridge : Cartridge` |
| `CartSDClient.h`, `CartSDClient.cpp` | `CartSDClient : RemoteStorage`, the client for the cart's SD card server (`pico/intellivision` `cartsd.c`) |

## How it works

- A `Source` is a size plus a `read(offset, buffer, length)` callback, so the class never sees a
  bus's `MediaTypeROM`.
- Each subclass speaks one companion firmware's `NET_OPEN`:

| Class | `NET_OPEN` | Block | Companion | Used by |
|---|---|---|---|---|
| `StreamCartridge` | payload `[stream id][size u32le]`; map on stream 1 first, image on 0; `NET_CLOSE` carries `0x01` to abort | 512 | the `pico/<console>` carts | `rs232Fuji` |
| `SegmentCartridge` | `u16` param: load segment (0); any reply counts as taken | 256 | `pico/fujiversal` | `drivewireFuji` |

## How it fits

- The bus supplies the link: `rs232DBCLink` in [lib/bus/rs232/](../../bus/rs232/),
  `drivewireDBCLink` in [lib/bus/drivewire/](../../bus/drivewire/).
- The Fuji's `mount_media()` override loads a mounted ROM; a failed push fails the mount.
- Covered by `cartridge_tests` (frames byte for byte against a fake link), `cartsdclient_tests`
  (frames and tags against a fake cart) and the
  `no_build_ifdefs_in_fujidevice` ctest.

## Build

ESP: globbed by `src/CMakeLists.txt` into every target. PC: `FUJINET_TARGET=RS232` lists
`Cartridge`, `StreamCartridge` and `CartSDClient`; `COCO` lists `Cartridge` and `SegmentCartridge`.
