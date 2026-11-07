#pragma once
// thug2-web: DirectInput 8 (ANSI), as far as the game uses it. Interface method order follows the SDK.
// On the web the game reads input through SDL (sio/sdl_input.cpp); DirectInput8Create fails.
#include <windows.h>

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif

typedef struct DIJOYSTATE2 {
    LONG lX, lY, lZ;
    LONG lRx, lRy, lRz;
    LONG rglSlider[2];
    DWORD rgdwPOV[4];
    BYTE rgbButtons[128];
    LONG lVX, lVY, lVZ;
    LONG lVRx, lVRy, lVRz;
    LONG rglVSlider[2];
    LONG lAX, lAY, lAZ;
    LONG lARx, lARy, lARz;
    LONG rglASlider[2];
    LONG lFX, lFY, lFZ;
    LONG lFRx, lFRy, lFRz;
    LONG rglFSlider[2];
} DIJOYSTATE2, *LPDIJOYSTATE2;

typedef struct DIMOUSESTATE2 {
    LONG lX;
    LONG lY;
    LONG lZ;
    BYTE rgbButtons[8];
} DIMOUSESTATE2, *LPDIMOUSESTATE2;

typedef struct _DIOBJECTDATAFORMAT {
    const GUID* pguid;
    DWORD dwOfs;
    DWORD dwType;
    DWORD dwFlags;
} DIOBJECTDATAFORMAT, *LPDIOBJECTDATAFORMAT;
typedef const DIOBJECTDATAFORMAT* LPCDIOBJECTDATAFORMAT;

typedef struct _DIDATAFORMAT {
    DWORD dwSize;
    DWORD dwObjSize;
    DWORD dwFlags;
    DWORD dwDataSize;
    DWORD dwNumObjs;
    LPDIOBJECTDATAFORMAT rgodf;
} DIDATAFORMAT, *LPDIDATAFORMAT;
typedef const DIDATAFORMAT* LPCDIDATAFORMAT;

typedef struct DIPROPHEADER {
    DWORD dwSize;
    DWORD dwHeaderSize;
    DWORD dwObj;
    DWORD dwHow;
} DIPROPHEADER, *LPDIPROPHEADER;
typedef const DIPROPHEADER* LPCDIPROPHEADER;

typedef struct DIPROPDWORD {
    DIPROPHEADER diph;
    DWORD dwData;
} DIPROPDWORD, *LPDIPROPDWORD;

typedef struct DIPROPRANGE {
    DIPROPHEADER diph;
    LONG lMin;
    LONG lMax;
} DIPROPRANGE, *LPDIPROPRANGE;

typedef struct DIENVELOPE {
    DWORD dwSize;
    DWORD dwAttackLevel;
    DWORD dwAttackTime;
    DWORD dwFadeLevel;
    DWORD dwFadeTime;
} DIENVELOPE, *LPDIENVELOPE;

typedef struct DICONSTANTFORCE {
    LONG lMagnitude;
} DICONSTANTFORCE, *LPDICONSTANTFORCE;

typedef struct DIEFFECT {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwDuration;
    DWORD dwSamplePeriod;
    DWORD dwGain;
    DWORD dwTriggerButton;
    DWORD dwTriggerRepeatInterval;
    DWORD cAxes;
    LPDWORD rgdwAxes;
    LPLONG rglDirection;
    LPDIENVELOPE lpEnvelope;
    DWORD cbTypeSpecificParams;
    LPVOID lpvTypeSpecificParams;
    DWORD dwStartDelay;
} DIEFFECT, *LPDIEFFECT;
typedef const DIEFFECT* LPCDIEFFECT;

typedef struct DIDEVICEINSTANCEA {
    DWORD dwSize;
    GUID guidInstance;
    GUID guidProduct;
    DWORD dwDevType;
    CHAR tszInstanceName[MAX_PATH];
    CHAR tszProductName[MAX_PATH];
    GUID guidFFDriver;
    WORD wUsagePage;
    WORD wUsage;
} DIDEVICEINSTANCEA, *LPDIDEVICEINSTANCEA;
typedef const DIDEVICEINSTANCEA* LPCDIDEVICEINSTANCEA;
typedef DIDEVICEINSTANCEA DIDEVICEINSTANCE;
typedef LPCDIDEVICEINSTANCEA LPCDIDEVICEINSTANCE;

