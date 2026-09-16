# Deki 2D

Documentation: https://dekiengine.github.io/deki-2d/ (components and properties, generated from the code)

2D rendering components for the Deki Engine: sprites, text, animations, gradients, buttons, scrolling, rollers, clipping, and sorting groups.

Part of the [Deki Engine](https://github.com/dekiengine/deki-engine) package ecosystem.

## Namespace

This package's types live in `Deki2D`. Scene files store the qualified
name, so a component is `Deki2D::SomeComponent` there, and code naming one
needs the namespace:

```cpp
using namespace Deki2D;
obj->AddComponent<SomeComponent>();
```

Scenes saved before 0.16.0 used bare names and still load: every component
records what it used to be called, and a save writes the current name.

## Installation

Install via the Package Manager inside the Deki Editor.

## Dependencies

| Dependency | Type |
|---|---|
| `deki-rendering` | Deki package |
| `deki-input` | Deki package |
| `deki-editor` | Deki editor DLL |
| `freetype` | External (FreeType License) |

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for details.

Third-party licenses are listed in [NOTICE](NOTICE).
