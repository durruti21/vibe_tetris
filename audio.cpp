// audio.cpp - chiptune arrangement of Korobeiniki ("Tetris Theme A"), rendered
// at startup and played through the Win32 waveOut API. The melody is a
// traditional Russian folk tune in the public domain; all sound is synthesized
// in code, so the game still ships without media assets.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "audio.h"

#pragma comment(lib, "winmm.lib")

namespace {

constexpr int kSampleRate = 44100;
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kBeatsPerMinute = 150.0;
constexpr double kBeatSamples = kSampleRate * 60.0 / kBeatsPerMinute;

constexpr double kLeadVolume = 0.20;
constexpr double kBassVolume = 0.085;

struct Note {
    int midi;       // 0 = rest
    double beats;
};

double MidiToFreq(int midi) {
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}

// Section A: the familiar 8-bar theme.
const Note kSectionA[] = {
    {76, 1.0}, {71, 0.5}, {72, 0.5}, {74, 1.0}, {72, 0.5}, {71, 0.5},
    {69, 1.0}, {69, 0.5}, {72, 0.5}, {76, 1.0}, {74, 0.5}, {72, 0.5},
    {71, 1.5}, {72, 0.5}, {74, 1.0}, {76, 1.0},
    {72, 1.0}, {69, 1.0}, {69, 1.0}, {0, 1.0},

    {74, 1.5}, {77, 0.5}, {81, 1.0}, {79, 0.5}, {77, 0.5},
    {76, 1.5}, {72, 0.5}, {76, 1.0}, {74, 0.5}, {72, 0.5},
    {71, 1.5}, {72, 0.5}, {74, 1.0}, {76, 1.0},
    {72, 1.0}, {69, 1.0}, {69, 1.0}, {0, 1.0},
};

// Section B: the half-note bridge.
const Note kSectionB[] = {
    {76, 2.0}, {72, 2.0},
    {74, 2.0}, {71, 2.0},
    {72, 2.0}, {69, 2.0},
    {68, 2.0}, {71, 2.0},
    {76, 2.0}, {72, 2.0},
    {74, 2.0}, {71, 2.0},
    {72, 2.0}, {76, 2.0},
    {81, 2.0}, {80, 2.0},
};

std::vector<Note> BuildMelody() {
    std::vector<Note> melody;
    melody.reserve(2 * (sizeof(kSectionA) + sizeof(kSectionB)) / sizeof(Note));
    for (int cycle = 0; cycle < 2; ++cycle) {
        melody.insert(melody.end(), kSectionA, kSectionA + sizeof(kSectionA) / sizeof(Note));
        melody.insert(melody.end(), kSectionB, kSectionB + sizeof(kSectionB) / sizeof(Note));
    }
    return melody;
}

// One root note per bar. A section: E A E A D C E A, B section: C G A E C G C E.
std::vector<int> BuildBassRoots() {
    static const int kRootsA[] = {40, 45, 40, 45, 38, 36, 40, 45};
    static const int kRootsB[] = {36, 43, 45, 40, 36, 43, 36, 40};
    std::vector<int> roots;
    roots.reserve(2 * (sizeof(kRootsA) + sizeof(kRootsB)) / sizeof(int));
    for (int cycle = 0; cycle < 2; ++cycle) {
        roots.insert(roots.end(), kRootsA, kRootsA + sizeof(kRootsA) / sizeof(int));
        roots.insert(roots.end(), kRootsB, kRootsB + sizeof(kRootsB) / sizeof(int));
    }
    return roots;
}

double ToneEnvelope(size_t i, size_t count) {
    const size_t attack = std::min(count, static_cast<size_t>(kSampleRate * 0.004));
    const size_t decay = std::min(count - attack, static_cast<size_t>(kSampleRate * 0.030));
    const size_t release = std::min(count, static_cast<size_t>(kSampleRate * 0.025));

    double env = 0.8;
    if (attack > 0 && i < attack) {
        env = static_cast<double>(i) / static_cast<double>(attack);
    } else if (decay > 0 && i < attack + decay) {
        const double t = static_cast<double>(i - attack) / static_cast<double>(decay);
        env = 1.0 - 0.2 * t;
    }
    if (release > 0) {
        const size_t left = count - i;
        if (left < release) env *= static_cast<double>(left) / static_cast<double>(release);
    }
    return env;
}

void AddSquare(std::vector<float>& mix, size_t start, size_t count, double freq,
               double volume) {
    if (count == 0 || start + count > mix.size()) return;
    const double step = freq / static_cast<double>(kSampleRate);
    double phase = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const double wave = phase < 0.5 ? 1.0 : -1.0;
        phase += step;
        if (phase >= 1.0) phase -= 1.0;
        mix[start + i] += static_cast<float>(wave * ToneEnvelope(i, count) * volume);
    }
}

