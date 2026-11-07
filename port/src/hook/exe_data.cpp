// What the source knows of THUG2.exe's .data, put in place when the exe's copy is encrypted (a retail CD build:
// exe_image.cpp leaves it zeroed). The GAME_GLOBALs' start values are there already (written as each was defined);
// this adds the tables the script system reads at start-up, which aren't globals of the source yet. Their names are
// the exe's own strings: MSVC put each table's names together in .rdata (which isn't encrypted), in reverse order.
//   - the script functions (0x0067F3A0, 1254 x {name, function}): the 1250 strings at 0x006514BC-0x0065762C are
//     entries 1252 down to 3 (CD at 371, NotCD at 372 and IsPS2 at 1049 - which other code finds by address - land
//     where the original had them; entries 0-2 and 1253 point at strings elsewhere and repeat a neighbour). Each
//     name's function: the REPLACE line of its CFunc_<Name>; else a second name of a rewritten function (the
//     source's comments); else, a command the linker folded into the shared stubs (stubs.cpp) - a question (Is...,
//     Has..., Card...) false, anything else true.
//   - the member function names (0x00681AD0, 250 names): the 250 strings at 0x00650350, just before.
//   - the member function tables the source reads by address (the object's, the composite object's, the skater's
//     core physics, walk, vehicle camera and balance trick: {name, component type, function, 0...} up to a zero
//     name - in the original their functions put in by static initialisers): member_tables.inc, generated from
//     upstream's headers by port/tools/member_tables.py. Each fills its space up to the next known variable.
//   - the member functions of the tables the source defines (GAME_GLOBAL g_*MemberFunctions: names and component
//     types only): each name's function, from the same headers (member_tables.inc's kMemberCode).
// And the small tables the source reads by address (RebuildTables: each says what it's known from - the source, or,
// marked "inferred", the way it's used).
// Then the share of the initialised .data the source's values covered is logged.

#include <algorithm>
#include <iterator>
#include <map>
#include <new>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>


#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/pc_texture.h"
#include "game/gfx/scene_lights.h"
#include "game/lst/hashtable.h"
#include "hook/game.h"
#include "hook/web_hook.h"
#include "thug2web/exe_data.h"

namespace {

constexpr uint32_t kRdataStart = 0x00645000, kRdataEnd = 0x0067B5D5;
constexpr uint32_t kDataStart = 0x0067C000, kDataInitialisedEnd = 0x00690000; // (the raw part: the rest is .bss)
constexpr uint32_t kCFunctions = 0x0067F3A0, kCFunctionCount = 1254;
constexpr uint32_t kMemberNames = 0x00681AD0, kMemberNameCount = 250;

struct Entry {
    uint32_t name;
    uint32_t function;
};

constexpr uint32_t kScriptNameStrings = 0x006514BC, kScriptNameStringsEnd = 0x00657634;
constexpr uint32_t kMemberNameStrings = 0x00650350;
constexpr uint32_t kReturnTrue = 0x005B9760, kReturnFalse = 0x005B9E50; // (Stubs::ReturnTrue / ReturnFalse)

// "<Name>" for a function named "...CFunc_<Name>", else empty.
std::string ScriptName(const char* function)
{
    const char* at = std::strstr(function, "CFunc_");
    if (!at)
        return {};
    at += 6;
    size_t length = 0;
    while (at[length] == '_' || (at[length] >= '0' && at[length] <= '9') || (at[length] >= 'A' && at[length] <= 'Z') ||
           (at[length] >= 'a' && at[length] <= 'z'))
        ++length;
    return std::string(at, length);
}

// The strings from `at` (each NUL-ended, the next after its padding): up to `end`, or `most` of them.
std::vector<uint32_t> Strings(uint32_t at, uint32_t end, size_t most)
{
    std::vector<uint32_t> strings;
    const auto* p = reinterpret_cast<const char*>(uintptr_t{at});
    const auto* stop = reinterpret_cast<const char*>(uintptr_t{end});
    while (p < stop && strings.size() < most) {
        while (p < stop && !*p)
            ++p;
        if (p == stop)
            break;
        strings.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)));
        p += std::strlen(p) + 1;
    }
    return strings;
}

