# Deki 2D

Docs: https://dekiengine.github.io/deki-2d/ (components and properties, generated from the code)

2D rendering components for the Deki Engine: sprites, sprite batches, text, animations, gradients, buttons, scrolling, rollers, clipping, and sorting groups.

Part of [Deki Engine](https://github.com/dekiengine/deki-engine).

## Namespace

Types live in `Deki2D`. Scene files store the qualified name, and so does code:

```cpp
using namespace Deki2D;
obj->AddComponent<SomeComponent>();
```

## Install

Package Manager in the Deki Editor, or `DekiEditor --packages-add deki-2d <project>`.

## Dependencies

| Dependency | Type |
|---|---|
| `deki-rendering` | Deki package |
| `deki-input` | Deki package |
| `deki-editor` | Deki editor DLL |
| `freetype` | External (FreeType License) |

## Sprite Batch

`SpriteBatchComponent` draws many pieces of one sprite from one component, each
with its own place, frame, flips, tint and sorting order. The pixels are those
of as many sprite objects, with far less to go through each frame. Each piece
sorts on its own, so a character can pass in front of one and behind another.
Pieces are added from code (`Add`, `SetPlace`, `SetFrameRect`, `SetVisible`,
`SetFlip`, `SetTint`) and are not saved with the scene. The first-game guide's
soil and turnips are two batches.

## Raised procedural sprites

A procedural sprite can have an outline all round and a band along its bottom
under a shorter face: the raised look of a key or a HUD panel, in one sprite
instead of three stacked shapes, and stretched in NineSlice mode. The
Inspector's **Raised** section sets them.

## Pictures from a PNG

For a picture the app gets while it runs, such as a download:

```cpp
#include "PngSprite.h"

Deki2D::Png::Result why;
Deki2D::Sprite* picture = Deki2D::CreateSpriteFromPng(bytes, size, 320, 240, &why);
if (!picture)
{
    DEKI_LOG_WARNING("Picture not shown: %s", Deki2D::Png::ResultName(why));
}
```

It is shrunk to fit 320 x 240 keeping its shape, and never enlarged. The
reader keeps two rows of the picture and 32 KiB besides the sprite it makes,
so it suits small boards too. Interlaced PNGs are not read.

## Community

Questions, ideas and what you made with Deki: [the Deki Discord](https://dekiengine.com/discord/).

## License

Apache 2.0. See [LICENSE](LICENSE).

Third-party licenses are listed in [NOTICE](NOTICE).