typedef struct DIDEVICEOBJECTINSTANCEA {
    DWORD dwSize;
    GUID guidType;
    DWORD dwOfs;
    DWORD dwType;
    DWORD dwFlags;
    CHAR tszName[MAX_PATH];
    DWORD dwFFMaxForce;
    DWORD dwFFForceResolution;
    WORD wCollectionNumber;
    WORD wDesignatorIndex;
    WORD wUsagePage;
    WORD wUsage;
    DWORD dwDimension;
    WORD wExponent;
    WORD wReportId;
} DIDEVICEOBJECTINSTANCEA, *LPDIDEVICEOBJECTINSTANCEA;
typedef const DIDEVICEOBJECTINSTANCEA* LPCDIDEVICEOBJECTINSTANCEA;
typedef DIDEVICEOBJECTINSTANCEA DIDEVICEOBJECTINSTANCE;
typedef LPCDIDEVICEOBJECTINSTANCEA LPCDIDEVICEOBJECTINSTANCE;

typedef struct DIDEVICEOBJECTDATA {
    DWORD dwOfs;
    DWORD dwData;
    DWORD dwTimeStamp;
    DWORD dwSequence;
    UINT_PTR uAppData;
} DIDEVICEOBJECTDATA, *LPDIDEVICEOBJECTDATA;

typedef BOOL(CALLBACK* LPDIENUMDEVICESCALLBACKA)(LPCDIDEVICEINSTANCEA instance, LPVOID context);
typedef BOOL(CALLBACK* LPDIENUMDEVICEOBJECTSCALLBACKA)(LPCDIDEVICEOBJECTINSTANCEA object, LPVOID context);
#define LPDIENUMDEVICESCALLBACK LPDIENUMDEVICESCALLBACKA
#define LPDIENUMDEVICEOBJECTSCALLBACK LPDIENUMDEVICEOBJECTSCALLBACKA

// Properties are identified by small integers cast to GUID pointers.
#define MAKEDIPROP(prop) (*(const GUID*)(prop))
#define DIPROP_BUFFERSIZE MAKEDIPROP(1)
#define DIPROP_AXISMODE MAKEDIPROP(2)
#define DIPROP_GRANULARITY MAKEDIPROP(3)
#define DIPROP_RANGE MAKEDIPROP(4)
#define DIPROP_DEADZONE MAKEDIPROP(5)
#define DIPROP_SATURATION MAKEDIPROP(6)
#define DIPROP_FFGAIN MAKEDIPROP(7)
#define DIPROP_AUTOCENTER MAKEDIPROP(9)

#define DIPH_DEVICE 0
#define DIPH_BYOFFSET 1
#define DIPH_BYID 2
#define DIPH_BYUSAGE 3

#define DISCL_EXCLUSIVE 0x1
#define DISCL_NONEXCLUSIVE 0x2
#define DISCL_FOREGROUND 0x4
#define DISCL_BACKGROUND 0x8
#define DISCL_NOWINKEY 0x10

#define DIENUM_STOP 0
#define DIENUM_CONTINUE 1

#define DI8DEVCLASS_ALL 0
#define DI8DEVCLASS_DEVICE 1
#define DI8DEVCLASS_POINTER 2
#define DI8DEVCLASS_KEYBOARD 3
#define DI8DEVCLASS_GAMECTRL 4
#define DIEDFL_ALLDEVICES 0x0
#define DIEDFL_ATTACHEDONLY 0x1
#define DIEDFL_FORCEFEEDBACK 0x100

