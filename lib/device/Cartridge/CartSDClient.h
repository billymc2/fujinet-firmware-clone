#ifndef CARTSDCLIENT_H
#define CARTSDCLIENT_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "FujiBusPacket.h"
#include "global_types.h"

class SystemBusBase;

// Client for the cartridge MCU's SD card server: directories and files over
// the bus's companion link (FUJI_DEVICEID::DBC).
class CartSDClient
{
public:
    struct DirEntry
    {
        std::string name;
        bool is_dir;
    };

    struct FileInfo
    {
        uint8_t handle;
        uint32_t size;
    };

private:
    SystemBusBase &_bus;
    uint8_t _tag = 0;

    // A request with a tag as the last param, which the cart echoes as the
    // last param of its reply, so a reply we gave up on is dropped rather than
    // taken for the next one. nullptr unless the cart ACKs.
    template <typename... Args>
    std::unique_ptr<FujiBusPacket> request(fujiCommandID_t command, Args &&...args)
    {
        uint8_t tag = ++_tag;
        FujiBusPacket packet(FUJI_DEVICEID::DBC, command, std::forward<Args>(args)..., tag);
        return exchange(packet, tag);
    }

    std::unique_ptr<FujiBusPacket> exchange(FujiBusPacket &packet, uint8_t tag);

public:
    explicit CartSDClient(SystemBusBase &bus) : _bus(bus) {}

    success_is_true open_directory(const char *path, const char *pattern);
    // nullopt at the end of the listing or on error
    std::optional<DirEntry> read_dir_entry();
    void close_directory();
    // nullopt on error
    std::optional<uint16_t> directory_position();
    success_is_true set_directory_position(uint16_t pos);

    std::optional<FileInfo> open_file(const char *path);
    // Bytes read; short at EOF or on error
    size_t read_file(uint8_t handle, uint32_t offset, void *buf, size_t len);
    void close_file(uint8_t handle);
};

#endif /* CARTSDCLIENT_H */
