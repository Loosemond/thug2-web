#pragma once
// thug2-web: the audio session volume, only as far as sfx/mute_audio.cpp uses it.
#include <windows.h>
struct ISimpleAudioVolume : public IUnknown {
    virtual HRESULT SetMasterVolume(float level, const GUID* context) = 0;
    virtual HRESULT GetMasterVolume(float* level) = 0;
    virtual HRESULT SetMute(BOOL mute, const GUID* context) = 0;
    virtual HRESULT GetMute(BOOL* mute) = 0;
};
struct IAudioSessionManager : public IUnknown {
    virtual HRESULT GetAudioSessionControl(const GUID* session, DWORD flags, void** control) = 0;
    virtual HRESULT GetSimpleAudioVolume(const GUID* session, DWORD flags, ISimpleAudioVolume** volume) = 0;
};
DECLARE_UUIDOF_HELPER(IAudioSessionManager, 0xBFA971F1, 0x4D5E, 0x40BB, 0x93, 0x5E, 0x96, 0x70, 0x39, 0xBF, 0xBE, 0xE4)
DECLARE_UUIDOF_HELPER(ISimpleAudioVolume, 0x87CE5498, 0x68D6, 0x44E5, 0x92, 0x15, 0x6D, 0xA4, 0x7E, 0xF8, 0x83, 0xD8)
