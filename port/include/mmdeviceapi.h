#pragma once
// thug2-web: the Core Audio device enumerator, only as far as sfx/mute_audio.cpp uses it. CoCreateInstance fails
// on the web, so the game falls back to its own volume.
#include <windows.h>
typedef enum EDataFlow { eRender, eCapture, eAll } EDataFlow;
typedef enum ERole { eConsole, eMultimedia, eCommunications } ERole;
typedef struct tagPROPVARIANT PROPVARIANT;
struct IMMDevice : public IUnknown {
    virtual HRESULT Activate(REFIID iid, DWORD context, PROPVARIANT* parameters, void** result) = 0;
    virtual HRESULT OpenPropertyStore(DWORD access, void** store) = 0;
    virtual HRESULT GetId(LPWSTR* id) = 0;
    virtual HRESULT GetState(DWORD* state) = 0;
};
struct IMMDeviceEnumerator : public IUnknown {
    virtual HRESULT EnumAudioEndpoints(EDataFlow flow, DWORD mask, void** devices) = 0;
    virtual HRESULT GetDefaultAudioEndpoint(EDataFlow flow, ERole role, IMMDevice** device) = 0;
    virtual HRESULT GetDevice(LPCWSTR id, IMMDevice** device) = 0;
    virtual HRESULT RegisterEndpointNotificationCallback(void* client) = 0;
    virtual HRESULT UnregisterEndpointNotificationCallback(void* client) = 0;
};
struct MMDeviceEnumerator;
DECLARE_UUIDOF_HELPER(MMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E)
DECLARE_UUIDOF_HELPER(IMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6)
DECLARE_UUIDOF_HELPER(IMMDevice, 0xD666063F, 0x1587, 0x4E43, 0x81, 0xF1, 0xB9, 0x48, 0xE8, 0x07, 0x36, 0x3F)