#define DIDFT_ALL 0x0
#define DIDFT_RELAXIS 0x1
#define DIDFT_ABSAXIS 0x2
#define DIDFT_AXIS 0x3
#define DIDFT_PSHBUTTON 0x4
#define DIDFT_TGLBUTTON 0x8
#define DIDFT_BUTTON 0xC
#define DIDFT_POV 0x10
#define DIDFT_FFACTUATOR 0x01000000
#define DIDFT_GETINSTANCE(n) LOWORD((n) >> 8)
#define DIDOI_FFACTUATOR 0x00000001
#define DIDOI_FFEFFECTTRIGGER 0x00000002
#define DI8DEVTYPE_DEVICE 0x11
#define DI8DEVTYPE_MOUSE 0x12
#define DI8DEVTYPE_KEYBOARD 0x13
#define DI8DEVTYPE_JOYSTICK 0x14
#define DI8DEVTYPE_GAMEPAD 0x15
#define DI8DEVTYPE_DRIVING 0x16
#define DI8DEVTYPE_FLIGHT 0x17
#define DI8DEVTYPE_1STPERSON 0x18
#define GET_DIDEVICE_TYPE(dwDevType) LOBYTE(dwDevType)
#define GET_DIDEVICE_SUBTYPE(dwDevType) HIBYTE(dwDevType)
#define DIJOFS_X 0x00
#define DIJOFS_Y 0x04
#define DIJOFS_Z 0x08
#define DIJOFS_RX 0x0C
#define DIJOFS_RY 0x10
#define DIJOFS_RZ 0x14
#define DIJOFS_SLIDER(n) (0x18 + (n) * 4)
#define DIJOFS_POV(n) (0x20 + (n) * 4)
#define DIJOFS_BUTTON(n) (0x30 + (n))

#define DIEFF_OBJECTIDS 0x1
#define DIEFF_OBJECTOFFSETS 0x2
#define DIEFF_CARTESIAN 0x10
#define DIEFF_POLAR 0x20
#define DIEFF_SPHERICAL 0x40
#define DIEP_DURATION 0x1
#define DIEP_SAMPLEPERIOD 0x2
#define DIEP_GAIN 0x4
#define DIEP_TRIGGERBUTTON 0x8
#define DIEP_TRIGGERREPEATINTERVAL 0x10
#define DIEP_AXES 0x20
#define DIEP_DIRECTION 0x40
#define DIEP_ENVELOPE 0x80
#define DIEP_TYPESPECIFICPARAMS 0x100
#define DIEP_STARTDELAY 0x200
#define DIEP_START 0x20000000
#define DIEP_NORESTART 0x40000000
#define DIEP_NODOWNLOAD 0x80000000
#define DIES_SOLO 0x1
#define DIES_NODOWNLOAD 0x80000000
#define DIEB_NOTRIGGER 0xFFFFFFFF
#define DI_FFNOMINALMAX 10000
#define DI_SECONDS 1000000
#define DI_DEGREES 100

#define DI_OK S_OK
#define DI_NOEFFECT S_FALSE
#define DI_POLLEDDEVICE ((HRESULT)0x00000002)
#define DIERR_NOTACQUIRED ((HRESULT)0x8007000C)
#define DIERR_INPUTLOST ((HRESULT)0x8007001E)
#define DIERR_OTHERAPPHASPRIO ((HRESULT)0x80070005)
#define DIERR_NOTINITIALIZED ((HRESULT)0x80070015)
#define DIERR_DEVICENOTREG ((HRESULT)0x80040154)

