#pragma once

#include <filesystem>
#include <memory>
#include <string>

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool open(const std::filesystem::path& path, int volume, std::wstring& error_message);
    void close();
    bool play(std::wstring& error_message);
    void pause();
    void stop();
    bool seek_ms(int position_ms);
    void set_volume(int volume);
    int position_ms() const;
    int length_ms() const;
    bool opened() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

const wchar_t* audio_backend_name();
