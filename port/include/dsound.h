#pragma once
// thug2-web: DirectSound 8. The game calls the buffers through vtable slots (sfx_voices.cpp Com<>), so the
// implementation (WebAudio) keeps every interface's method order exactly as in the SDK.
#include <windows.h>

typedef struct tWAVEFORMATEX {
    WORD wFormatTag;
    WORD nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD nBlockAlign;
    WORD wBitsPerSample;
    WORD cbSize;
} WAVEFORMATEX, *LPWAVEFORMATEX;
typedef const WAVEFORMATEX* LPCWAVEFORMATEX;
#define WAVE_FORMAT_PCM 1

typedef struct _DSBUFFERDESC {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwBufferBytes;
    DWORD dwReserved;
    LPWAVEFORMATEX lpwfxFormat;
    GUID guid3DAlgorithm;
} DSBUFFERDESC, *LPDSBUFFERDESC;
typedef const DSBUFFERDESC* LPCDSBUFFERDESC;

typedef struct _DSCAPS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwMinSecondarySampleRate;
    DWORD dwMaxSecondarySampleRate;
    DWORD dwPrimaryBuffers;
    DWORD dwMaxHwMixingAllBuffers;
    DWORD dwMaxHwMixingStaticBuffers;
    DWORD dwMaxHwMixingStreamingBuffers;
    DWORD dwFreeHwMixingAllBuffers;
    DWORD dwFreeHwMixingStaticBuffers;
    DWORD dwFreeHwMixingStreamingBuffers;
    DWORD dwMaxHw3DAllBuffers;
    DWORD dwMaxHw3DStaticBuffers;
    DWORD dwMaxHw3DStreamingBuffers;
    DWORD dwFreeHw3DAllBuffers;
    DWORD dwFreeHw3DStaticBuffers;
    DWORD dwFreeHw3DStreamingBuffers;
    DWORD dwTotalHwMemBytes;
    DWORD dwFreeHwMemBytes;
    DWORD dwMaxContigFreeHwMemBytes;
    DWORD dwUnlockTransferRateHwBuffers;
    DWORD dwPlayCpuOverheadSwBuffers;
    DWORD dwReserved1;
    DWORD dwReserved2;
} DSCAPS, *LPDSCAPS;

#define DSSCL_NORMAL 0x1
#define DSSCL_PRIORITY 0x2
#define DSSCL_EXCLUSIVE 0x3
#define DSSCL_WRITEPRIMARY 0x4
#define DSBCAPS_PRIMARYBUFFER 0x1
#define DSBCAPS_STATIC 0x2
#define DSBCAPS_LOCHARDWARE 0x4
#define DSBCAPS_LOCSOFTWARE 0x8
#define DSBCAPS_CTRL3D 0x10
#define DSBCAPS_CTRLFREQUENCY 0x20
#define DSBCAPS_CTRLPAN 0x40
#define DSBCAPS_CTRLVOLUME 0x80
#define DSBCAPS_CTRLPOSITIONNOTIFY 0x100
#define DSBCAPS_GETCURRENTPOSITION2 0x10000
#define DSBCAPS_GLOBALFOCUS 0x8000
#define DSBPLAY_LOOPING 0x1
#define DSBSTATUS_PLAYING 0x1
#define DSBSTATUS_BUFFERLOST 0x2
#define DSBSTATUS_LOOPING 0x4
#define DSBLOCK_FROMWRITECURSOR 0x1
#define DSBLOCK_ENTIREBUFFER 0x2
#define DSBVOLUME_MIN -10000
#define DSBVOLUME_MAX 0
#define DSBPAN_LEFT -10000
#define DSBPAN_RIGHT 10000
#define DS_OK S_OK
#define DSERR_BUFFERLOST MAKE_HRESULT(1, 0x878, 150)
#define DSERR_INVALIDCALL MAKE_HRESULT(1, 0x878, 50)
#define DSERR_INVALIDPARAM E_INVALIDARG

typedef struct IDirectSoundBuffer IDirectSoundBuffer;
typedef IDirectSoundBuffer* LPDIRECTSOUNDBUFFER;

struct IDirectSound8 : public IUnknown {
    virtual HRESULT CreateSoundBuffer(LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER* buffer, IUnknown* outer) = 0;
    virtual HRESULT GetCaps(LPDSCAPS caps) = 0;
    virtual HRESULT DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER original, LPDIRECTSOUNDBUFFER* duplicate) = 0;
    virtual HRESULT SetCooperativeLevel(HWND window, DWORD level) = 0;
    virtual HRESULT Compact() = 0;
    virtual HRESULT GetSpeakerConfig(LPDWORD config) = 0;
    virtual HRESULT SetSpeakerConfig(DWORD config) = 0;
    virtual HRESULT Initialize(const GUID* device) = 0;
    virtual HRESULT VerifyCertification(LPDWORD certified) = 0;
};
typedef IDirectSound8* LPDIRECTSOUND8;

struct IDirectSoundBuffer : public IUnknown {
    virtual HRESULT GetCaps(void* caps) = 0;
    virtual HRESULT GetCurrentPosition(LPDWORD play, LPDWORD write) = 0;
    virtual HRESULT GetFormat(LPWAVEFORMATEX format, DWORD size, LPDWORD written) = 0;
    virtual HRESULT GetVolume(LPLONG volume) = 0;
    virtual HRESULT GetPan(LPLONG pan) = 0;
    virtual HRESULT GetFrequency(LPDWORD frequency) = 0;
    virtual HRESULT GetStatus(LPDWORD status) = 0;
    virtual HRESULT Initialize(IDirectSound8* sound, LPCDSBUFFERDESC desc) = 0;
    virtual HRESULT Lock(DWORD offset, DWORD bytes, LPVOID* first, LPDWORD firstBytes, LPVOID* second,
                         LPDWORD secondBytes, DWORD flags) = 0;
    virtual HRESULT Play(DWORD reserved, DWORD priority, DWORD flags) = 0;
    virtual HRESULT SetCurrentPosition(DWORD position) = 0;
    virtual HRESULT SetFormat(LPCWAVEFORMATEX format) = 0;
    virtual HRESULT SetVolume(LONG volume) = 0;
    virtual HRESULT SetPan(LONG pan) = 0;
    virtual HRESULT SetFrequency(DWORD frequency) = 0;
    virtual HRESULT Stop() = 0;
    virtual HRESULT Unlock(LPVOID first, DWORD firstBytes, LPVOID second, DWORD secondBytes) = 0;
    virtual HRESULT Restore() = 0;
};

extern "C" HRESULT WINAPI DirectSoundCreate8(const GUID* device, LPDIRECTSOUND8* sound, IUnknown* outer);
extern "C" HRESULT WINAPI DirectSoundEnumerateA(BOOL(CALLBACK* callback)(GUID*, const char*, const char*, void*),
                                                void* context);
