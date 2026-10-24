// lib/device/Cartridge/CartSDClient and lib/FileSystem/fnFsCartSD over a fake
// cart that serves an SD card from memory.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <string>
#include <vector>

#include "FakeCart.h"
#include "device/Cartridge/CartSDClient.h"
#include "fnFsCartSD.h"

TEST_CASE("each request carries a fresh tag as its last param")
{
    FakeCart cart;
    cart.listing = {{"a", false}, {"b", false}, {"c", false}, {"d", false}, {"e", false},
                    {"f", false}, {"g", false}};
    CartSDClient client(cart.bus());

    CHECK(client.set_directory_position(7).is_success());
    client.close_directory();

    REQUIRE(cart.requests.size() == 2);
    const FujiBusPacket &seek = *cart.requests[0];
    CHECK(seek.device() == FUJI_DEVICEID::DBC);
    CHECK(seek.command() == CMD::FUJI_SET_DIRECTORY_POSITION);
    REQUIRE(seek.paramCount() == 2);
    CHECK(seek.param(0) == 7);
    CHECK(seek.param(1) != cart.requests[1]->param(0));
}

TEST_CASE("a reply to an earlier tag is dropped")
{
    FakeCart cart;
    cart.listing = {{"x", true}};
    cart.stale = 2;
    CartSDClient client(cart.bus());

    auto entry = client.read_dir_entry();
    REQUIRE(entry.has_value());
    CHECK(entry->name == "x");
    CHECK(entry->is_dir);
    CHECK(cart.requests.size() == 1);
}

TEST_CASE("a NAK fails the request")
{
    FakeCart cart;
    cart.respond = [](const FujiBusPacket &) { return Answer{CMD::FUJI_NAK}; };
    CartSDClient client(cart.bus());

    CHECK(client.open_directory("/", nullptr).is_error());
    CHECK_FALSE(client.open_file("/a.rom").has_value());
}

TEST_CASE("open_directory sends path and pattern as NUL-terminated strings")
{
    FakeCart cart;
    cart.respond = [](const FujiBusPacket &) { return Answer{}; };
    CartSDClient client(cart.bus());

    CHECK(client.open_directory("/roms", "*.bin").is_success());
    const auto &data = cart.requests[0]->data();
    REQUIRE(data.has_value());
    CHECK(std::string(data->begin(), data->end()) == std::string("/roms\0*.bin\0", 12));
}

TEST_CASE("open_file sends stream 2 and the path; reply is handle then u32le size")
{
    FakeCart cart;
    cart.respond = [](const FujiBusPacket &) {
        return Answer{CMD::FUJI_ACK, std::string("\x05\x34\x12\x01\x00", 5)};
    };
    CartSDClient client(cart.bus());

    auto info = client.open_file("/a.rom");
    REQUIRE(info.has_value());
    CHECK(info->handle == 5);
    CHECK(info->size == 0x011234);

    const auto &data = cart.requests[0]->data();
    REQUIRE(data.has_value());
    CHECK(std::string(data->begin(), data->end()) == std::string("\x02/a.rom", 7));
}

TEST_CASE("read_file asks for 256-byte chunks and stops at a short one")
{
    FakeCart cart;
    cart.respond = [](const FujiBusPacket &request) {
        uint32_t offset = request.param(1);
        uint16_t want = static_cast<uint16_t>(request.param(2));
        size_t count = offset >= 600 ? 0 : std::min<size_t>(want, 600 - offset);
        return Answer{CMD::FUJI_ACK, std::string(count, static_cast<char>(offset / 256))};
    };
    CartSDClient client(cart.bus());

    std::vector<uint8_t> buf(1000);
    CHECK(client.read_file(3, 0, buf.data(), buf.size()) == 600);

    REQUIRE(cart.requests.size() == 3);
    const uint32_t offsets[] = {0, 256, 512};
    for (size_t idx = 0; idx < 3; idx++)
    {
        const FujiBusPacket &read = *cart.requests[idx];
        CHECK(read.command() == CMD::NET_READ);
        REQUIRE(read.paramCount() == 4);
        CHECK(read.param(0) == 3);
        CHECK(read.param(1) == offsets[idx]);
        CHECK(read.param(2) == 256);
    }
    CHECK(buf[255] == 0);
    CHECK(buf[256] == 1);
    CHECK(buf[599] == 2);
}

