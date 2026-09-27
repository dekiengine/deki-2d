#pragma once

/**
 * @brief Register the animation asset loader.
 *
 * Called from deki_init_package_systems() on firmware/static builds and from
 * DekiPlugin_Init() when the package is loaded as a DLL. Idempotent.
 *
 * GLOBAL SCOPE, deliberately: the editor generates a translation unit that
 * declares these as plain `extern void Deki2D_InitSystem();`, and that file
 * cannot know a package's namespace (see deki-tween's TweenInit.h).
 */
void Deki2D_InitSystem();
void Deki2D_ShutdownSystem();