// "<name>\0" in the exe's read-only data (as a whole string, not the end of a longer one), or 0.
uint32_t FindInRdata(const std::string& name)
{
    const auto* begin = reinterpret_cast<const char*>(uintptr_t{kRdataStart});
    const auto* end = reinterpret_cast<const char*>(uintptr_t{kRdataEnd});
    const char* at = std::search(begin, end, name.c_str(), name.c_str() + name.size() + 1);
    while (at != end && at > begin && at[-1] != '\0')
        at = std::search(at + 1, end, name.c_str(), name.c_str() + name.size() + 1);
    return at == end ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(at));
}

const char* Text(uint32_t address) { return reinterpret_cast<const char*>(uintptr_t{address}); }

// A folded command's stub: a question false, anything else true.
uint32_t StubFor(const char* name)
{
    for (const char* exception : {"PAL", "NodeArrayBusy"}) // (questions too)
        if (std::strcmp(name, exception) == 0)
            return kReturnFalse;
    for (const char* prefix : {"Is", "Has", "Check", "Card", "Bad", "Already", "System", "Network", "MusicIs", "Got"}) {
        size_t n = std::strlen(prefix);
        if (std::strncmp(name, prefix, n) == 0 && name[n] >= 'A' && name[n] <= 'Z')
            return kReturnFalse;
    }
    return kReturnTrue;
}

