// audio.h - looping chiptune music for the game (Windows waveOut)
#pragma once

#include <memory>

class MusicPlayer {
public:
    MusicPlayer();
    ~MusicPlayer();
    MusicPlayer(const MusicPlayer&) = delete;
    MusicPlayer& operator=(const MusicPlayer&) = delete;

    bool Init();
    void Shutdown();

    void Start();          // (re)start the melody from the beginning
    void Stop();           // silence and keep the engine idle
    void SetPaused(bool paused);
    void ToggleMute();
    bool Muted() const;
    bool Available() const;

    void Pump();           // call regularly from the main thread
    void PlayGameOver();   // short descending jingle

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