#define DIK_ESCAPE 0x01
#define DIK_1 0x02
#define DIK_2 0x03
#define DIK_3 0x04
#define DIK_4 0x05
#define DIK_5 0x06
#define DIK_6 0x07
#define DIK_7 0x08
#define DIK_8 0x09
#define DIK_9 0x0A
#define DIK_0 0x0B
#define DIK_MINUS 0x0C
#define DIK_EQUALS 0x0D
#define DIK_BACK 0x0E
#define DIK_TAB 0x0F
#define DIK_Q 0x10
#define DIK_W 0x11
#define DIK_E 0x12
#define DIK_R 0x13
#define DIK_T 0x14
#define DIK_Y 0x15
#define DIK_U 0x16
#define DIK_I 0x17
#define DIK_O 0x18
#define DIK_P 0x19
#define DIK_LBRACKET 0x1A
#define DIK_RBRACKET 0x1B
#define DIK_RETURN 0x1C
#define DIK_LCONTROL 0x1D
#define DIK_A 0x1E
#define DIK_S 0x1F
#define DIK_D 0x20
#define DIK_F 0x21
#define DIK_G 0x22
#define DIK_H 0x23
#define DIK_J 0x24
#define DIK_K 0x25
#define DIK_L 0x26
#define DIK_SEMICOLON 0x27
#define DIK_APOSTROPHE 0x28
#define DIK_GRAVE 0x29
#define DIK_LSHIFT 0x2A
#define DIK_BACKSLASH 0x2B
#define DIK_Z 0x2C
#define DIK_X 0x2D
#define DIK_C 0x2E
#define DIK_V 0x2F
#define DIK_B 0x30
#define DIK_N 0x31
#define DIK_M 0x32
#define DIK_COMMA 0x33
#define DIK_PERIOD 0x34
#define DIK_SLASH 0x35
#define DIK_RSHIFT 0x36
#define DIK_MULTIPLY 0x37
#define DIK_LMENU 0x38
#define DIK_SPACE 0x39
#define DIK_CAPITAL 0x3A
#define DIK_F1 0x3B
#define DIK_F2 0x3C
#define DIK_F3 0x3D
#define DIK_F4 0x3E
#define DIK_F5 0x3F
#define DIK_F6 0x40
#define DIK_F7 0x41
#define DIK_F8 0x42
#define DIK_F9 0x43
#define DIK_F10 0x44
#define DIK_NUMLOCK 0x45
#define DIK_SCROLL 0x46
#define DIK_NUMPAD7 0x47
#define DIK_NUMPAD8 0x48
#define DIK_NUMPAD9 0x49
#define DIK_SUBTRACT 0x4A
#define DIK_NUMPAD4 0x4B
#define DIK_NUMPAD5 0x4C
#define DIK_NUMPAD6 0x4D
#define DIK_ADD 0x4E
#define DIK_NUMPAD1 0x4F
#define DIK_NUMPAD2 0x50
#define DIK_NUMPAD3 0x51
#define DIK_NUMPAD0 0x52
#define DIK_DECIMAL 0x53
#define DIK_F11 0x57
#define DIK_F12 0x58
#define DIK_NUMPADENTER 0x9C
#define DIK_RCONTROL 0x9D
#define DIK_DIVIDE 0xB5
#define DIK_SYSRQ 0xB7
#define DIK_RMENU 0xB8
#define DIK_PAUSE 0xC5
#define DIK_HOME 0xC7
#define DIK_UP 0xC8
#define DIK_PRIOR 0xC9
#define DIK_LEFT 0xCB
#define DIK_RIGHT 0xCD
#define DIK_END 0xCF
#define DIK_DOWN 0xD0
#define DIK_NEXT 0xD1
#define DIK_INSERT 0xD2
#define DIK_DELETE 0xD3
#define DIK_LWIN 0xDB
#define DIK_RWIN 0xDC
#define DIK_APPS 0xDD

struct IDirectInputEffect : public IUnknown {
    virtual HRESULT Initialize(HINSTANCE instance, DWORD version, REFGUID guid) = 0;
    virtual HRESULT GetEffectGuid(GUID* guid) = 0;
    virtual HRESULT GetParameters(LPDIEFFECT effect, DWORD flags) = 0;
    virtual HRESULT SetParameters(LPCDIEFFECT effect, DWORD flags) = 0;
    virtual HRESULT Start(DWORD iterations, DWORD flags) = 0;
    virtual HRESULT Stop() = 0;
    virtual HRESULT GetEffectStatus(LPDWORD status) = 0;
    virtual HRESULT Download() = 0;
    virtual HRESULT Unload() = 0;
    virtual HRESULT Escape(void* escape) = 0;
};
typedef IDirectInputEffect* LPDIRECTINPUTEFFECT;