void RebuildScriptFunctions()
{
    std::map<std::string, uint32_t> functions; // name -> address
    Thug2Web::ForEachReplacement(
        [](uint32_t address, const char* function, void* context) {
            std::string name = ScriptName(function);
            if (!name.empty())
                static_cast<std::map<std::string, uint32_t>*>(context)->emplace(name, address);
        },
        &functions);
    // A rewritten function's second name (from the source's comments: the command's other name, a declaration's
    // address).
    struct Alias {
        const char* name;
        uint32_t function;
    };
    static const Alias kAliases[] = {
        {"CD", kReturnTrue},     // (the disc build: true; debug_mode.cpp points it at its own in debug mode)
        {"NotCD", kReturnFalse},
        {"IfDebugOff", 0x0044A930},
        {"SetException", 0x0044ABD0},
        {"FireEvent", 0x004634D0},
        {"ProfileLoggedIn", 0x00548360},
        {"SendBuddyMessage", 0x00548370},
        {"SetLobbyStatus", 0x005483E0},
        {"SetUniqueId", 0x00548940},
        {"JoinBuddy", 0x00548A40},
        {"AddBuddy", 0x00548E40},
        {"HasBuddies", 0x00548E90},
        {"BuddyListFull", 0x00548ED0},
        {"AnyBuddyMessagesLeft", 0x00548F10},
        {"ProfileLogOff", 0x00549D10},
        {"FillBuddyList", 0x00549D20},
        {"CancelAddBuddy", 0x00549D80},
        {"RemoveBuddy", 0x00549D90},
        {"ShowNextBuddyMessage", 0x00549DC0},
        {"IsAlreadyMyBuddy", 0x00549EA0},
        {"ProfileLogIn", 0x0054A250},
        {"FillProspectiveBuddyList", 0x0054A2B0},
        {"CreateProfile", 0x0054A650},
        {"DownloadFile", 0x0054AA10},
        {"UploadFile", 0x0054B060},
        {"CancelFileTransfer", 0x0054B240},
        {"DownloadDirectoryList", 0x0054C290},
        {"FreeDirectoryListing", 0x0054C460},
        {"FillVaultMenu", 0x0054C470},
        {"NextVaultCategory", 0x0054C770},
        {"PrevVaultCategory", 0x0054C800},
        {"StatsLoggedIn", 0x0054CC90},
        {"StatsLogIn", 0x0054CCB0},
        {"StatsLogOff", 0x0054CCC0},
        {"RetrievePersonalStats", 0x0054CE00},
        {"NeedToRetrieveTopStats", 0x0054CE70},
        {"ReportStats", 0x0054D670},
        {"GetRank", 0x0054D6A0},
        {"RetrieveTopStats", 0x0054DCB0},
        {"CleanUpTopStats", 0x0054EA40},
        {"FillStatsArrays", 0x0054EB00},
        {"GoalManager_NextHorseSpot", 0x00559930},
        {"GoalManager_NextTourSpot", 0x00559930},
        {"Goal_Collect_Active", 0x0055B930},
        {"Goal_Gaps_Active", 0x0055B930},
        {"SkateLetters_Active", 0x0055B930},
        {"PlayMovie", 0x0059F0C0},
        {"SaveFailedDueToInsufficientSpace", 0x005A6550},
        {"SetSectionsToApplyWhenLoading", 0x005A6560},
        {"LoadedCustomSkater", 0x005A6600},
        {"GetMostRecentSave", 0x005A6D30},
        {"GetMemCardSpaceAvailable", 0x005A6E40},
        {"DeleteMemCardFile", 0x005A6EA0},
        {"FormatCard", 0x005A7210},
        {"CardIsInSlot", 0x005A7260},
        {"CardIsFormatted", 0x005A7280},
        {"GetSummaryInfo", 0x005A72A0},
        {"GetMemCardDataForUpload", 0x005A7430},
        {"GetMaxTHPS4FilesAllowed", 0x005A7450},
        {"SwitchToTempPoolsIfTheyExist", 0x005A7470},
        {"SwitchToRegularPools", 0x005A74F0},
        {"GetMemCardSpaceRequired", 0x005A8500},
        {"MemCardFileExists", 0x005A8550},
        {"LoadFromMemoryCard", 0x005A8730},
        {"ClearMemCardDataForUpload", 0x005A8BB0},
        {"GetSaveInfo", 0x005A8BF0},
        {"CreateTemporaryMemCardPools", 0x005A8DA0},
        {"GetMemCardDirectoryListing", 0x005A8F70},
        {"RemoveTemporaryMemCardPools", 0x005A9700},
        {"SaveToMemoryCard", 0x005A97E0},
        {"SetCutsceneSkippable", 0x005AD960},
        {"SetCutsceneShouldPause", 0x005ADA30},
        {"CutsceneFinished", 0x005ADC10},
        {"CutsceneHeld", 0x005ADCC0},
        {"KillCutscene", 0x005ADD70},
        {"IsNGC", 0x005B9E50},
    };
    std::map<std::string, uint32_t> aliases;
    for (const Alias& alias : kAliases)
        aliases.emplace(alias.name, alias.function);

    std::vector<uint32_t> names = Strings(kScriptNameStrings, kScriptNameStringsEnd, kCFunctionCount);
    std::vector<Entry> table(kCFunctionCount, Entry{0, 0});
    size_t fromSource = 0, fromAliases = 0;
    std::vector<const char*> stubbed;
    for (size_t i = 0; i < names.size() && i <= 1252; ++i) {
        const char* name = Text(names[i]);
        uint32_t function;
        if (auto f = functions.find(name); f != functions.end()) {
            function = f->second;
            ++fromSource;
        } else if (auto a = aliases.find(name); a != aliases.end()) {
            function = a->second;
            ++fromAliases;
        } else {
            function = StubFor(name);
            stubbed.push_back(name);
        }
        table[1252 - i] = {names[i], function};
    }
    // The entries whose names aren't in the run (MSVC pooled them with an earlier use of the same string): the
    // source's CFunc_ functions not named yet, their names found anywhere in .rdata, in the free entries.
    std::unordered_set<std::string> named;
    for (uint32_t name : names)
        named.insert(Text(name));
    std::vector<uint32_t> free;
    for (uint32_t i = 0; i < kCFunctionCount; ++i)
        if (!table[i].name)
            free.push_back(i);
    size_t pooled = 0;
    for (const auto& [name, function] : functions) {
        if (named.count(name) || pooled == free.size())
            continue;
        if (uint32_t at = FindInRdata(name)) {
            table[free[pooled++]] = {at, function};
            ++fromSource;
        }
    }
    // (any left repeat a neighbour: registering a name twice changes nothing)
    for (uint32_t i = 0; i < kCFunctionCount; ++i)
        if (!table[i].name)
            table[i] = table[i ? i - 1 : 3];
    if (!table[0].name)
        table[0] = table[3];
    std::memcpy(reinterpret_cast<void*>(uintptr_t{kCFunctions}), table.data(), kCFunctionCount * sizeof(Entry));
    std::fprintf(stderr, "exe data: the %zu script function names: %zu from the source's CFunc_ functions, %zu second "
                         "names, %zu folded stubs\n",
                 names.size(), fromSource, fromAliases, stubbed.size());
    for (const char* name : stubbed)
        std::fprintf(stderr, "  %s: a stub (%s)\n", name, StubFor(name) == kReturnTrue ? "true" : "false");
    if (std::strcmp(Text(table[371].name), "CD") != 0 || std::strcmp(Text(table[1049].name), "IsPS2") != 0)
        std::fprintf(stderr, "exe data: the script function names aren't where the table expects them\n");
}

