#ifdef BUILD_RS232

#include "rs232DBCLink.h"

#include "rs232.h"

success_is_true rs232DBCLink::send(FujiBusPacket &packet)
{
    SYSTEM_BUS.writeBusPacket(packet);
    RETURN_SUCCESS_AS_TRUE();
}

std::unique_ptr<FujiBusPacket> rs232DBCLink::receive()
{
    return SYSTEM_BUS.readBusPacket();
}

#endif /* BUILD_RS232 */
