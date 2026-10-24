#include "CartSDClient.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "bus.h"
#include "../../include/debug.h"

// Bounded by the cart's 384-byte fujibus packet buffer
static constexpr uint16_t READ_CHUNK = 256;
// Longest name the cart lists, plus '/' and NUL
static constexpr uint16_t DIR_ENTRY_MAX = 257;
// NET_OPEN stream ids 0 and 1 are the Cartridge's ROM push
static constexpr char STREAM_FILE = 2;
// A large directory scan on the cart outlasts one readFujiBusPacket() timeout
static constexpr std::chrono::milliseconds REPLY_TIMEOUT{2000};

std::unique_ptr<FujiBusPacket> CartSDClient::exchange(FujiBusPacket &packet, uint8_t tag)
{
    if (_bus.writeFujiBusPacket(packet).is_error())
        return nullptr;

    auto deadline = std::chrono::steady_clock::now() + REPLY_TIMEOUT;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto reply = _bus.readFujiBusPacket();
        if (!reply)
            continue;

        unsigned count = reply->paramCount();
        if (reply->device() != FUJI_DEVICEID::DBC || count == 0 ||
            reply->param(count - 1) != tag)
        {
            Debug_printv("cart SD: dropping stale reply to 0x%02x\n",
                         static_cast<unsigned>(packet.command()));
            continue;
        }
        if (reply->command() != CMD::FUJI_ACK)
            return nullptr;
        return reply;
    }

    Debug_printv("cart SD: no reply to 0x%02x\n", static_cast<unsigned>(packet.command()));
    return nullptr;
}

success_is_true CartSDClient::open_directory(const char *path, const char *pattern)
{
    std::string payload(path != nullptr ? path : "/");
    payload.push_back('\0');
    if (pattern != nullptr && pattern[0] != '\0')
    {
        payload += pattern;
        payload.push_back('\0');
    }
    RETURN_SUCCESS_IF(request(CMD::FUJI_OPEN_DIRECTORY, payload) != nullptr);
}

std::optional<CartSDClient::DirEntry> CartSDClient::read_dir_entry()
{
    auto reply = request(CMD::FUJI_READ_DIR_ENTRY, DIR_ENTRY_MAX, uint8_t{0});
    if (!reply || !reply->data())
        return std::nullopt;

    // Same reply as fujicmd_read_directory_entry()
    const ByteBuffer &data = *reply->data();
    if (data.size() >= 2 && data[0] == 0x7F && data[1] == 0x7F)
        return std::nullopt;

    DirEntry entry;
    entry.name.assign(data.begin(), std::find(data.begin(), data.end(), 0));
    entry.is_dir = !entry.name.empty() && entry.name.back() == '/';
    if (entry.is_dir)
        entry.name.pop_back();
    if (entry.name.empty())
        return std::nullopt;
    return entry;
}

void CartSDClient::close_directory()
{
    request(CMD::FUJI_CLOSE_DIRECTORY);
}

std::optional<uint16_t> CartSDClient::directory_position()
{
    auto reply = request(CMD::FUJI_GET_DIRECTORY_POSITION);
    u16le_t pos;
    if (!reply || !reply->data() || reply->data()->size() < sizeof(pos))
        return std::nullopt;
    memcpy(&pos, reply->data()->data(), sizeof(pos));
    return pos;
}

success_is_true CartSDClient::set_directory_position(uint16_t pos)
{
    RETURN_SUCCESS_IF(request(CMD::FUJI_SET_DIRECTORY_POSITION, pos) != nullptr);
}

std::optional<CartSDClient::FileInfo> CartSDClient::open_file(const char *path)
{
    std::string payload(1, STREAM_FILE);
    payload += path;
    auto reply = request(CMD::NET_OPEN, payload);

    struct
    {
        uint8_t handle;
        u32le_t size;
    } __attribute__((packed)) info;
    if (!reply || !reply->data() || reply->data()->size() < sizeof(info))
        return std::nullopt;
    memcpy(&info, reply->data()->data(), sizeof(info));
    return FileInfo{info.handle, info.size};
}

size_t CartSDClient::read_file(uint8_t handle, uint32_t offset, void *buf, size_t len)
{
    uint8_t *out = static_cast<uint8_t *>(buf);
    size_t got = 0;
    while (got < len)
    {
        uint16_t want = static_cast<uint16_t>(std::min<size_t>(len - got, READ_CHUNK));
        uint32_t read_offset = offset + static_cast<uint32_t>(got);
        auto reply = request(CMD::NET_READ, handle, read_offset, want);
        if (!reply || !reply->data())
            break;

        size_t copied = std::min<size_t>(reply->data()->size(), want);
        memcpy(out + got, reply->data()->data(), copied);
        got += copied;
        if (copied < want)
            break;
    }
    return got;
}

void CartSDClient::close_file(uint8_t handle)
{
    request(CMD::NET_CLOSE, handle);
}