TEST_CASE("the filesystem lists names and directory flags, and seeks")
{
    FakeCart cart;
    cart.listing = {{"GAMES", true}, {"Astrosmash.rom", false}};
    CartSDClient client(cart.bus());
    FileSystemCartSD cart_fs(client);

    CHECK(cart_fs.dir_open("/", nullptr, 0).is_success());
    fsdir_entry_t *entry = cart_fs.dir_read();
    REQUIRE(entry != nullptr);
    CHECK(std::string(entry->filename) == "GAMES");
    CHECK(entry->isDir);
    entry = cart_fs.dir_read();
    REQUIRE(entry != nullptr);
    CHECK(std::string(entry->filename) == "Astrosmash.rom");
    CHECK_FALSE(entry->isDir);
    CHECK(cart_fs.dir_read() == nullptr);

    CHECK(cart_fs.dir_seek(1).is_success());
    CHECK(cart_fs.dir_tell() == 1);
    cart_fs.dir_close();
    CHECK(cart.dir_closes == 1);
}

TEST_CASE("nothing reaches the cart for a directory that is not open")
{
    FakeCart cart;
    CartSDClient client(cart.bus());
    FileSystemCartSD cart_fs(client);

    CHECK(cart_fs.dir_open("/missing", nullptr, 0).is_error());
    CHECK(cart_fs.dir_read() == nullptr);
    CHECK(cart_fs.dir_tell() == FNFS_INVALID_DIRPOS);
    CHECK(cart_fs.dir_seek(0).is_error());
    cart_fs.dir_close();
    CHECK(cart.requests.size() == 1);
}

TEST_CASE("files read, seek and close through their handle")
{
    FakeCart cart;
    cart.files["/a.rom"] = "0123456789";
    CartSDClient client(cart.bus());
    FileSystemCartSD cart_fs(client);

    FileHandler *handler = cart_fs.filehandler_open("/a.rom", "rb");
    REQUIRE(handler != nullptr);
    char buf[16] = {};
    CHECK(handler->read(buf, 1, 4) == 4);
    CHECK(std::string(buf, 4) == "0123");
    CHECK(handler->seek(-2, SEEK_END) == 0);
    CHECK(handler->read(buf, 1, sizeof(buf)) == 2);
    CHECK(std::string(buf, 2) == "89");
    CHECK(handler->eof() == 1);
    CHECK(cart.open_handles.size() == 1);
    handler->close();
    CHECK(cart.open_handles.empty());
}

TEST_CASE("a handler deleted without close() still releases its handle")
{
    FakeCart cart;
    cart.files["/a.rom"] = "x";
    CartSDClient client(cart.bus());
    FileSystemCartSD cart_fs(client);

    FileHandler *handler = cart_fs.filehandler_open("/a.rom", "rb");
    REQUIRE(handler != nullptr);
    delete handler;
    CHECK(cart.open_handles.empty());
}

TEST_CASE("write modes are refused and exists() leaves no handle open")
{
    FakeCart cart;
    cart.files["/a.rom"] = "x";
    CartSDClient client(cart.bus());
    FileSystemCartSD cart_fs(client);

    CHECK(cart_fs.filehandler_open("/a.rom", "r+b") == nullptr);
    CHECK(cart_fs.filehandler_open("/a.rom", "wb") == nullptr);
    CHECK(cart_fs.exists("/a.rom"));
    CHECK_FALSE(cart_fs.exists("/b.rom"));
    CHECK(cart.open_handles.empty());
}
