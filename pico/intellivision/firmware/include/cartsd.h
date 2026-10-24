#ifndef CARTSD_H
#define CARTSD_H

#include <stdbool.h>
#include "fujibus.h"

// Answers the ESP32's DBC directory and file requests
// (lib/device/Cartridge/CartSDClient.cpp) from our own SD card. Returns false
// for frames that aren't one, e.g. the ROM push.
bool cartsd_inbound_handler(const fb_reply_t *req);

#endif /* CARTSD_H */
