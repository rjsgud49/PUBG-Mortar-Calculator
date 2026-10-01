#include <windows.h>
#include <sapi.h>

#include "speech.hpp"

#include <algorithm>
#include <cmath>

namespace {

const GUID kSpVoice = {0x96749377, 0x3391, 0x11D2, {0x9E, 0xE3, 0x00, 0xC0, 0x4F, 0x79, 0x73, 0x96}};
const GUID kISpVoice = {0x6C44DF74, 0x72B9, 0x4992, {0xA1, 0xEC, 0xEF, 0x99, 0x6E, 0x04, 0x22, 0xD4}};

ISpVoice* voice = nullptr;
bool ready = false;
int volume = 50;
int rate = 150;

void ensure_voice() {
    if (ready) {
        return;
    }
    ready = true;
    CoCreateInstance(kSpVoice, nullptr, CLSCTX_ALL, kISpVoice, reinterpret_cast<void**>(&voice));
}

void apply_voice() {
    if (voice == nullptr) {
        return;
    }
    voice->SetVolume(static_cast<USHORT>(std::clamp(volume, 0, 100)));
    const long sapi_rate = std::clamp(std::lround((rate - 150) / 15.0), -10L, 10L);
    voice->SetRate(sapi_rate);
}

}  // namespace

void configure_speech(int new_volume, int new_rate) {
    volume = std::clamp(new_volume, 0, 100);
    rate = std::clamp(new_rate, 50, 300);
    ensure_voice();
    apply_voice();
}

void speak_text(const std::wstring& text) {
    ensure_voice();
    apply_voice();
    if (voice == nullptr || text.empty()) {
        return;
    }
    voice->Speak(text.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
}
