# GPU Landmark Label Rendering Plan

## Current State

Runtime landmark labels are drawn by `ULandmarkSubsystem::DrawLandmarks` through `UCanvas`.
`UpdateCameraState` already caches visibility, screen position, scale, and alpha when the camera moves, but the draw path still does these operations for every visible label every HUD frame:

- Resolve fonts.
- Build `FText` and `FCanvasTextItem`.
- Measure text with `Canvas->StrLen`.
- Submit one or two Canvas text items per landmark.

This is acceptable for a small number of labels. It becomes expensive when the map shows many cities or when city names and victory point strings are visible at the same time.

The editor `UTextRenderComponent` helpers are not the runtime bottleneck. They are used for authoring/proxy visualization and are hidden in game.

## Goal

Keep LandmarkSystem as the source of landmark identity, type, visibility, and Mass Battle spawning, but move repeated label rendering work out of the per-frame Canvas path.

The target model is:

1. CPU performs broad visibility, priority, and invalidation.
2. Text rasterization is cached by label content and style.
3. GPU draws cached label quads in one or a few batches.

## Phase 1: CPU Cache Before GPU Rewrite

This phase keeps Canvas as the renderer and reduces unnecessary work.

- Add a cached draw item per visible landmark containing ID, name string, victory point string, font pointers, measured text sizes, scale, alpha, and final screen positions.
- Recompute text measurement only when text, font, victory point value, language, or scale bucket changes.
- Keep `UpdateCameraState` responsible for visibility and projection.
- Add priority sorting and an optional visible label cap so low-priority labels do not consume draw time when the screen is crowded.
- Keep `DrawLandmarks` as a fallback path for platforms or debugging.

This phase is low risk and should be the first code change because it preserves existing visual output.

## Phase 2: Cached Label Atlas

The first GPU path should cache full label images, not individual glyphs.

For Chinese map labels, full-label atlas entries are simpler and more reliable than a custom glyph renderer because shaping, fallback fonts, and punctuation are handled once by Unreal's text stack.

Suggested cache key:

```text
LabelText + VictoryPointText + Font + FontSizeBucket + Style + Locale
```

Each cache entry stores:

- Atlas texture or render target reference.
- UV rectangle.
- Pixel size.
- Baseline/layout offsets for name and victory point rows.
- Last-used frame for eviction.

The runtime renderer then sends per-label instance data:

- Screen position or world anchor.
- UV rectangle.
- Pixel size.
- Scale.
- Alpha.
- Color/team color.
- Priority.

The draw path becomes one or a few batched quad draws using a material that samples the atlas.

## Phase 3: Mass/FogOfWar Style GPU Data Path

After the atlas path works, LandmarkSystem can share the same data-flow style used by FogOfWar:

- CPU keeps the landmark spatial grid or switches to a shared Mass Battle hash grid for broad phase.
- Visible landmark instance data is uploaded to a compact GPU buffer or texture.
- A material/Niagara/custom renderer draws label quads from the buffer.
- CPU only updates the buffer when camera visibility, label state, or team colors change.

This avoids per-label Canvas submission and makes rendering cost scale closer to visible batch count than visible text count.

## Team Color Integration

Landmark labels should not own the global team palette. A later integration pass can read team colors from the same per-map INI channel used by FogOfWar and minimap bounds.

Recommended rule:

- Landmark JSON stores landmark identity, type, position, victory value, and optional team ID.
- Per-map INI stores map bounds and shared team color metadata.
- LandmarkSystem resolves team ID to color through the shared map config provider.
- If no team color is found, fallback to the label's local style color.

This keeps map data portable and avoids hard coupling LandmarkSystem to Minimap rendering internals.

## Risks

- Atlas invalidation must handle language, font, style, DPI, and settings changes.
- Full-label atlas entries are memory heavier than glyph atlases, so eviction is required for large maps or multiple languages.
- Overlap avoidance is still a CPU problem. The GPU can draw quads fast, but priority and hiding rules should stay deterministic on CPU.
- A pure material-based text renderer is not recommended as the first step because arbitrary CJK text rendering in a shader is a large separate problem.

## Recommended Next PR

Implement Phase 1 first:

1. Add a `FLandmarkLabelDrawCache` structure.
2. Move text string construction and `StrLen` measurement behind cache invalidation.
3. Add priority sorting and a configurable visible label cap.
4. Preserve current `DrawLandmarks` visual output.

After that, add the atlas renderer behind a runtime setting and keep Canvas as a fallback until the GPU path is verified on dense maps.
