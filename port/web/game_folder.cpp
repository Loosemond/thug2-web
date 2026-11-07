// The game folder in the page: its files mounted at /game, fetched from the server's /game/ as they're read.

#include "thug2web/paths.h"
#include "web/game_files.h"

#include <emscripten/console.h>

void Thug2Web::PrepareGameFolder()
{
    if (!MountGameFiles(GameFolder(), "game"))
        emscripten_err("the game folder could not be mounted");
}
