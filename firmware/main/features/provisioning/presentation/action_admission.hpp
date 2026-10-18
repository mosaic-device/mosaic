#pragma once
#include <cstdint>
namespace mosaic::features::provisioning {
// Bridge serializes issue/send/record under one short lock. Owner drains rejection
// before processing each accepted action; state machine IDs discard obsolete feedback.
class ActionAdmission {
public:
    uint64_t issue() {
        return ++next_;
    }
    struct Observation {
        uint64_t id = 0;
        uint32_t context = 0;
    };
    void rejected(uint64_t id, uint32_t context = 0) {
        if (id > rejected_.id)
            rejected_ = { id, context };
    }
    Observation take_rejected() {
        const auto observation = rejected_;
        rejected_ = { };
        return observation;
    }

private:
    uint64_t next_ = 0;
    Observation rejected_;
};
}
