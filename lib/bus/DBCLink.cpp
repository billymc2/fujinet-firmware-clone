#include "DBCLink.h"

#include <chrono>

#include "../../include/debug.h"

// A large directory scan on the companion outlasts one receive() timeout
static constexpr std::chrono::milliseconds TAGGED_TIMEOUT{2000};

std::unique_ptr<FujiBusPacket> DBCLink::tagged_exchange(FujiBusPacket &packet, uint8_t tag)
{
    if (send(packet).is_error())
        return nullptr;

    auto deadline = std::chrono::steady_clock::now() + TAGGED_TIMEOUT;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto reply = receive();
        if (!reply)
            continue;

        unsigned count = reply->paramCount();
        if (reply->device() != FUJI_DEVICEID::DBC || count == 0 ||
            reply->param(count - 1) != tag)
        {
            Debug_printv("DBC: dropping stale reply to 0x%02x\n",
                         static_cast<unsigned>(packet.command()));
            continue;
        }
        if (reply->command() != CMD::FUJI_ACK)
            return nullptr;
        return reply;
    }

    Debug_printv("DBC: no reply to 0x%02x\n", static_cast<unsigned>(packet.command()));
    return nullptr;
}
