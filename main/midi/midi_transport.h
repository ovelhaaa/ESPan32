#pragma once

namespace pocketpan::midi {

class MidiTransport {
public:
    virtual ~MidiTransport() = default;

    virtual bool begin() = 0;
    virtual void poll() = 0;
    virtual bool isConnected() const = 0;
};

} // namespace pocketpan::midi
