#include "thug2web/paths.h"

#include <emscripten/em_js.h>

namespace {

EM_JS_DEPS(game_folder, "$stringToNewUTF8");

EM_JS(char*, NodeGameFolder, (), {
    if (typeof process == "undefined" || !process.env || !process.env.THUG2_GAME)
        return 0;
    return stringToNewUTF8(process.env.THUG2_GAME);
});

EM_JS(int, NodeScriptTrace, (), {
    if (typeof process == "undefined" || !process.env || !process.env.THUG2_SCRIPT_TRACE)
        return 0;
    return parseInt(process.env.THUG2_SCRIPT_TRACE, 10) | 0;
});

EM_JS(char*, NodeReportPath, (), {
    if (typeof process == "undefined" || !process.env || !process.env.THUG2_REPORT)
        return 0;
    return stringToNewUTF8(process.env.THUG2_REPORT);
});

} // namespace

const char* Thug2Web::ReportPath() { return NodeReportPath(); }

const char* Thug2Web::GameFolder()
{
    static const char* folder = [] {
        const char* node = NodeGameFolder();
        return node ? node : "/game";
    }();
    return folder;
}

// THUG2_SCRIPT_TRACE=<n> (Node): the first n script commands run, logged (the script calls, script functions and
// member functions, by checksum) - for following the start-up scripts.
extern "C" bool thug2_trace_script_command()
{
    static int left = NodeScriptTrace();
    return left > 0 && left-- > 0;
}