struct MemberEntry {
    uint32_t name, type, function, thisAdjustment, virtualBases[2];
};
static_assert(sizeof(MemberEntry) == 0x18);
struct MemberTable {
    uint32_t address;
    const MemberEntry* entries;
    uint32_t count;
};
struct MemberCode {
    uint32_t table, name, function;
};
#include "member_tables.inc"

void RebuildMemberTables()
{
    uint32_t functions = 0;
    for (const MemberTable& table : kMemberTables) {
        std::memcpy(reinterpret_cast<void*>(uintptr_t{table.address}), table.entries, table.count * sizeof(MemberEntry));
        functions += table.count - 1;
    }
    // The source's own tables (GAME_GLOBAL, their start values already written) have names but no functions: put
    // each in, as the original's static initialisers did. (Without one, an entry goes to the component's
    // CallMemberFunction by name, which knows only a few of them.)
    uint32_t filled = 0, unknown = 0;
    for (const MemberCode& code : kMemberCode) {
        auto* entry = reinterpret_cast<MemberEntry*>(uintptr_t{code.table});
        while (entry->name && entry->name != code.name)
            ++entry;
        if (!entry->name) {
            ++unknown;
            continue;
        }
        entry->function = code.function;
        ++filled;
    }
    std::fprintf(stderr, "exe data: %zu member function tables (%u functions) from upstream's headers; %u functions "
                 "put in the source's tables%s\n", std::size(kMemberTables), functions, filled,
                 unknown ? " (some names not found)" : "");
}

void RebuildMemberNames()
{
    std::vector<uint32_t> names = Strings(kMemberNameStrings, kScriptNameStrings, kMemberNameCount);
    auto* table = reinterpret_cast<uint32_t*>(uintptr_t{kMemberNames});
    for (uint32_t i = 0; i < kMemberNameCount; ++i)
        table[i] = names[std::min<size_t>(i, names.size() - 1)];
    std::fprintf(stderr, "exe data: %zu member function names (\"%s\" ... \"%s\")\n", names.size(),
                 Text(names.front()), Text(names.back()));
}

// The tables the source reads by address.
struct Rebuilt {
    uint32_t address;
    uint32_t size;
};
std::vector<Rebuilt>& RebuiltTables()
{
    static std::vector<Rebuilt> tables;
    return tables;
}
template <typename T, size_t N>
void Put(uint32_t address, const T (&values)[N])
{
    std::memcpy(reinterpret_cast<void*>(uintptr_t{address}), values, sizeof(values));
    RebuiltTables().push_back({address, static_cast<uint32_t>(sizeof(values))});
}
template <typename T>
void Put(uint32_t address, T value)
{
    std::memcpy(reinterpret_cast<void*>(uintptr_t{address}), &value, sizeof(value));
    RebuiltTables().push_back({address, static_cast<uint32_t>(sizeof(value))});
}

