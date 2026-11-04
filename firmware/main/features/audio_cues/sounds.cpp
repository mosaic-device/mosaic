#include "sounds.hpp"

extern const uint8_t notification_wav_start[] asm("_binary_notification_wav_start");
extern const uint8_t notification_wav_end[] asm("_binary_notification_wav_end");
extern const uint8_t connected_wav_start[] asm("_binary_connected_wav_start");
extern const uint8_t connected_wav_end[] asm("_binary_connected_wav_end");

namespace mosaic::features::audio_cues {
std::span<const uint8_t> sound_file(Cue cue) {
    if (cue == Cue::connected)
        return { connected_wav_start, connected_wav_end };
    return { notification_wav_start, notification_wav_end };
}
}
