#include "audio_engine.h"

#include <algorithm>
#include <array>
#include <cmath>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

namespace {

std::wstring wide_from_utf8(const char* text) {
    if (!text || !*text) {
        return L"Unknown audio error";
    }
    const int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (count <= 0) {
        std::wstring fallback;
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
            fallback.push_back(static_cast<wchar_t>(*p));
        }
        return fallback;
    }
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), count);
    wide.resize(static_cast<std::size_t>(count - 1));
    return wide;
}
std::wstring result_message(ma_result result) {
    return wide_from_utf8(ma_result_description(result));
}

float volume_float(int volume) {
    return static_cast<float>(std::clamp(volume, 0, 100)) / 100.0f;
}

int seconds_to_ms(float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0f) {
        return 0;
    }
    return static_cast<int>(seconds * 1000.0f + 0.5f);
}

} // namespace

struct AudioEngine::Impl {
    ma_engine engine{};
    ma_sound sound{};
    bool engine_initialized = false;
    bool sound_initialized = false;

    ~Impl() {
        close_sound();
        if (engine_initialized) {
            ma_engine_uninit(&engine);
        }
    }

    void close_sound() {
        if (sound_initialized) {
            ma_sound_uninit(&sound);
            sound_initialized = false;
        }
    }
};

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine() = default;

bool AudioEngine::open(const std::filesystem::path& path, int volume, std::wstring& error_message) {
    close();
    if (!impl_->engine_initialized) {
        const ma_result result = ma_engine_init(nullptr, &impl_->engine);
        if (result != MA_SUCCESS) {
            error_message = L"无法初始化 miniaudio 播放引擎：\n" + result_message(result);
            return false;
        }
        impl_->engine_initialized = true;
    }

    constexpr ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
    const ma_result result = ma_sound_init_from_file_w(&impl_->engine, path.wstring().c_str(), flags, nullptr, nullptr, &impl_->sound);
    if (result != MA_SUCCESS) {
        error_message = L"无法打开音频文件：\n" + path.wstring() + L"\n\n" + result_message(result);
        return false;
    }
    impl_->sound_initialized = true;
    set_volume(volume);
    return true;
}

void AudioEngine::close() {
    impl_->close_sound();
}

bool AudioEngine::play(std::wstring& error_message) {
    if (!impl_->sound_initialized) {
        error_message = L"播放列表里没有可播放的音频。";
        return false;
    }
    const ma_result result = ma_sound_start(&impl_->sound);
    if (result != MA_SUCCESS) {
        error_message = L"无法开始播放：\n" + result_message(result);
        return false;
    }
    return true;
}

void AudioEngine::pause() {
    if (impl_->sound_initialized) {
        ma_sound_stop(&impl_->sound);
    }
}

void AudioEngine::stop() {
    if (impl_->sound_initialized) {
        ma_sound_stop(&impl_->sound);
        ma_sound_seek_to_pcm_frame(&impl_->sound, 0);
    }
}

bool AudioEngine::seek_ms(int position_ms) {
    if (!impl_->sound_initialized) {
        return false;
    }
    const float seconds = static_cast<float>(std::max(0, position_ms)) / 1000.0f;
    return ma_sound_seek_to_second(&impl_->sound, seconds) == MA_SUCCESS;
}

void AudioEngine::set_volume(int volume) {
    if (impl_->sound_initialized) {
        ma_sound_set_volume(&impl_->sound, volume_float(volume));
    }
}

int AudioEngine::position_ms() const {
    if (!impl_->sound_initialized) {
        return 0;
    }
    float seconds = 0.0f;
    if (ma_sound_get_cursor_in_seconds(&impl_->sound, &seconds) != MA_SUCCESS) {
        return 0;
    }
    return seconds_to_ms(seconds);
}

int AudioEngine::length_ms() const {
    if (!impl_->sound_initialized) {
        return 0;
    }
    float seconds = 0.0f;
    if (ma_sound_get_length_in_seconds(&impl_->sound, &seconds) != MA_SUCCESS) {
        return 0;
    }
    return seconds_to_ms(seconds);
}

bool AudioEngine::opened() const {
    return impl_->sound_initialized;
}

const wchar_t* audio_backend_name() {
    return L"miniaudio";
}

