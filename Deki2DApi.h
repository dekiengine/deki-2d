#pragma once

// DEKI_2D_API, what deki-2d exports from its DLL. On its own so a header that
// needs only the macro (FontCompiler.h) does not pull in the whole package and
// the reflection code it is generated with.

#ifdef _WIN32
#if defined(DEKI_2D_EXPORTS) || defined(DEKI_PLUGIN_EXPORTS)
#define DEKI_2D_API __declspec(dllexport)
#else
#define DEKI_2D_API __declspec(dllimport)
#endif
#else
#define DEKI_2D_API __attribute__((visibility("default")))
#endif