void AddBass(std::vector<float>& mix, size_t start, size_t count, double freq,
             double volume) {
    if (count == 0 || start + count > mix.size()) return;
    const double step = freq / static_cast<double>(kSampleRate);
    double phase = 0.0;
    for (size_t i = 0; i < count; ++i) {
        phase += step;
        if (phase >= 1.0) phase -= 1.0;
        const double wave = std::sin(kTwoPi * phase) + 0.2 * std::sin(2.0 * kTwoPi * phase);
        mix[start + i] += static_cast<float>(wave * ToneEnvelope(i, count) * volume);
    }
}

std::vector<int16_t> ToPcm(const std::vector<float>& mix) {
    std::vector<int16_t> pcm(mix.size());
    for (size_t i = 0; i < mix.size(); ++i) {
        const double sample = std::clamp(static_cast<double>(mix[i]), -1.0, 1.0);
        pcm[i] = static_cast<int16_t>(std::lround(sample * 32000.0));
    }
    return pcm;
}

std::vector<int16_t> RenderLoop() {
    const std::vector<Note> melody = BuildMelody();

    double totalBeats = 0.0;
    for (const Note& n : melody) totalBeats += n.beats;
    const size_t totalSamples =
        static_cast<size_t>(std::llround(totalBeats * kBeatSamples));
    if (totalSamples == 0) return {};

    std::vector<float> mix(totalSamples, 0.0f);

    // Lead line.
    size_t cursor = 0;
    for (const Note& n : melody) {
        const size_t len = static_cast<size_t>(std::llround(n.beats * kBeatSamples));
        if (n.midi > 0) {
            const size_t gate =
                static_cast<size_t>(std::llround(static_cast<double>(len) * 0.92));
            AddSquare(mix, cursor, gate, MidiToFreq(n.midi), kLeadVolume);
        }
        cursor += len;
    }

    // Eighth-note bass arpeggio (root, fifth, octave, fifth).
    static const int kArpeggio[8] = {0, 7, 12, 7, 0, 7, 12, 7};
    const int kArpeggioSteps = static_cast<int>(sizeof(kArpeggio) / sizeof(int));
    const size_t halfBeat = static_cast<size_t>(std::llround(kBeatSamples * 0.5));
    const std::vector<int> roots = BuildBassRoots();
    size_t barStart = 0;
    for (int root : roots) {
        for (int step = 0; step < kArpeggioSteps; ++step) {
            const size_t start = barStart + static_cast<size_t>(step) * halfBeat;
            const size_t gate =
                static_cast<size_t>(std::llround(static_cast<double>(halfBeat) * 0.82));
            AddBass(mix, start, gate, MidiToFreq(root + kArpeggio[step]), kBassVolume);
        }
        barStart += halfBeat * static_cast<size_t>(kArpeggioSteps);
    }

    return ToPcm(mix);
}

std::vector<int16_t> RenderJingle() {
    struct JingleNote {
        int midi;
        double seconds;
    };
    static const JingleNote kNotes[] = {
        {76, 0.16}, {74, 0.16}, {72, 0.16}, {71, 0.16},
        {69, 0.34}, {0, 0.08}, {64, 0.55},
    };

    size_t totalSamples = 0;
    for (const JingleNote& n : kNotes) {
        totalSamples +=
            static_cast<size_t>(std::llround(n.seconds * static_cast<double>(kSampleRate)));
    }
    if (totalSamples == 0) return {};

    std::vector<float> mix(totalSamples, 0.0f);
    size_t cursor = 0;
    for (const JingleNote& n : kNotes) {
        const size_t len =
            static_cast<size_t>(std::llround(n.seconds * static_cast<double>(kSampleRate)));
        if (n.midi > 0) AddSquare(mix, cursor, len, MidiToFreq(n.midi), 0.17);
        cursor += len;
    }
    return ToPcm(mix);
}

}  // namespace

