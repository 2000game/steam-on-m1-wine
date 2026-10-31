/*
 * Stand-in for lib/wine/x86_64-unix/wine. ntdll starts every Windows process
 * through that path; this re-executes the real loader inside
 * "Wine Game.app" next to it, so macOS sees an app bundle with the games
 * category (Game Mode). macOS only finds the bundle when the executable's
 * own path is inside it; a symlink does not work.
 */
#include <libgen.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    char self[PATH_MAX], real[PATH_MAX], target[PATH_MAX];
    uint32_t size = sizeof(self);

    (void)argc;
    if (_NSGetExecutablePath(self, &size) || !realpath(self, real))
    {
        fprintf(stderr, "wine trampoline: cannot resolve own path\n");
        return 1;
    }
    snprintf(target, sizeof(target), "%s/Wine Game.app/Contents/MacOS/wine", dirname(real));
    execv(target, argv);
    perror("wine trampoline: exec Wine Game.app");
    return 1;
}
