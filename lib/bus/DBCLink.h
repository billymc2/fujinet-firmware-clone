#ifndef DBCLINK_H
#define DBCLINK_H

#include <cstdint>
#include <memory>
#include <utility>

#include "global_types.h"
#include "rs232/FujiBusPacket.h"

// Our end of the link to the companion MCU (FUJI_DEVICEID::DBC). Knows only
// requests and replies, not what they carry. Each bus that has a companion
// implements send() and receive().
class DBCLink
{
public:
    virtual ~DBCLink() = default;

    // The companion's reply, ACK or not; nullptr if none came back.
    template <typename... Args>
    std::unique_ptr<FujiBusPacket> request(fujiCommandID_t command, Args &&...args)
    {
        FujiBusPacket packet(FUJI_DEVICEID::DBC, command, std::forward<Args>(args)...);
        if (send(packet).is_error())
            return nullptr;
        return receive();
    }

    // As request(), with a tag as the last param that the companion echoes as
    // the last param of its reply, so a reply we gave up on is dropped rather
    // than taken for the next one. nullptr unless the companion ACKs.
    template <typename... Args>
    std::unique_ptr<FujiBusPacket> tagged_request(fujiCommandID_t command, Args &&...args)
    {
        uint8_t tag = ++_tag;
        FujiBusPacket packet(FUJI_DEVICEID::DBC, command, std::forward<Args>(args)..., tag);
        return tagged_exchange(packet, tag);
    }

protected:
    virtual success_is_true send(FujiBusPacket &packet) = 0;
    // The next packet from the companion; nullptr on timeout.
    virtual std::unique_ptr<FujiBusPacket> receive() = 0;

private:
    uint8_t _tag = 0;

    std::unique_ptr<FujiBusPacket> tagged_exchange(FujiBusPacket &packet, uint8_t tag);
};

#endif /* DBCLINK_H */
