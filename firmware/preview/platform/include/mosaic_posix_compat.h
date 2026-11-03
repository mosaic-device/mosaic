#pragma once
// The POSIX time calls the firmware uses, for MSVC. CMake force-includes this there.
#ifdef _WIN32
#include <cstdlib>
#include <ctime>

inline int setenv(const char* name, const char* value, int) {
    return _putenv_s(name, value) == 0 ? 0 : -1;
}
inline std::tm* localtime_r(const std::time_t* time, std::tm* result) {
    return localtime_s(result, time) == 0 ? result : nullptr;
}
#define tzset _tzset
#endif
