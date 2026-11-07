// The web build's entry point: WinMain in the game folder, with the command line the page (or Node) passes.

#include <string>

#include <unistd.h>
#include <windows.h>

#include "game/sys/winmain.h"
#include "thug2web/exe_data.h"
#include "thug2web/paths.h"

int main(int argc, char** argv)
{
    Thug2Web::RebuildEncryptedData();
    Thug2Web::RunStaticInitialisers();
    Thug2Web::ReportUnplacedCode(Thug2Web::ReportPath());
    Thug2Web::PrepareGameFolder();
    chdir(Thug2Web::GameFolder());
    std::string commandLine;
    for (int i = 1; i < argc; ++i) {
        if (i > 1)
            commandLine += ' ';
        commandLine += argv[i];
    }
    return Sys::WinMain(nullptr, nullptr, commandLine.data(), SW_SHOWNORMAL);
}