struct MusicPlayer::Impl {
    HWAVEOUT out = nullptr;

    std::vector<int16_t> loopPcm;
    std::vector<int16_t> jinglePcm;
    size_t splitSample = 0;

    WAVEHDR loopHdr[2]{};
    bool loopPrepared[2]{};
    WAVEHDR jingleHdr{};
    bool jinglePrepared = false;

    bool available = false;
    bool playing = false;
    bool jingle = false;
    bool paused = false;
    bool muted = false;
    bool devicePaused = false;

    bool Init();
    void Shutdown();
    void ApplyTransport();
    void StopAll();
    bool PrepareLoop(int index);
    bool PrepareJingle();
};

bool MusicPlayer::Impl::PrepareLoop(int index) {
    if (!out || index < 0 || index > 1) return false;
    const size_t start = (index == 0) ? 0 : splitSample;
    const size_t samples = (index == 0) ? splitSample : loopPcm.size() - splitSample;
    if (samples == 0) return false;

    WAVEHDR& h = loopHdr[index];
    h = WAVEHDR{};
    h.lpData = reinterpret_cast<LPSTR>(loopPcm.data() + start);
    h.dwBufferLength = static_cast<DWORD>(samples * sizeof(int16_t));

    loopPrepared[index] = false;
    if (waveOutPrepareHeader(out, &h, static_cast<UINT>(sizeof(WAVEHDR))) !=
        MMSYSERR_NOERROR) {
        return false;
    }
    loopPrepared[index] = true;
    return true;
}

bool MusicPlayer::Impl::PrepareJingle() {
    if (!out || jinglePcm.empty()) return false;
    jingleHdr = WAVEHDR{};
    jingleHdr.lpData = reinterpret_cast<LPSTR>(jinglePcm.data());
    jingleHdr.dwBufferLength = static_cast<DWORD>(jinglePcm.size() * sizeof(int16_t));

    jinglePrepared = false;
    if (waveOutPrepareHeader(out, &jingleHdr, static_cast<UINT>(sizeof(WAVEHDR))) !=
        MMSYSERR_NOERROR) {
        return false;
    }
    jinglePrepared = true;
    return true;
}

void MusicPlayer::Impl::ApplyTransport() {
    if (!out) return;
    const bool shouldPause = paused || muted;
    if (shouldPause && !devicePaused) {
        waveOutPause(out);
        devicePaused = true;
    } else if (!shouldPause && devicePaused) {
        waveOutRestart(out);
        devicePaused = false;
    }
}

void MusicPlayer::Impl::StopAll() {
    if (out) {
        waveOutReset(out);
        if (jinglePrepared) {
            if (waveOutUnprepareHeader(out, &jingleHdr,
                                       static_cast<UINT>(sizeof(WAVEHDR))) ==
                MMSYSERR_NOERROR) {
                jinglePrepared = false;
            }
        }
        for (int i = 0; i < 2; ++i) {
            if (!loopPrepared[i]) continue;
            if (waveOutUnprepareHeader(out, &loopHdr[i],
                                       static_cast<UINT>(sizeof(WAVEHDR))) ==
                MMSYSERR_NOERROR) {
                loopPrepared[i] = false;
            }
        }
    }
    playing = false;
    jingle = false;
    devicePaused = false;
}

