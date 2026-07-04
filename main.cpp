#include <cstdio>
#include <cstdlib>
#include <ctime>

// __attribute__((constructor)) makes this function run automatically the
// instant the shared library is loaded into the game's process - no need
// to know any mcpelauncher-specific hook API for this test. If this mod
// is loading correctly, this file will appear/update every time you
// launch Minecraft.
__attribute__((constructor))
static void kairo_hello_on_load() {
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";

    char path[512];
    snprintf(path, sizeof(path), "%s/kairo_test_loaded.txt", home);

    FILE* f = fopen(path, "a");
    if (f) {
        time_t now = time(nullptr);
        fprintf(f, "Kairo test mod loaded at %s", ctime(&now));
        fclose(f);
    }
}
