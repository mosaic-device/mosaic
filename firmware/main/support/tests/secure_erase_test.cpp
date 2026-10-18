#include "../secure_erase.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <string>

namespace {
template <typename T>
concept Erasable = requires(T& value) { mosaic::support::secure_erase(value); };

struct Record {
    unsigned char marker;
    unsigned long counter;
    char secret[17];
};

static_assert(Erasable<Record>);
static_assert(Erasable<char[37]>);
static_assert(!Erasable<const Record>);
static_assert(!Erasable<volatile Record>);
static_assert(!Erasable<std::string>);

void assert_zero(const void* storage, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(storage);
    for (std::size_t i = 0; i < size; ++i)
        assert(bytes[i] == 0);
}
}

int main() {
    using mosaic::support::secure_erase;
    std::array<unsigned char, 67> bytes;
    bytes.fill(0xa5);
    secure_erase(bytes.data() + 1, bytes.size() - 2);
    assert(bytes.front() == 0xa5 && bytes.back() == 0xa5);
    assert_zero(bytes.data() + 1, bytes.size() - 2);
    secure_erase(bytes.data(), 0);
    assert(bytes.front() == 0xa5);
    secure_erase(nullptr, 0);

    char secret[37];
    std::memset(secret, 0xff, sizeof(secret));
    secure_erase(secret);
    assert_zero(secret, sizeof(secret));

    Record record;
    // Seed padding too: the object overload must clear the entire representation.
    std::memset(&record, 0xa5, sizeof(record));
    secure_erase(record);
    assert_zero(&record, sizeof(record));

    unsigned char single = 0xff;
    secure_erase(single);
    assert(single == 0);
}
