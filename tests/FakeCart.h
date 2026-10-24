// A cart that serves an SD card from memory, for CartSDTests.cpp. Built
// without the platform define, so bus.h stays out of the tests that use it.
#ifndef FAKECART_H
#define FAKECART_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "FujiBusPacket.h"

class SystemBusBase;

struct Answer
{
    fujiCommandID_t command = CMD::FUJI_ACK;
    std::string data;
};

// Answers each request the way cartsd.c does, echoing its tag; `stale`
// replies to an earlier tag go out first. Set `respond` to answer by hand.
class FakeCart
{
public:
    std::vector<std::pair<std::string, bool>> listing;
    std::map<std::string, std::string> files;
    std::set<uint8_t> open_handles;
    int dir_closes = 0;
    int stale = 0;
    std::vector<std::unique_ptr<FujiBusPacket>> requests;
    std::function<Answer(const FujiBusPacket &request)> respond;

    FakeCart();
    ~FakeCart();

    SystemBusBase &bus();

private:
    class Bus;
    std::unique_ptr<Bus> _bus;
    size_t _cursor = 0;
    uint8_t _next_handle = 0;
    std::map<uint8_t, std::string> _handle_paths;

    Answer serve(const FujiBusPacket &request);
};

#endif /* FAKECART_H */
