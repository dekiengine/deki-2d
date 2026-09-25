# Changelog

Notable changes to `deki-2d`. Engine and editor changes are in the
[engine changelog](https://github.com/dekiengine/deki-engine/blob/master/CHANGELOG.md).

A package's `minEngine` names the engine version it needs. Before 1.0 a
breaking change bumps the minor across the editor, the engine and every
package together, so a package with no changes of its own is still released
alongside one that has them.

## Unreleased

### Changed
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
