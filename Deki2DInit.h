#pragma once

/**
 * @brief Register the animation asset loader.
 *
 * Called from DekiInitPackageSystems() on firmware/static builds and from
 * DekiPluginInit() when the package is loaded as a DLL. Idempotent.
 *
 * GLOBAL SCOPE, deliberately: the editor generates a translation unit that
 * declares these as plain `extern void Deki2DInitSystem();`, and that file
 * cannot know a package's namespace (see deki-tween's TweenInit.h).
 */
void Deki2DInitSystem();
void Deki2DShutdownSystem();