bool MusicPlayer::Impl::Init() {
    if (available) return true;
    if (out) return false;

    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = kSampleRate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = static_cast<WORD>(fmt.nChannels * fmt.wBitsPerSample / 8);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    fmt.cbSize = 0;

    if (waveOutOpen(&out, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        out = nullptr;
        return false;
    }

    loopPcm = RenderLoop();
    jinglePcm = RenderJingle();
    if (loopPcm.size() < 4 || jinglePcm.empty()) {
        Shutdown();
        return false;
    }
    if ((loopPcm.size() & 1u) != 0) loopPcm.push_back(0);

    splitSample = (loopPcm.size() / 2) & ~static_cast<size_t>(1);
    if (splitSample == 0 || splitSample >= loopPcm.size()) {
        Shutdown();
        return false;
    }

    available = true;
    return true;
}

void MusicPlayer::Impl::Shutdown() {
    if (out) {
        StopAll();
        waveOutClose(out);
        out = nullptr;
    }
    available = false;
    playing = false;
    jingle = false;
    paused = false;
    muted = false;
    devicePaused = false;
    loopHdr[0] = WAVEHDR{};
    loopHdr[1] = WAVEHDR{};
    jingleHdr = WAVEHDR{};
    loopPrepared[0] = false;
    loopPrepared[1] = false;
    jinglePrepared = false;
    splitSample = 0;
    std::vector<int16_t>().swap(loopPcm);
    std::vector<int16_t>().swap(jinglePcm);
}

MusicPlayer::MusicPlayer() : impl_(std::make_unique<Impl>()) {}

MusicPlayer::~MusicPlayer() { Shutdown(); }

bool MusicPlayer::Init() { return impl_ && impl_->Init(); }

void MusicPlayer::Shutdown() {
    if (impl_) impl_->Shutdown();
}

void MusicPlayer::Start() {
    if (!impl_ || !impl_->available || !impl_->out) return;
    impl_->StopAll();
    impl_->paused = false;

    if (!impl_->PrepareLoop(0) || !impl_->PrepareLoop(1)) {
        impl_->StopAll();
        return;
    }

    for (int i = 0; i < 2; ++i) {
        if (waveOutWrite(impl_->out, &impl_->loopHdr[i],
                         static_cast<UINT>(sizeof(WAVEHDR))) != MMSYSERR_NOERROR) {
            impl_->StopAll();
            return;
        }
    }

    impl_->playing = true;
    if (impl_->paused || impl_->muted) {
        waveOutPause(impl_->out);
        impl_->devicePaused = true;
    } else {
        waveOutRestart(impl_->out);
        impl_->devicePaused = false;
    }
}

void MusicPlayer::Stop() {
    if (!impl_ || !impl_->available) return;
    impl_->StopAll();
    impl_->paused = false;
}

void MusicPlayer::SetPaused(bool paused) {
    if (!impl_ || !impl_->available) return;
    impl_->paused = paused;
    impl_->ApplyTransport();
}

void MusicPlayer::ToggleMute() {
    if (!impl_ || !impl_->available) return;
    impl_->muted = !impl_->muted;
    impl_->ApplyTransport();
}

bool MusicPlayer::Muted() const { return impl_ && impl_->muted; }

bool MusicPlayer::Available() const { return impl_ && impl_->available; }

void MusicPlayer::Pump() {
    if (!impl_ || !impl_->out) return;

    if (impl_->jingle) {
        if (impl_->jinglePrepared && (impl_->jingleHdr.dwFlags & WHDR_DONE) != 0) {
            if (waveOutUnprepareHeader(impl_->out, &impl_->jingleHdr,
                                       static_cast<UINT>(sizeof(WAVEHDR))) ==
                MMSYSERR_NOERROR) {
                impl_->jinglePrepared = false;
            }
            if (!impl_->jinglePrepared) impl_->jingle = false;
        }
        return;
    }

    if (!impl_->playing) return;

    // Keep both halves of the loop queued: whichever finishes is re-prepared and
    // written behind the other one.
    for (int i = 0; i < 2; ++i) {
        if (!impl_->loopPrepared[i]) continue;
        WAVEHDR& h = impl_->loopHdr[i];
        if ((h.dwFlags & WHDR_DONE) == 0) continue;

        if (waveOutUnprepareHeader(impl_->out, &h, static_cast<UINT>(sizeof(WAVEHDR))) !=
            MMSYSERR_NOERROR) {
            continue;
        }
        impl_->loopPrepared[i] = false;

        if (!impl_->PrepareLoop(i)) continue;
        if (waveOutWrite(impl_->out, &h, static_cast<UINT>(sizeof(WAVEHDR))) !=
            MMSYSERR_NOERROR) {
            impl_->StopAll();
            return;
        }
    }
}

void MusicPlayer::PlayGameOver() {
    if (!impl_ || !impl_->available || !impl_->out) return;
    impl_->StopAll();
    if (impl_->muted) return;

    if (!impl_->PrepareJingle()) return;
    if (waveOutWrite(impl_->out, &impl_->jingleHdr,
                     static_cast<UINT>(sizeof(WAVEHDR))) != MMSYSERR_NOERROR) {
        impl_->StopAll();
        return;
    }

    impl_->jingle = true;
    impl_->paused = false;
    waveOutRestart(impl_->out);
    impl_->devicePaused = false;
}
