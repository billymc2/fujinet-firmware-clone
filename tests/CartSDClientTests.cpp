// lib/device/Cartridge/CartSDClient over a fake cart, and DBCLink's tagged_request().
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "device/Cartridge/CartSDClient.h"

struct Answer
{
    fujiCommandID_t command = CMD::FUJI_ACK;
    std::string data;
};

// Answers each request through the wire format, echoing its tag; `stale`
// replies to an earlier tag go out first.
class FakeCart : public DBCLink
{
public:
    std::vector<std::unique_ptr<FujiBusPacket>> requests;
    std::function<Answer(const FujiBusPacket &request)> respond = [](const FujiBusPacket &) {
        return Answer{};
    };
    int stale = 0;

protected:
    success_is_true send(FujiBusPacket &packet) override
    {
        requests.push_back(FujiBusPacket::fromSerialized(packet.serialize()));
        RETURN_SUCCESS_IF(requests.back() != nullptr);
    }

    std::unique_ptr<FujiBusPacket> receive() override
    {
        const FujiBusPacket &request = *requests.back();
        uint8_t tag = static_cast<uint8_t>(request.param(request.paramCount() - 1));
        if (stale > 0)
        {
            stale--;
            return wire(Answer{}, static_cast<uint8_t>(tag - 1));
        }
        return wire(respond(request), tag);
    }

private:
    static std::unique_ptr<FujiBusPacket> wire(const Answer &answer, uint8_t tag)
    {
        FujiBusPacket reply = answer.data.empty()
            ? FujiBusPacket(FUJI_DEVICEID::DBC, answer.command, tag)
            : FujiBusPacket(FUJI_DEVICEID::DBC, answer.command, answer.data, tag);
        return FujiBusPacket::fromSerialized(reply.serialize());
    }
};

TEST_CASE("each request carries a fresh tag as its last param")
{
    FakeCart cart;
    CartSDClient client(cart);

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
    cart.stale = 2;
    cart.respond = [](const FujiBusPacket &) { return Answer{CMD::FUJI_ACK, "x/"}; };
    CartSDClient client(cart);

    auto entry = client.read_dir_entry();
    REQUIRE(entry.has_value());
    CHECK(entry->name == "x");
    CHECK(cart.requests.size() == 1);
}

TEST_CASE("a NAK fails the request")
{
    FakeCart cart;
    cart.respond = [](const FujiBusPacket &) { return Answer{CMD::FUJI_NAK}; };
    CartSDClient client(cart);

    CHECK(client.open_directory("/", nullptr).is_error());
    CHECK_FALSE(client.open_file("/a.rom").has_value());
}

TEST_CASE("open_directory sends path and pattern as NUL-terminated strings")
{
    FakeCart cart;
    CartSDClient client(cart);

    CHECK(client.open_directory("/roms", "*.bin").is_success());
    const auto &data = cart.requests[0]->data();
    REQUIRE(data.has_value());
    CHECK(std::string(data->begin(), data->end()) == std::string("/roms\0*.bin\0", 12));
}

TEST_CASE("read_dir_entry: directories end in '/', 0x7F 0x7F ends the listing")
{
    FakeCart cart;
    std::deque<std::string> listing{std::string("games/\0", 7), std::string("a.rom\0", 6),
                                    "\x7F\x7F"};
    cart.respond = [&](const FujiBusPacket &) {
        Answer a{CMD::FUJI_ACK, listing.front()};
        listing.pop_front();
        return a;
    };
    CartSDClient client(cart);

    auto dir = client.read_dir_entry();
    REQUIRE(dir.has_value());
    CHECK(dir->name == "games");
    CHECK(dir->is_dir);

    auto file = client.read_dir_entry();
    REQUIRE(file.has_value());
    CHECK(file->name == "a.rom");
    CHECK_FALSE(file->is_dir);

    CHECK_FALSE(client.read_dir_entry().has_value());
}

TEST_CASE("open_file sends stream 2 and the path; reply is handle then u32le size")
{
    FakeCart cart;
    cart.respond = [](const FujiBusPacket &) {
        return Answer{CMD::FUJI_ACK, std::string("\x05\x34\x12\x01\x00", 5)};
    };
    CartSDClient client(cart);

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
        size_t n = offset >= 600 ? 0 : std::min<size_t>(want, 600 - offset);
        return Answer{CMD::FUJI_ACK, std::string(n, static_cast<char>(offset / 256))};
    };
    CartSDClient client(cart);

    std::vector<uint8_t> buf(1000);
    CHECK(client.read_file(3, 0, buf.data(), buf.size()) == 600);

    REQUIRE(cart.requests.size() == 3);
    const uint32_t offsets[] = {0, 256, 512};
    for (size_t i = 0; i < 3; i++)
    {
        const FujiBusPacket &read = *cart.requests[i];
        CHECK(read.command() == CMD::NET_READ);
        REQUIRE(read.paramCount() == 4);
        CHECK(read.param(0) == 3);
        CHECK(read.param(1) == offsets[i]);
        CHECK(read.param(2) == 256);
    }
    CHECK(buf[255] == 0);
    CHECK(buf[256] == 1);
    CHECK(buf[599] == 2);
}