void RebuildTables()
{
    // The asset manager's file name extensions and their types (assman.cpp: {name, Ass::AssetType} up to a null
    // name; the extensions and types are asset.h's, compared ignoring case).
    struct ExtensionType {
        const char* extension;
        int type;
    };
    static const ExtensionType kExtensions[] = {
        {"bin", 1}, {"scn", 2}, {"geom", 2}, {"tex", 3}, {"col", 4}, {"ska", 5}, {"fam", 5},
        {"ske", 6}, {"mdl", 7}, {"skin", 7}, {"cut", 9}, {"qb", 10}, {nullptr, 0},
    };
    static_assert(sizeof(ExtensionType) == 8);
    Put(0x0067C650, kExtensions);

    // The collision's shared face list: 0..0x7FF (coll_tri.cpp's SetUpAllFaces writes it when its first entry isn't
    // 0 - the original's start value can't be 0 - so it's written here as SetUpAllFaces would).
    static uint16_t faces[0x800];
    for (uint32_t i = 0; i < 0x800; ++i)
        faces[i] = static_cast<uint16_t>(i);
    Put(0x0067C6B8, faces);

    // A sprite quad's two triangles (sprite.cpp: 0 1 2, 1 3 2).
    static const uint16_t kQuad[] = {0, 1, 2, 1, 3, 2};
    Put(0x0067EFA8, kQuad);

    // The script expressions' operator precedences, by operator (eval.cpp; eval.h's operators) - inferred: C's order
    // (or, and, equals, comparisons, add / minus, multiply / divide, dot, array element). The other tokens: 0.
    static int precedence[74] = {};
    precedence[50] = 1;                    // or
    precedence[51] = 2;                    // and
    precedence[7] = 3;                     // equals
    precedence[18] = precedence[20] = 4;   // less than, greater than
    precedence[11] = precedence[10] = 5;   // add, minus
    precedence[13] = precedence[12] = 6;   // multiply, divide
    precedence[8] = 7;                     // dot
    precedence[5] = 8;                     // array element
    Put(0x0067D908, precedence);

    // The configured frame rate (cfuncs_scene.cpp's SetConfig: 60 when not given) - inferred: 60.
    Put(0x00684FB4, 60);

    // The input's button names (inpman.cpp's ButtonChecksum / ButtonIndex: 22 checksums, 1-21 the buttons). Each
    // index's button is the one the trick component gives it (trick_queue.cpp's UpdateButtons: 1-8 the d-pad's
    // directions in DPadDirection's order, then its index -> event bit pairs, whose events are input_component.cpp's
    // kEventButtons); the names are skate_cfuncs.cpp's ControllerPressed's, and l3 / r3. Without them every button
    // is index 0 - no trick sees its buttons.
    static const uint32_t kButtonNames[22] = {
        0,
        0xBC6B118F, 0xE3006FC4, 0x85981897, 0x4B358AEB, // up, down, left, right
        0xB7231A95, 0xA50950C5, 0xD8847EFA, 0x786B8B68, // upleft, upright, downleft, downright
        0x2B489A86, 0x321C9756, 0x7323E97C, 0x20689278, // circle, square, x, triangle
        0x26B0C991, 0xBFB9982B, 0xC8BEA8BD,             // l1, l2, l3
        0xF2F1F64E, 0x6BF8A7F4, 0x1CFF9762,             // r1, r2, r3
        0x767A45D7, 0xBD30325B, 0x9D2D8850,             // black, white, z
    };
    Put(0x0067D6D8, kButtonNames);

    // The digital-to-analogue table (inpman.cpp's ConvertDigitalToAnalog: {button mask, event} up to a mask that
    // isn't positive): each button's pressure 0xFF while it's down, else 0, every frame. The masks are the PS2 pad's
    // (inpman.h: bit 12 up ...; the device's extra byte - black, white - above bit 16), the events its pressures
    // (Data::kPadRight ...: the control data's order from byte 8 - sdl_input.cpp) and input_component.cpp's
    // kEventButtons (L3, R3, black, white). Without it no button's pressure is ever set - CControlPad reads its
    // buttons from them - and the left stick's d-pad stand-in (OverridePadWithStick) is never cleared: let go of
    // the stick and the skater keeps turning.
    static const int kDigitalToAnalog[][2] = {
        {0x2000, 4}, {0x8000, 5}, {0x1000, 6}, {0x4000, 7},     // right, left, up, down
        {0x0010, 8}, {0x0020, 9}, {0x0040, 10}, {0x0080, 11},   // triangle, circle, x, square
        {0x0004, 12}, {0x0008, 13}, {0x0001, 14}, {0x0002, 15}, // l1, r1, l2, r2
        {0x0200, 16}, {0x0400, 17},                             // l3, r3
        {0x10000, 22}, {0x20000, 23},                           // black, white
        {0, 0},
    };
    Put(0x0067D730, kDigitalToAnalog);

    // HORSE's statuses (horse.cpp's Statuses: checksums up to 0 - GotLetter, TieScore, BeatScore, the game over,
    // Idle, NoScoreSet; selftests_goals_horse.cpp's values). Without them no run's outcome is ever seen.
    static const uint32_t kHorseStatuses[] = {0x0FD80EBA, 0x006BE86F, 0xBAA12466, 0xAE7D29B0, 0x23DB4AEA, 0xA71DD8EB, 0};
    Put(0x0067F340, kHorseStatuses);

    // The world's ambient light (light_manager.cpp: sWorldLights::Construct, the original's static initialiser
    // 0x0048EAC0, sets ambient 0x80484848; its other fields are GAME_GLOBALs). Zero: black until a script sets it.
    Put(0x0067DC98, 0x80484848u);

    // The loading screen's picture name, an MSVC std::string (loading_screen.cpp): empty, its capacity 15 as its
    // static initialiser leaves it (crt.cpp's AssignString copes with 0, but leaks a buffer).
    Put(0x0067E958 + 0x18, 15u);

    // Left zero on purpose:
    // - the loading bar's position, height and border (0x0067DDB4, 0x0067DDC4): only SetLoadingBarPos / Size /
    //   Border write them, and the PC's DrawLoadingScreenFrame never reads them (it centres the bar at 420 / 480);
    // - the high-score level list and the stats files' names (0x0067F260, 0x0067F2A0): GameSpy's online stats only;
    // - the skater's air sound (0x0067F018, a Terrain::SoundInfo): its values aren't known (encrypted, not in the
    //   source) - zero is no sound in the air.
    // The vehicle camera's member functions (0x00684150) are member_tables.inc's.

    std::fprintf(stderr, "exe data: %zu tables rebuilt (the precedences, frame rate and input tables inferred)\n",
                 RebuiltTables().size());
}

