#pragma once

/// Registers the animation asset loader. Safe to call more than once.
///
/// Called from DekiInitPackageSystems() on firmware and static builds, and
/// from DekiPluginInit() when the package is loaded as a DLL.
///
/// Must stay in the global namespace: the editor generates a file that
/// declares these as plain `extern void Deki2DInitSystem();`, and that file
/// cannot know a package's namespace (see deki-tween's TweenInit.h).
void Deki2DInitSystem();
void Deki2DShutdownSystem();
