#ifdef BUILD_RS232
#ifndef CARTSDFUJI_H
#define CARTSDFUJI_H

#include "rs232Fuji.h"
#include "CartSDClient.h"
#include "fnFsCartSD.h"
#include "fnFsSD.h"

// A board whose SD card is wired to the cartridge MCU, not to us: the "SD"
// host reads it through the bus's companion link (FUJI_DEVICEID::DBC).
class cartSDFuji : public rs232Fuji
{
private:
    CartSDClient _cart_sd_client{SYSTEM_BUS};
    FileSystemCartSD _cart_sd{_cart_sd_client};

public:
    void setup() override
    {
        // Unless a card came up on our own pins after all
        if (!fnSDFAT.running())
            for (auto &host : _fnHosts)
                host.set_local_fs(&_cart_sd);
        rs232Fuji::setup();
    }
};

#endif /* CARTSDFUJI_H */
#endif /* BUILD_RS232 */