// How much of the initialised .data the source's start values (and these tables) cover.
void ReportCoverage()
{
    std::vector<bool> covered(kDataInitialisedEnd - kDataStart, false);
    auto cover = [&](uint32_t address, uint32_t size) {
        for (uint32_t a = std::max(address, kDataStart); a < std::min(address + size, kDataInitialisedEnd); ++a)
            covered[a - kDataStart] = true;
    };
    size_t globals = 0;
    for (const Thug2Web::PlacedGlobal& g : Thug2Web::PlacedGlobals())
        if (g.address + g.size > kDataStart && g.address < kDataInitialisedEnd) {
            cover(g.address, g.size);
            ++globals;
        }
    cover(kCFunctions, kCFunctionCount * sizeof(Entry));
    cover(kMemberNames, kMemberNameCount * 4);
    for (const Rebuilt& r : RebuiltTables())
        cover(r.address, r.size);
    for (const MemberTable& table : kMemberTables)
        cover(table.address, table.count * sizeof(MemberEntry));
    size_t bytes = std::count(covered.begin(), covered.end(), true);
    std::fprintf(stderr, "exe data: the source covers %zu of the %u bytes of initialised .data (%zu globals + the "
                         "tables); the rest starts at zero\n",
                 bytes, kDataInitialisedEnd - kDataStart, globals);
}

} // namespace

void Thug2Web::RebuildEncryptedData()
{
    if (!ExeDataEncrypted())
        return;
    RebuildScriptFunctions();
    RebuildMemberNames();
    RebuildMemberTables();
    RebuildTables();
    ReportCoverage();
}

