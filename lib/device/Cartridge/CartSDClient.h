#ifndef CARTSDCLIENT_H
#define CARTSDCLIENT_H

#include "DBCLink.h"
#include "fnFsRemote.h"

// Client for the cartridge MCU's SD card server: directories and files over
// the bus's DBCLink.
class CartSDClient : public RemoteStorage
{
private:
    DBCLink &_link;

public:
    explicit CartSDClient(DBCLink &link) : _link(link) {}

    success_is_true open_directory(const char *path, const char *pattern) override;
    std::optional<DirEntry> read_dir_entry() override;
    void close_directory() override;
    uint16_t directory_position() override;
    success_is_true set_directory_position(uint16_t pos) override;

    std::optional<FileInfo> open_file(const char *path) override;
    size_t read_file(uint8_t handle, uint32_t offset, void *buf, size_t len) override;
    void close_file(uint8_t handle) override;
};

#endif /* CARTSDCLIENT_H */
