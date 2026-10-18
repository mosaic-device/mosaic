#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>

namespace mosaic::support {

// Clears only the supplied storage, not copies retained by SDKs or UI allocations.
inline void secure_erase(void* data, std::size_t size) {
    auto* bytes = static_cast<volatile unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i)
        bytes[i] = 0;
}

// Arrays retain their full extent; non-trivial objects must not have their invariants wiped.
template <typename T>
    requires(std::is_trivially_copyable_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>)
inline void secure_erase(T& object) {
    secure_erase(static_cast<void*>(std::addressof(object)), sizeof(object));
}

} // namespace mosaic::support
