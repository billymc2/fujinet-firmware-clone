#include "FakeCart.h"

#include "bus.h"

class FakeCart::Bus : public SystemBusBase
{
private:
    FakeCart &_cart;

    static std::unique_ptr<FujiBusPacket> wire(const Answer &answer, uint8_t tag)
    {
        FujiBusPacket reply = answer.data.empty()
            ? FujiBusPacket(FUJI_DEVICEID::DBC, answer.command, tag)
            : FujiBusPacket(FUJI_DEVICEID::DBC, answer.command, answer.data, tag);
        return FujiBusPacket::fromSerialized(reply.serialize());
    }

public:
    explicit Bus(FakeCart &cart) : _cart(cart) {}

    success_is_true writeFujiBusPacket(FujiBusPacket &packet) override
    {
        _cart.requests.push_back(FujiBusPacket::fromSerialized(packet.serialize()));
        RETURN_SUCCESS_IF(_cart.requests.back() != nullptr);
    }

    std::unique_ptr<FujiBusPacket> readFujiBusPacket(int) override
    {
        const FujiBusPacket &request = *_cart.requests.back();
        uint8_t tag = static_cast<uint8_t>(request.param(request.paramCount() - 1));
        if (_cart.stale > 0)
        {
            _cart.stale--;
            return wire(Answer{}, static_cast<uint8_t>(tag - 1));
        }
        return wire(_cart.respond ? _cart.respond(request) : _cart.serve(request), tag);
    }

    void transaction_accept(transState_t) override {}
    void transaction_success() override {}
    void transaction_error() override {}
    success_is_true transaction_get(void *, size_t) override { RETURN_SUCCESS_AS_TRUE(); }
    void transaction_send(const void *, size_t, bool) override {}
};

FakeCart::FakeCart() : _bus(std::make_unique<Bus>(*this)) {}
FakeCart::~FakeCart() = default;

SystemBusBase &FakeCart::bus()
{
    return *_bus;
}

static std::string data_of(const FujiBusPacket &request)
{
    const auto &data = request.data();
    return data ? std::string(data->begin(), data->end()) : std::string();
}

Answer FakeCart::serve(const FujiBusPacket &request)
{
    const Answer nak{CMD::FUJI_NAK};
    switch (request.command())
    {
    case CMD::FUJI_OPEN_DIRECTORY:
        if (data_of(request).c_str() != std::string("/"))
            return nak;
        _cursor = 0;
        return Answer{};
    case CMD::FUJI_READ_DIR_ENTRY:
    {
        if (_cursor >= listing.size())
            return Answer{CMD::FUJI_ACK, "\x7F\x7F"};
        const auto &entry = listing[_cursor++];
        std::string name = entry.first + (entry.second ? "/" : "");
        name.push_back('\0');
        return Answer{CMD::FUJI_ACK, name};
    }
    case CMD::FUJI_CLOSE_DIRECTORY:
        dir_closes++;
        return Answer{};
    case CMD::FUJI_GET_DIRECTORY_POSITION:
        return Answer{CMD::FUJI_ACK, std::string{static_cast<char>(_cursor), 0}};
    case CMD::FUJI_SET_DIRECTORY_POSITION:
        if (request.param(0) > listing.size())
            return nak;
        _cursor = request.param(0);
        return Answer{};
    case CMD::NET_OPEN:
    {
        std::string data = data_of(request);
        if (data.empty() || data[0] != 2)
            return nak;
        auto found = files.find(data.substr(1));
        if (found == files.end())
            return nak;
        uint8_t handle = _next_handle++;
        open_handles.insert(handle);
        _handle_paths[handle] = found->first;
        uint32_t size = static_cast<uint32_t>(found->second.size());
        std::string reply{static_cast<char>(handle)};
        for (int shift = 0; shift < 32; shift += 8)
            reply.push_back(static_cast<char>(size >> shift));
        return Answer{CMD::FUJI_ACK, reply};
    }
    case CMD::NET_READ:
    {
        const std::string &content = files[_handle_paths[request.param(0)]];
        size_t offset = request.param(1);
        if (offset >= content.size())
            return Answer{};
        return Answer{CMD::FUJI_ACK, content.substr(offset, request.param(2))};
    }
    case CMD::NET_CLOSE:
        open_handles.erase(static_cast<uint8_t>(request.param(0)));
        return Answer{};
    default:
        return nak;
    }
}
