#pragma once

#include <cstddef>
#include <cstdint>

namespace tt::service::mesh {

/**
 * Recent-packet filter keyed on (sender, packet id), matching the mesh
 * flooding rule: a packet heard twice (via different relays) must only
 * be processed once. Fixed-size ring, oldest entry evicted first.
 */
class PacketDedup {

public:

    /** @return true when the packet is new (and records it), false when it is a duplicate. */
    bool checkAndAdd(uint32_t from, uint32_t id) {
        for (size_t i = 0; i < CAPACITY; i++) {
            if (entries[i].used && entries[i].from == from && entries[i].id == id) {
                return false;
            }
        }
        entries[nextIndex] = {from, id, true};
        nextIndex = (nextIndex + 1) % CAPACITY;
        return true;
    }

private:

    static constexpr size_t CAPACITY = 64;

    struct Entry {
        uint32_t from = 0;
        uint32_t id = 0;
        bool used = false;
    };

    Entry entries[CAPACITY] = {};
    size_t nextIndex = 0;
};

} // namespace tt::service::mesh