struct IDirectInputDevice8A : public IUnknown {
    virtual HRESULT GetCapabilities(void* caps) = 0;
    virtual HRESULT EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA callback, LPVOID context, DWORD flags) = 0;
    virtual HRESULT GetProperty(REFGUID property, LPDIPROPHEADER header) = 0;
    virtual HRESULT SetProperty(REFGUID property, LPCDIPROPHEADER header) = 0;
    virtual HRESULT Acquire() = 0;
    virtual HRESULT Unacquire() = 0;
    virtual HRESULT GetDeviceState(DWORD size, LPVOID data) = 0;
    virtual HRESULT GetDeviceData(DWORD size, LPDIDEVICEOBJECTDATA data, LPDWORD count, DWORD flags) = 0;
    virtual HRESULT SetDataFormat(LPCDIDATAFORMAT format) = 0;
    virtual HRESULT SetEventNotification(HANDLE event) = 0;
    virtual HRESULT SetCooperativeLevel(HWND window, DWORD flags) = 0;
    virtual HRESULT GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA object, DWORD id, DWORD how) = 0;
    virtual HRESULT GetDeviceInfo(LPDIDEVICEINSTANCEA instance) = 0;
    virtual HRESULT RunControlPanel(HWND owner, DWORD flags) = 0;
    virtual HRESULT Initialize(HINSTANCE instance, DWORD version, REFGUID guid) = 0;
    virtual HRESULT CreateEffect(REFGUID guid, LPCDIEFFECT effect, LPDIRECTINPUTEFFECT* created, IUnknown* outer) = 0;
    virtual HRESULT EnumEffects(void* callback, LPVOID context, DWORD type) = 0;
    virtual HRESULT GetEffectInfo(void* info, REFGUID guid) = 0;
    virtual HRESULT GetForceFeedbackState(LPDWORD state) = 0;
    virtual HRESULT SendForceFeedbackCommand(DWORD command) = 0;
    virtual HRESULT EnumCreatedEffectObjects(void* callback, LPVOID context, DWORD flags) = 0;
    virtual HRESULT Escape(void* escape) = 0;
    virtual HRESULT Poll() = 0;
    virtual HRESULT SendDeviceData(DWORD size, const DIDEVICEOBJECTDATA* data, LPDWORD count, DWORD flags) = 0;
    virtual HRESULT EnumEffectsInFile(LPCSTR file, void* callback, LPVOID context, DWORD flags) = 0;
    virtual HRESULT WriteEffectToFile(LPCSTR file, DWORD count, void* effects, DWORD flags) = 0;
    virtual HRESULT BuildActionMap(void* format, LPCSTR user, DWORD flags) = 0;
    virtual HRESULT SetActionMap(void* format, LPCSTR user, DWORD flags) = 0;
    virtual HRESULT GetImageInfo(void* header) = 0;
};
typedef IDirectInputDevice8A* LPDIRECTINPUTDEVICE8A;
typedef IDirectInputDevice8A IDirectInputDevice8;
typedef LPDIRECTINPUTDEVICE8A LPDIRECTINPUTDEVICE8;

struct IDirectInput8A : public IUnknown {
    virtual HRESULT CreateDevice(REFGUID guid, LPDIRECTINPUTDEVICE8A* device, IUnknown* outer) = 0;
    virtual HRESULT EnumDevices(DWORD type, LPDIENUMDEVICESCALLBACKA callback, LPVOID context, DWORD flags) = 0;
    virtual HRESULT GetDeviceStatus(REFGUID guid) = 0;
    virtual HRESULT RunControlPanel(HWND owner, DWORD flags) = 0;
    virtual HRESULT Initialize(HINSTANCE instance, DWORD version) = 0;
    virtual HRESULT FindDevice(REFGUID guidClass, LPCSTR name, GUID* guidInstance) = 0;
    virtual HRESULT EnumDevicesBySemantics(LPCSTR user, void* format, void* callback, LPVOID context, DWORD flags) = 0;
    virtual HRESULT ConfigureDevices(void* callback, void* parameters, DWORD flags, LPVOID data) = 0;
};
typedef IDirectInput8A* LPDIRECTINPUT8A;
typedef IDirectInput8A IDirectInput8;
typedef LPDIRECTINPUT8A LPDIRECTINPUT8;

extern "C" {
extern const GUID GUID_SysKeyboard;
extern const GUID GUID_SysMouse;
extern const GUID GUID_Joystick;
extern const GUID GUID_ConstantForce;
extern const GUID GUID_XAxis;
extern const GUID GUID_YAxis;
extern const GUID IID_IDirectInput8A;
extern const DIDATAFORMAT c_dfDIKeyboard;
extern const DIDATAFORMAT c_dfDIMouse2;
extern const DIDATAFORMAT c_dfDIJoystick2;
HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* result, IUnknown* outer);
}
