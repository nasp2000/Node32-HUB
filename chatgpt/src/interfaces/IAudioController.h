#pragma once
#include <Arduino.h>
#include <stdint.h>

enum class AudioSource : uint8_t {
    None = 0,
    Radio = 1,
    TTS = 2,
    Alarm = 3,
    File = 4
};

struct RadioStation {
    String name;
    String url;
    uint16_t port;
    uint32_t bitrate;
    bool isPlaying;
};

struct EqBand {
    uint8_t band;
    int8_t gain;
};

class IAudioController {
public:
    virtual ~IAudioController() = default;

    virtual bool init() = 0;
    virtual void loop() = 0;

    virtual bool play() = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual bool isPlaying() const = 0;

    virtual bool setVolume(uint8_t volume) = 0;
    virtual uint8_t getVolume() const = 0;

    virtual bool setSource(AudioSource source) = 0;
    virtual AudioSource getSource() const = 0;

    virtual bool playRadio(const String& url) = 0;
    virtual bool addRadioStation(const RadioStation& station) = 0;
    virtual bool removeRadioStation(const String& name) = 0;
    virtual bool getRadioStations(std::vector<RadioStation>& out) const = 0;

    virtual bool playTTS(const String& text) = 0;
    virtual bool playAlarm(const String& tone, uint32_t durationMs = 3000) = 0;
    virtual bool playFile(const String& path) = 0;

    virtual bool setEq(const EqBand* bands, size_t count) = 0;
    virtual bool getEq(std::vector<EqBand>& out) const = 0;
    virtual bool resetEq() = 0;

    virtual bool isReady() const = 0;
    virtual bool hasError() const = 0;
    virtual String getLastError() const = 0;
};
