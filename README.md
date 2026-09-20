# Deki 2D

Docs: https://dekiengine.github.io/deki-2d/ (components and properties, generated from the code)

2D rendering components for the Deki Engine: sprites, text, animations, gradients, buttons, scrolling, rollers, clipping, and sorting groups.

Part of [Deki Engine](https://github.com/dekiengine/deki-engine).

## Namespace

Types live in `Deki2D`. Scene files store the qualified name, and so does code:

```cpp
using namespace Deki2D;
obj->AddComponent<SomeComponent>();
```

Scenes saved before 0.16.0 used bare names and still load; saving writes the current one.

## Install

Package Manager in the Deki Editor, or `DekiEditor --packages-add deki-2d <project>`.

## Dependencies

| Dependency | Type |
|---|---|
| `deki-rendering` | Deki package |
| `deki-input` | Deki package |
| `deki-editor` | Deki editor DLL |
| `freetype` | External (FreeType License) |

## License

Apache 2.0. See [LICENSE](LICENSE).

Third-party licenses are listed in [NOTICE](NOTICE).
