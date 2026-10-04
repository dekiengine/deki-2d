# Changelog

Notable changes to `deki-2d`. Engine and editor changes are in the
[engine changelog](https://github.com/dekiengine/deki-engine/blob/master/CHANGELOG.md).

A package's `minEngine` names the engine version it needs. Before 1.0 a
breaking change bumps the minor across the editor, the engine and every
package together, so a package with no changes of its own is still released
alongside one that has them.

## Unreleased

### Changed
- **Names follow the code style** (deki-engine/docs/codestyle): types, functions and enum values are PascalCase, constants kPascalCase, members m_PascalCase, locals and parameters camelCase. The code is formatted with clang-format 22.
- The functions the editor finds by name are PascalCase: Deki2DRegisterComponents, Deki2DGetAutoComponentCount, Deki2DEnsureRegistered and the rest. Built against engine ABI 21; a build of this package from before does not load and is rebuilt.
- Renamed: `AssetTypeName` is `kAssetTypeName` (Sprite, BitmapFont, FrameAnimationData), `GradientComponent::MAX_STOPS` is `kMaxStops`, BitmapFont's public fields lost their `m_` (`firstChar`, `lastChar`, `glyphCount`, `lineHeight`, `capHeight`, `xHeight`, `decorationMode`), `ButtonComponent::on_state_changed` is `onStateChanged`, `AnimationComponent::completion_callback` is `completionCallback`.

## 0.17.0

### Fixed
- **Animations load on a device.** The animation loader registered itself
  from a static object in a file nothing else referenced, and a firmware links
  the game from an archive, so the linker dropped that file and every `.anim`
  failed with "No loader registered for asset type: Animation". It is now
  registered by `Deki2D_InitSystem()` (`Deki2DInit.h`), which the generated
  firmware and simulator code call, and by `DekiPlugin_Init` in the editor.
- A dithered gradient drawn at a fractional scale (a camera showing 1280x720
  of art on a 480x320 screen, which is not a whole multiple of it) showed
  seams: it was baked on the art grid and then scaled, which
  repeats or drops pixels unevenly. It is now baked at the density the view
  draws it at, 1:1 on the screen, with the dither laid out in whole screen
  pixels. At a whole-number scale the pixels are the same as before.
- A sprite showing a frame kept the frame's old rect after its image was
  reimported (new frames, another Max Size), drawing the wrong part of the new
  texture until the scene reloaded. `SpriteComponent::SetFrame` remembers the
  frame by GUID and looks it up again when assets reload; the animation's
  resolved frames are keyed on the asset epoch as well as the sprite.

### Added
- **Max Size.** Images are stored at their target's Max Size (the texture
  inspector's Targets table): scaled so the larger side is exactly Max Size,
  each sprite frame and nine-slice part resampled on its own so none bleeds
  into another, chroma-keyed images by picking pixels. The file records the
  image's size (SourceSize chunk); `Sprite::ApplySourceSize` brings frames,
  nine-slice borders and `pixelsPerMeter` to the stored pixels at load, so a
  sprite keeps its size in the world. `sourceWidth/Height`, `sourceScale` and
  `SourceToStoredX/Y` are on `Sprite`. Tiled and nine-slice sprites bake in
  the stored pixels.
- The Spritesheet editor works on the original image, not the editor cache,
  so frames are authored in the image's own pixels whatever the Max Size.

### Changed
- `minEngine` 0.17.0. Reflection ABI 20: the package must be rebuilt.
- An image's Format may be Automatic (the default) and set per target
  (`settings.texture.targets`); its cache follows the editor's active
  platform, and an export for another target re-encodes it for that one.
- The button's editor reads the input collider's padding fields directly;
  `InputCollider::GetBounds()` is gone from deki-input.

## 0.16.0

### Changed
- **Moved into the `Deki2D` namespace.** Every component was declared at global
  scope, which made its identity a bare class name — the name a scene file
  stores and the name the registry keys on — so two packages defining one name
  collided there with nothing to tell them apart. Each component carries
  `DEKI_FORMER_NAME` with the name it was saved under before, so existing
  scenes load unchanged and are written back qualified on the next save.
  Code naming these types needs the namespace: `using namespace Deki2D;` or a
  qualified name.
- Enum properties are stored by name rather than by number, so appending to an
  enum or reordering one no longer changes what a saved scene means. Files
  written before this still read.
- `minEngine` 0.16.0. Reflection ABI 17: the package must be rebuilt.

## 0.15.0

### Changed
- Allocations name their region explicitly. Every `Deki::Buffer` and
  `Memory::Allocate` here passes `Deki::Memory::Internal` or
  `Deki::Memory::External`; the old `MemoryUse` enum, its `Auto` placement and
  the `Hot`/`Buffer` labels are gone.
- Every allocation goes through the engine's memory system. `new[]`, `malloc`
  and the hand-unwound cleanup paths are replaced by owning buffers, so a
  sprite, a bitmap font or a baked gradient releases itself rather than
  depending on an error branch remembering to.
- Blit sources are built from a named `PixelLayout` instead of positional
  booleans, and `Texture2D` comes from the engine core rather than this
  package.

### Fixed
- A gradient that will not fit no longer reboots the board. The allocation
  fails and is reported; it used to reach `new[]`, which aborts on ESP-IDF
  because exceptions are off.
- `BuildOpaqueRowSpans` moved to `SpriteRowSpans.h` so the opaque-run rule can
  be tested directly, with eleven tests covering it — longest run rather than
  first-to-last, soft pixels breaking a run, ties, the empty-row span, and
  per-row independence. The renderer's golden blit hashes are unchanged.
