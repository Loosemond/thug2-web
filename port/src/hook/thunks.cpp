// The original's tail jumps through `this`'s vtable: a slot's function shared by classes the linker folded into one
// jump, `mov eax, [ecx]; jmp [eax + 4 * slot]` (upstream's VirtualSlot<n>Jump and the like, naked x86 there, and
// registered unchecked: the web build leaves them out). Here a forwarder per slot, portable because of the
// function-pointer cast emulation (port/CMakeLists.txt): every function in the table takes i64s, so a forwarder
// taking and passing on more arguments than any slot has hands each one over unchanged - floats as their bits, the
// ones it isn't given as 0, to a slot that ignores them - and the slot's result back the same way.

#include <cstdint>

#include "hook/hook.h"

#if defined(THUG2_FPCAST_EMU)

namespace {

using Word = int64_t;
using Slot = Word (*)(Word, Word, Word, Word, Word, Word, Word, Word, Word, Word, Word, Word);

template <int Index>
Word VirtualSlotJump(Word self, Word a1, Word a2, Word a3, Word a4, Word a5, Word a6, Word a7, Word a8, Word a9,
                     Word a10, Word a11)
{
    void** vtable = *reinterpret_cast<void***>(static_cast<uintptr_t>(static_cast<uint32_t>(self)));
    return reinterpret_cast<Slot>(vtable[Index])(self, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
}

} // namespace

REPLACE(0x00456B30, VirtualSlotJump<5>); // Net::VirtualSlot5Jump
REPLACE(0x00487D50, VirtualSlotJump<4>); // Gfx::VirtualSlot4Jump
REPLACE(0x0048D060, VirtualSlotJump<8>); // Nx::VirtualSlot8Jump
REPLACE(0x0048DE30, VirtualSlotJump<9>); // Nx::VirtualSlot9Jump
REPLACE(0x0048DE40, VirtualSlotJump<11>); // Nx::VirtualSlot11Jump
REPLACE(0x0048DE50, VirtualSlotJump<12>); // Nx::VirtualSlot12Jump
REPLACE(0x0048DE60, VirtualSlotJump<13>); // Nx::VirtualSlot13Jump
REPLACE(0x0048DE70, VirtualSlotJump<14>); // Nx::VirtualSlot14Jump
REPLACE(0x0048DE80, VirtualSlotJump<15>); // Nx::VirtualSlot15Jump
REPLACE(0x0048DEB0, VirtualSlotJump<16>); // Nx::VirtualSlot16Jump
REPLACE(0x0048DEC0, VirtualSlotJump<18>); // Nx::VirtualSlot18Jump
REPLACE(0x0048DED0, VirtualSlotJump<19>); // Nx::VirtualSlot19Jump
REPLACE(0x0048DEE0, VirtualSlotJump<20>); // Nx::VirtualSlot20Jump
REPLACE(0x0048DEF0, VirtualSlotJump<22>); // Nx::VirtualSlot22Jump
REPLACE(0x0048DF00, VirtualSlotJump<25>); // Nx::VirtualSlot25Jump
REPLACE(0x0048DF20, VirtualSlotJump<27>); // Nx::VirtualSlot27Jump
REPLACE(0x0048DF30, VirtualSlotJump<28>); // Nx::VirtualSlot28Jump
REPLACE(0x0048DF40, VirtualSlotJump<29>); // Nx::VirtualSlot29Jump
REPLACE(0x0048DF50, VirtualSlotJump<30>); // Nx::VirtualSlot30Jump
REPLACE(0x0048DF60, VirtualSlotJump<31>); // Nx::VirtualSlot31Jump
REPLACE(0x0048DF70, VirtualSlotJump<36>); // Nx::VirtualSlot36Jump
REPLACE(0x0048DF80, VirtualSlotJump<37>); // Nx::VirtualSlot37Jump
REPLACE(0x0048DF90, VirtualSlotJump<39>); // Nx::VirtualSlot39Jump
REPLACE(0x0048DFA0, VirtualSlotJump<40>); // Nx::VirtualSlot40Jump
REPLACE(0x0048DFB0, VirtualSlotJump<42>); // Nx::VirtualSlot42Jump
REPLACE(0x0048DFC0, VirtualSlotJump<43>); // Nx::VirtualSlot43Jump
REPLACE(0x0048DFD0, VirtualSlotJump<45>); // Nx::VirtualSlot45Jump
REPLACE(0x0048DFE0, VirtualSlotJump<44>); // Nx::VirtualSlot44Jump
REPLACE(0x0048DFF0, VirtualSlotJump<46>); // Nx::VirtualSlot46Jump
REPLACE(0x0048E000, VirtualSlotJump<47>); // Nx::VirtualSlot47Jump
REPLACE(0x0048E010, VirtualSlotJump<48>); // Nx::VirtualSlot48Jump
REPLACE(0x0048E020, VirtualSlotJump<49>); // Nx::VirtualSlot49Jump
REPLACE(0x0048E030, VirtualSlotJump<50>); // Nx::VirtualSlot50Jump
REPLACE(0x0048E4B0, VirtualSlotJump<7>); // Nx::VirtualSlot7Jump
REPLACE(0x0048E4C0, VirtualSlotJump<10>); // Nx::VirtualSlot10Jump
REPLACE(0x0049C6F0, VirtualSlotJump<35>); // Nx::CallVirtual35Stub
REPLACE(0x0049F360, VirtualSlotJump<17>); // Nx::VirtualSlot17Jump
REPLACE(0x0049F370, VirtualSlotJump<21>); // Nx::VirtualSlot21Jump
REPLACE(0x0049F380, VirtualSlotJump<23>); // Nx::VirtualSlot23Jump
REPLACE(0x0049F390, VirtualSlotJump<24>); // Nx::VirtualSlot24Jump
REPLACE(0x0049F3A0, VirtualSlotJump<26>); // Nx::VirtualSlot26Jump
REPLACE(0x0049F3B0, VirtualSlotJump<32>); // Nx::VirtualSlot32Jump
REPLACE(0x0049F3C0, VirtualSlotJump<33>); // Nx::VirtualSlot33Jump
REPLACE(0x0049F3D0, VirtualSlotJump<34>); // Nx::VirtualSlot34Jump
REPLACE(0x0049F3E0, VirtualSlotJump<38>); // Nx::VirtualSlot38Jump
REPLACE(0x0049F420, VirtualSlotJump<41>); // Nx::VirtualSlot41Jump
REPLACE(0x004A1090, VirtualSlotJump<1>); // Nx::VirtualSlot1Jump
REPLACE(0x004A10A0, VirtualSlotJump<3>); // Nx::VirtualSlot3Jump
REPLACE(0x004E3F90, VirtualSlotJump<2>); // Obj::RefreshFromStructure

#endif