// The exe's static initialisers (its C runtime runs them before WinMain; they're in its .text, 0x00640000-0x00645000,
// not decompiled - and in the retail build encrypted, like their table in .data): what the source has of them, and
// the objects the source uses but never makes (inferred: made as the source's own constructors make them).
void Thug2Web::RunStaticInitialisers()
{
    Mem::EnsureManager(); // (0x004E1950: the memory manager, before any static object allocates)

    // The engine's two texture dictionaries - the level's / scripts' (0x00700F1C) and the particles' (0x00700F20):
    // read throughout (texture_cfuncs.cpp, sprite_element.cpp, ...), set nowhere in the source. Inferred: PC
    // dictionaries (pc_texture.cpp) with an empty table.
    for (uint32_t address : {0x00700F1Cu, 0x00700F20u}) {
        void*& dictionary = GameVariable<void*>(address);
        if (!dictionary) {
            void* memory = Mem::New(sizeof(Nx::CPcTexDict));
            dictionary = memory ? static_cast<Nx::CPcTexDict*>(memory)->Construct(0) : nullptr;
        }
    }

    // The hash tables kept in place (static objects: their constructor makes the buckets). The sizes are inferred -
    // they change only how long a look-up takes.
    struct InPlaceTable {
        uint32_t address;
        int bits;
    };
    static const InPlaceTable kTables[] = {
        {0x006968D0, 6}, // the terrain types (terrain.cpp)
        {0x006968E8, 6}, // the terrain sound actions
        {0x006FA308, 4}, // the bone volumes, by bone count (bone_volumes.cpp)
        {0x006FA9F0, 4}, // the fonts (engine_font.cpp, font.cpp)
        {0x00700F04, 6}, // the loaded texture dictionaries (texture_dict.cpp)
        {0x00701418, 8}, // the shadows' silhouette edges (engine_shadows.cpp)
        {0x007CA9C0, 8}, // the generated pixel shaders (pixel_shaders.cpp)
        {0x007CA9D8, 6}, // the light glows (glow.cpp)
    };
    for (const InPlaceTable& table : kTables)
        new (reinterpret_cast<void*>(uintptr_t{table.address})) Lst::HashTable(table.bits);

    // The static lists' heads (data -1, linked to themselves, as their constructors make them): the loaded scenes
    // (engine.cpp; CSceneListHead::Construct) and the movable collision objects (coll_world.cpp: Lst::Head<CCollObj>,
    // the vtable engine_scene_sectors.cpp gives its own such heads).
    reinterpret_cast<Nx::CSceneListHead*>(uintptr_t{0x006FA7E0})->Construct();
    auto* movables = reinterpret_cast<Mem::ListNode*>(uintptr_t{0x00691978});
    movables->vtable = reinterpret_cast<void**>(uintptr_t{0x00645D80});
    movables->data = reinterpret_cast<void*>(-1);
    movables->priority = 0;
    movables->next = movables;
    movables->prev = movables;

    // The scene lights (0x006FAF80, 150): the array's static constructor calls 0x0048FF40 on each - an empty table of
    // the sectors it reaches (scene_lights.h).
    auto* lights = reinterpret_cast<Nx::sSceneLight*>(uintptr_t{0x006FAF80});
    for (int i = 0; i < 150; ++i)
        lights[i].Construct();
}

// The code addresses the exe's read-only data points at (vtables, tables of handlers) with no function placed: a call
// through one traps. Logged as a count; the list (with where each is pointed at from) to `path` when given.
void Thug2Web::ReportUnplacedCode(const char* path)
{
    struct Missing {
        uint32_t references = 0;
        uint32_t firstFrom = 0;
    };
    std::map<uint32_t, Missing> missing;
    for (uint32_t at = kRdataStart; at + 4 <= kRdataEnd; at += 4) {
        uint32_t value = *reinterpret_cast<const uint32_t*>(uintptr_t{at});
        bool game = (value >= 0x00401000 && value < 0x005BEAAE) || (value >= 0x005F2E00 && value < 0x005F6640);
        if (!game || FunctionPlaced(value))
            continue;
        Missing& m = missing[value];
        if (!m.references++)
            m.firstFrom = at;
    }
    std::fprintf(stderr, "exe code pointers with no function: %zu addresses (game code pointed at by .rdata); "
                         "%zu REPLACE lines not placed\n",
                 missing.size(), DroppedReplacements().size());
    if (!path)
        return;
    if (FILE* f = std::fopen(path, "w")) {
        for (const auto& [address, m] : missing)
            std::fprintf(f, "0x%08X\t%u refs\tfirst at 0x%08X\n", address, m.references, m.firstFrom);
        std::fprintf(f, "\n# REPLACE lines not placed\n");
        for (const auto& [address, name] : DroppedReplacements())
            std::fprintf(f, "0x%08X\t%s\n", address, name);
        std::fclose(f);
    }
}
