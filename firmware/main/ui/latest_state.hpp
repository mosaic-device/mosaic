#pragma once
#include <cstdint>
namespace mosaic::ui {
// All methods require the same external lock. Never hold that lock while rendering.
template <class T> class LatestState {
public:
    bool publish(const T& value) {
        if (valid_ && value_ == value)
            return false;
        value_ = value;
        valid_ = true;
        ++revision_;
        return true;
    }
    bool read(T& value, uint64_t& revision) const {
        if (!valid_ || consumed_ == revision_)
            return false;
        value = value_;
        revision = revision_;
        return true;
    }
    void consumed(uint64_t revision) {
        if (revision == revision_)
            consumed_ = revision;
    }
    void replay() {
        consumed_ = 0;
    }

private:
    T value_ { };
    uint64_t revision_ = 0;
    uint64_t consumed_ = 0;
    bool valid_ = false;
};
}
