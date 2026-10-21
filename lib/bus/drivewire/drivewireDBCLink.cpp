#ifdef BUILD_COCO

#include "drivewireDBCLink.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "drivewire.h"

success_is_true drivewireDBCLink::send(FujiBusPacket &packet)
{
#ifdef FN_DBC_COMPANION
    if (SYSTEM_BUS.isBoIP())
    {
        Debug_printv("DBC needs a real pico; not supported over BoIP");
        RETURN_ERROR_AS_FALSE();
    }

    SYSTEM_BUS.writeBusPacket(packet);
    RETURN_SUCCESS_AS_TRUE();
#else
    (void)packet;
    Debug_printv("DBC not supported on this FujiNet hardware.");
    RETURN_ERROR_AS_FALSE();
#endif
}

std::unique_ptr<FujiBusPacket> drivewireDBCLink::receive()
{
    return SYSTEM_BUS.readBusPacket();
}

#endif /* BUILD_COCO */
