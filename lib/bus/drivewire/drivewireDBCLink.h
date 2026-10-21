#ifdef BUILD_COCO
#ifndef DRIVEWIREDBCLINK_H
#define DRIVEWIREDBCLINK_H

#include "DBCLink.h"

// DBC frames share the serial link with DriveWire traffic. Nothing is sent
// over BoIP or on a board whose pinmap lacks FN_DBC_COMPANION.
class drivewireDBCLink : public DBCLink
{
protected:
    success_is_true send(FujiBusPacket &packet) override;
    std::unique_ptr<FujiBusPacket> receive() override;
};

#endif /* DRIVEWIREDBCLINK_H */
#endif /* BUILD_COCO */
