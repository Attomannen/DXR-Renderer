# Editor: Roadmap to an Unreal-like Experience

What the GameEditor is today, everything it lacks against the Unreal Editor
baseline, and the order to build it in. Written 2026-09-18 from a full read of
`Source/Editor`, `Source/EditorDefaultGraphics`, `Source/SceneScriptCore` and
`Source/GameEditor` (about 13k lines). Every claim carries a file reference.

Companion documents:
- `EDITOR_USABILITY_PROGRESS.md` - the earlier Unity-inspired usability pass.
- `PORTFOLIO_SHOWCASE_ROADMAP.md` - renderer features; this doc only covers
  how they are *authored*.

Paths below are relative to `Source/`.

---

## 0. What "Unreal-like" means here

Unreal's editor experience rests on a small set of pillars. This roadmap is
organised around them, because most gaps trace back to one missing pillar
rather than one missing button:

| Pillar | Unreal | This editor today |
|---|---|---|
| Asset identity | GUID-referenced asset registry, safe rename/move, dependency graph | Path strings, `fs::rename` breaks references silently |
| Object model | Actors with components, transform hierarchy, blueprint classes with inheritance | Flat objects with a prefab name and a list of overrides, no parenting |
| Details panel | Reflection-driven, categories, multi-edit, override indicators, reset arrows | Per-type ImGui editors, one table per object, no override UI |
| Viewport | Multiple view types, view modes, show flags, stats, bookmarks, pilot | One perspective viewport, no view modes in UI |
| Level authoring | Lights, fog, sky, post volumes, probes all placed and edited in-level and saved | Only sun, ambient and env map are saved; everything else is a runtime tuning panel |
| Content pipeline | Async import with progress, thumbnails, reimport, source control | Blocking cooker, DDS-only thumbnails |
| Play | PIE with state restore, simulate, eject, output log | Separate process launch |
| Undo | Everything undoable, transaction names, history panel | Global stack, many editors bypass it |
| Layout | Named layouts, tabs, workspace save | One hardcoded DockBuilder layout |

---

## 1. Current state, verified

### 1.1 Shell
- ImGui docking and multi-viewport on (`Editor/age/editor/Editor.cpp:160`).
  One hardcoded DockBuilder layout (`:447-465`), persisted only through
  `imgui.ini`. No layout presets, reset, or workspaces.
- Menus: File (Save only), Edit (Undo/Redo), View (grid toggle), Create, Assets
  (Texture Importer), ImGui, Perforce toggle (`Editor.cpp:470-586`). No
  recent files, no Open, no Save As, no project menu, no Window menu.
- Document base (`Document/Document.h:33-65`) with dirty tracking by undo-stack
  size. No virtual destructor (`Material/MaterialDocument.h:34`).
- `Editor::Save()` saves all open **and closed** documents (`Editor.cpp:695`).
- Global single undo stack, no coalescing, no names, no limit
  (`Core/age/editor/CommandManager/CommandManager.cpp:9`). Undo refocuses the
  owning document (`Editor.cpp:714-783`).
- Shortcuts hardcoded: Ctrl+Z/Shift+Z/S, F5, F, Ctrl+D, Delete, W/E/R, F2.
- No editor theme; Lucide icon font in use. No settings persistence for
  editor state (grid, snap, camera speed, P4 toggle are process statics).

### 1.2 Viewport (`Tools/Viewport/Viewport.cpp`)
- One perspective viewport per document, FOV 60, near 0.1, far 50000.
- Camera: Maya Alt-orbit, Blender MMB, RMB fly with wheel speed. F focuses the
  selection centroid by position only (`Scene/SceneDocument.cpp:399`).
- Picking via an ID render target with a per-mouse-move CPU readback
  (`Viewport.cpp:371-392`). Marquee select uses unrotated bounds (`SceneDocument.cpp:809`).
- ImGuizmo translate/rotate/scale, local/world, snapping with Ctrl invert
  (`Gizmos/Gizmos.cpp`). Pivot is the last selected object.
- Static CPU grid (`Tools/ViewportGrid`). ID-buffer selection outline.
- Rendering: the real deferred path with CSM, SSAO, SSR, clustered lights,
  post and tonemap (`EditorDefaultGraphics/DefaultEditorGraphics.cpp:564-739`).
  DXR on when the device supports it (`:620`), TLAS rebuilt on a geometry hash.
  **Missing versus the game:** GI volume and reflection probes disabled
  (`:613`), local shadows off (`:626`), env cubemap hardcoded to
  `horizonCubeMap.dds` so the scene's own HDRI is ignored (`:598`), no DLSS,
  no NRD, no fog or sky controls. View modes only via `AGE_EDITOR_GBUF` env var
  (`:723`). No stats overlay.
- Play: launches `GameMain_*.exe` as a separate process
  (`Tools/ProjectRunControls/ProjectRunControls.cpp:20-66`).

### 1.3 Scene and hierarchy
- `SceneObject` (`SceneScriptCore/age/scene/SceneObject.h`): name, folder
  string, TRS, prefab name, type (GameObject / PointLight / SpotLight) with
  inline light fields, property overrides. No components, no parenting, no
  visibility, lock, layers, tags, or mobility.
- Prefabs are `.tgo` definitions (`SceneObjectDefinition.h:44-74`); instances
  store overrides. `myParent` exists but has no UI, so no inheritance.
- Hierarchy (`Tools/SceneObjectList/SceneObjectList.cpp`): search, type filter
  (recomputes every object's property set every frame, `:399-419`), folder
  tree, drag to folder, multi-select, rename, duplicate, delete. Light creation
  buttons are not undoable. No reorder, no visibility or lock columns.
- Drop into viewport: `.tgo` places, `.fbx` auto-creates `.tgm` + `.tgo`,
  `.tgmat` on a row assigns to all slots (`SceneDocument.cpp:626-761`).

### 1.4 Inspector (`Tools/SceneObjectProperties/SceneObjectProperties.cpp`)
- Name, folder, transform (three DragFloat3, no reset), then definition
  properties via `Property::ShowImGuiEditor`. Types: bool, int, float,
  Vector2/3/4, Color, string, SceneModel, SceneSprite, SceneReference,
  AnimationClipReference, PoseAndMotion.
- No override indicator or revert (`:392`), no groups (`:385`), no multi-edit.
- **Lights have no property editing at all**; only creation defaults.
- Sun, ambient and environment picker exist (`:114-167`) but bypass undo.
- P4 status block queries the folder id instead of the file path (`:407-451`), likely a bug.

### 1.5 Assets
- Browser (`Tools/AssetBrowser/AssetBrowser.cpp`): background rescan every 1 s
  holding a mutex through the whole draw (`:54-135`, `:225`); list/grid,
  search, type filter, DDS thumbnails only, Copy Path, Convert to TGO, Delete,
  Create Folder. Drag-move is a raw rename with no reference fixup (`:196`).
- Import (`Import/ImportSettingsDocument.cpp`): `.tgm` JSON; "Generate" shells
  out to `TextureCooker_Debug.exe` and blocks with `WaitForSingleObject(INFINITE)`
  (`:159`). Hardcoded `_Debug` name here and in `Editor.cpp:366`.
- Formats: `.tgs` + `.leveldata/` per-object JSON, `.tgo`, `.tgac`, `.tgmat`,
  `.tgmatgraph`, `.tgm`, `.dds`, `.fbx`. All path-referenced, no GUIDs.
- Two independent scene readers: editor (`SceneSerialize.cpp`) and game
  (`Game/source/SceneFiles.cpp:132`).
- P4 (`p4/p4.cpp`): polling thread, auto-checkout, add/delete on save, status
  icons. No submit, revert, diff, changelist UI; login modal `#ifdef`'d out.

### 1.6 Material editor (`Material/MaterialDocument.cpp`)
- Sections: Material, Surface, Emission, Texture Maps (4 slots), glass.
  Preview mesh, key light, ambient, cubemap, Lit/Unlit/Wireframe/Normals.
  Preview renders forward with a stock shader, not the deferred/DXR path.
- Graph (`Material/Graph/`): 10 node types plus outputs, CPU per-texel bake
  to DDS. No live preview, no undo, no copy/paste, no parameters or instances.

### 1.7 Other documents
- ObjectDefinition: property add/remove/reorder, scripts, live preview. No parent picker (`:308`).
- ScriptEditor (imnodes): node categories, links, literal pins, live run.
  No breakpoints, execution trace, copy/paste, comments, search.
- AnimationClip: skeleton tree, play/scrub. No timeline, notifies, curves, blendspaces.
- Navmesh tool: complete Recast UI, entirely disconnected (`SceneDocument.cpp:77, 98, 270, 378`).
- FileDialog: native Win32 wrapper.
- ImGui addons present: docking imgui, ImGuizmo, imnodes, icon fonts. No ImPlot, no text editor widget.

### 1.8 Lighting and environment authoring
- Saved in `.tgs`: sun yaw/pitch/color/intensity, ambient color, environment
  texture (`SceneSerialize.cpp:153-164`). Lights in per-object `.leveldata`.
- Not authorable in the editor: fog, atmosphere, post FX, exposure, tonemap,
  GI volume, reflection probes, shadow settings, SSR/SSAO, DXR budgets, DLSS,
  NRD. All live in `Game/source/GameWorldDebugUI.cpp` and are never saved.

---

## 2. Bugs and hazards to fix first

1. **Blocking cooker.** `WaitForSingleObject(INFINITE)` freezes the editor for
   the whole FBX import (`ImportSettingsDocument.cpp:159`).
2. **Hardcoded `TextureCooker_Debug.exe`** (`ImportSettingsDocument.cpp`,
   `Editor.cpp:366`). Release editor cannot import.
3. **Asset move breaks references.** Drag-move is `fs::rename` with no fixup
   (`AssetBrowser.cpp:196-212`).
4. **Editor ignores the scene HDRI**, hardcoded `horizonCubeMap.dds`
   (`DefaultEditorGraphics.cpp:598`). What you author is not what you see.
5. **No GI or reflection probes in the viewport** (`:613`). Indirect light differs from the game.
6. **Null derefs**: missing `.tgs` (`SceneDocument.cpp:81-88`), empty
   selection in `CalculateSelectionPosition` (`:951-963`),
   `SceneObjectProperties.cpp:207`.
7. **Reentrancy asserts** on active scene and selection singletons
   (`SceneDocument.cpp:130-133, 517-520, 606`).
8. **P4 status uses the folder id** instead of the file path (`SceneObjectProperties.cpp:407`).
9. **P4 calls block the UI** (`p4.cpp:324`) and the cache never evicts (`:581`).
10. **Per-frame waste**: property-set rebuild in the filter bar
    (`SceneObjectList.cpp:399`), `P4::GetFileInfo` per object in the id pass
    (`DefaultEditorGraphics.cpp:465`), every open document renders its
    viewport every frame, `SceneCache` reloads all assets twice a second
    (`SceneUtil.h:52`), asset browser mutex held for the whole draw.
11. **UB**: `sprintf_s` with aliased source and destination (`SceneObjectList.cpp:191`).
12. **Document has no virtual destructor** (`MaterialDocument.h:34`).
13. **Marquee select ignores rotation** (`SceneDocument.cpp:809`).
14. **`Editor::Save` saves closed documents** (`Editor.cpp:695`), surprising and slow.
15. **DX12 SRV hazard**: raw `GetShaderResourceView()` is null on DX12; any new
    image or readback code must go through the RHI handles (`Viewport.cpp:193`).

---

## 3. Foundations (the pillars everything else stands on)

### 3.1 Asset registry and GUIDs
- Assign a GUID to every asset (stored in the JSON, or a sidecar for `.dds`
  and `.fbx`). Build an in-memory registry at startup and keep it current from
  a filesystem watcher (`ReadDirectoryChangesW`) instead of the 1 s rescan.
- Reference assets by GUID everywhere; keep a path-to-GUID map for migration.
- Dependency graph: who references what, so rename / move / delete can fix
  up or warn, and "Find references" and "Reference viewer" become possible.
- Cache the registry to disk for fast startup; the two startup tree scans
  (`Editor.cpp:152-153`) go away.
- **Unify the scene readers**: one serializer shared by editor and game.
- Effort: L. This is the one item every content feature in section 5 depends on.

### 3.2 Reflection-driven properties
- Replace the per-type `ShowImGuiEditor` switch with property metadata:
  display name, category, tooltip, range, step, units, default, advanced
  flag, condition. Already listed as the top remaining item in
  `EDITOR_USABILITY_PROGRESS.md`.
- Drive the inspector, the override system, multi-edit, serialization,
  copy/paste and the search box from the same metadata.
- Extend to renderer settings (`Tunables`) and light parameters so the same
  panel edits them.
- Effort: M-L.

### 3.3 Actor and component model
- Introduce a transform hierarchy: parent id on `SceneObject`, world / local
  transforms, attach and detach preserving world transform, hierarchy
  drag-reparent, gizmo on the root moves children.
- Components: Static Mesh, Light (point / spot / rect / directional),
  Reflection Probe, GI Volume, Post Process Volume, Fog Volume, Camera,
  Audio, Script. A component is a typed property block on the object, which
  keeps the existing override machinery.
- Prefab inheritance: wire the existing `myParent` on definitions, with
  override propagation and "revert to parent" per property.
- Per-object flags: visible, locked, editor-only, static / movable, cast
  shadow, ray visibility mask. Tags and layers.
- Effort: L.

### 3.4 Transactions
- Route **everything** through `CommandManager`: light creation and edits,
  sun / ambient / env, material edits, graph edits, import settings, asset
  operations, renderer settings.
- Command names, coalescing of continuous drags into one entry, a bounded
  stack, and an Undo History panel.
- Per-document dirty state should come from a document-scoped transaction
  count rather than the global stack.
- Effort: M.

### 3.5 Editor settings and layouts
- `EditorSettings.json` for camera speeds, snap, grid, theme, P4, recent
  projects, keybindings. Project settings separate from user settings.
- Named layouts with save / load / reset to default; ship Default, Level
  Design, Material, Scripting.
- Keybinding table with a Preferences page and conflict detection.
- Effort: S-M.

---

## 4. Viewport

### 4.1 Camera and navigation
- Camera speed slider and scroll speed scaling in the viewport toolbar, plus
  persisted speed.
- Focus (F) fits the selection **bounds**, not the centroid.
- Orthographic Top / Front / Side / Back views, and a 2x2 / 1+3 split layout
  per document with synchronized selection.
- Camera bookmarks (Ctrl+0-9 set, 0-9 jump), saved in the scene.
- Pilot a camera actor from the viewport; "Snap view to object" and
  "Snap object to view".
- Adaptive grid that fades with distance and shows the current snap size.
- Orbit around the pick point under the cursor, not the focus distance.

### 4.2 Selection and manipulation
- GPU picking without a per-mouse-move stall: readback one frame late,
  or read on click only.
- Pivot modes: last selected, centre of bounds, individual origins; temporary
  pivot with the middle mouse.
- Surface snapping (place on ground with normal alignment), vertex snapping
  (V), rotation snap presets, scale-from-corner.
- Gizmos for lights (radius, cone, direction), probes (box, influence),
  volumes (box with face handles), cameras (frustum).
- Duplicate-drag (Alt+drag), group / ungroup, select by type / material / name,
  invert selection, hide selected (H) and show all (Shift+H), isolate.
- Marquee with rotated bounds and frustum vs mesh triangles when precise.

### 4.3 View modes and show flags
- Toolbar combo for Lit, Unlit, Wireframe, Detail Lighting, Lighting Only,
  Reflections, Base Color, Normals, Roughness, Metallic, AO, Emissive, Depth,
  Motion Vectors, GI only, Shadow cascades, Overdraw, Light complexity,
  and the 19 existing DXR lighting views.
- Show flags: grid, gizmos, outlines, light icons, probe icons, volume
  bounds, fog, post, bloom, AA, DLSS, sky, particles, collision.
- Exposure override (fixed EV) in the viewport, independent of the game's auto exposure.
- Stats overlay: FPS, GPU pass timings from the existing `GpuProfiler`,
  draw calls, triangles, TLAS instance count, VRAM.

### 4.4 Render parity with the game
- Enable the GI volume, reflection probes and local shadows in the editor path.
- Read the scene environment texture, not the hardcoded cubemap.
- Expose DLSS / DLAA and NRD toggles and quality presets.
- Fog, sky and post read from the level's authored volumes (section 5.3).
- Real-time "Realtime" toggle per viewport and reduced render rate for
  unfocused documents to fix the every-document-every-frame cost.
- Material preview should render through the same deferred / DXR path as the
  scene so the two match.

### 4.5 Editor visualisation
- Billboard icons for lights, cameras, probes, volumes, empty objects; click to select.
- Light radius / cone wireframes when selected; sun direction widget.
- GI probe volume bounds and per-probe spheres coloured by state.
- Reflection probe capture preview sphere.
- Navmesh overlay when the Navmesh tool is reconnected.

---

## 5. Level authoring

### 5.1 Lights
- Full light editing in the inspector: type, colour, temperature in Kelvin,
  intensity in lumens / candela / lux, radius, range, inner / outer cone,
  source size for soft shadows, cast shadow, IES profile, emissive sphere
  preview, ray visibility. All undoable with transform commands.
- Rect and disc area lights and a directional light actor replacing the
  fixed Sun pseudo-row, so multiple suns and per-level rigs are possible.
- Light placement tools: place on surface, duplicate along path, light
  linking groups.
- Emissive material intensity editing with a nit readout, feeding the
  ReSTIR emissive gather.

### 5.2 Sky, atmosphere, time of day
- A Sky actor holding the physical sky parameters (turbidity, ground albedo,
  night sky, sun disk), HDRI override, and a time-of-day control with a
  scrub bar and sun path preview.
- Save it in the scene. The game reads the same data.

### 5.3 Volumes and post
- Post Process Volume: exposure mode and camera (aperture / shutter / ISO),
  EV comp, tonemapper, bloom, and the future DoF / motion blur / grade /
  vignette / grain, with global vs bounded volumes, priority and blend
  radius.
- Fog Volume and global height fog actor with the existing `AtmosphereParams`.
- GI Volume actor: origin, spacing, counts, intensity, hysteresis, rays per
  probe, bake / re-prime button, show bounds.
- Reflection Probe actor: box, influence, capture button, capture on save.
- Renderer quality settings as a project asset with Showcase / Balanced /
  Performance presets and per-level overrides, replacing the `BENCH_*` env vars.
- Move the `GameWorldDebugUI` tuning panel out of the game into an editor
  "Renderer Settings" panel driven by the same metadata (3.2), and keep a
  read-only runtime overlay in the game.

### 5.4 Cameras and cinematics
- Camera actor with FOV, aperture, focus distance, and a "look through" mode.
- Sequencer-lite: a timeline document with tracks for camera transform, FOV,
  focus, sun time, light intensity, object visibility; keyframes with
  easing; scrub in the viewport; export to the game and to the video capture
  in `PORTFOLIO_SHOWCASE_ROADMAP.md` 4.2.
- Camera path preview drawn in the viewport with editable keys.

### 5.5 Level structure
- World Outliner replacement for the hierarchy: tree table with columns for
  visibility, lock, type icon, and P4 status; reorder; nesting under
  parents; folders as real nodes.
- Layers panel; per-layer visibility and lock.
- Sub-levels or level streaming volumes (optional; only if scenes grow).
- Level blueprint equivalent: attach a script to the scene itself.

### 5.6 Placement and content tools
- Place Actors panel: lights, volumes, probes, cameras, primitives, recently
  placed. Drag into the viewport onto surfaces.
- Foliage-style scatter brush for instanced meshes (optional).
- Measure tool, align tools (align to grid, align selection), distribute.
- Snap settings in the toolbar with persistent values.

---

## 6. Content pipeline

### 6.1 Import
- Asynchronous cooker with a progress bar, cancel and an Output Log panel
  capturing cooker stdout. Use the Release cooker when running Release.
- Import dialog on drop: scale, axis, normal convention, material creation
  rules, texture packing, LOD generation, collision, skeleton handling.
- Reimport with the same settings; detect source changes through the file watcher.
- Batch import and drag-drop of whole folders.
- Mesh decimation already exists in the engine; expose LOD generation in the import settings.

### 6.2 Content Browser
- Thumbnails for every asset type: meshes and materials rendered by the
  editor renderer into a thumbnail cache on disk; scenes from a saved
  screenshot; prefabs from their mesh. Regenerate on change.
- Rename, move, duplicate, delete with reference fixup and a "references
  will break" dialog. Already gated on source control in the usability doc.
- Favourites, collections, recent, path breadcrumbs, back / forward, filters
  by type and tag, sort options, column view with size and modified date.
- Asset picker popup with search and thumbnails for every reference field.
- Right-click Create menu in any folder; asset actions (reimport, find in
  explorer, find references, copy reference).
- Reference viewer and size map.
- Double-click already-open check (`AssetBrowser.cpp:383, 397, 411`).

### 6.3 Source control
- Non-blocking P4: everything through the worker thread with a request queue.
- Check out, revert, diff against depot, submit dialog with changelist
  description, mark for add / delete, history. Status colours in the outliner
  and outline (`SceneUtil.cpp:169`).
- Login dialog reinstated (`SceneDocument.cpp:313-377`).
- Git backend as the second provider through the same interface.

### 6.4 Output log and diagnostics
- Output Log panel: engine log, cooker output, shader compile errors with a
  click-to-open, script runtime errors.
- Message Log: per-asset warnings (missing texture, broken reference, bad normals).
- Crash reporter already exists (`GoEditor.cpp:44`); add an autosave every N
  minutes with recovery on next launch.

---

## 7. Material editor

### 7.1 Editor
- Preview through the deferred / DXR path with the scene's sky, so the
  material matches in-level; preview scenes (studio, outdoor, night).
- Material instances: a `.tgmi` that references a parent and overrides
  exposed parameters. Parameter nodes in the graph become the instance UI.
- Undo for every property and graph edit; copy / paste of nodes; comment
  boxes; node search (Tab); alignment; reroute nodes; per-node preview thumbnails.
- Material functions (subgraphs) with inputs and outputs.
- Apply-to-selection and drag onto a viewport mesh.

### 7.2 Graph
- Nodes: UV / tiling / offset, Time, Fresnel, Noise (Perlin, Voronoi, Simplex),
  Gradient, Remap, Power, Saturate, Normal Blend, Height to Normal, Vertex
  Color, World Position, Camera Vector, Parameter (scalar, vector, texture),
  Switch, If, Panner, Rotator, Desaturate, HSV.
- Bilinear and mip-aware sampling in the baker; box-filtered resize;
  progress and cancel on bake.
- **Runtime path**: generate HLSL for the graph into a material permutation
  used by raster and by `DecodeHit`, with the CPU bake kept as a fallback for
  static textures. This is what turns the tool into a real shader graph.
  Effort: L, and it is the largest single editor feature in this document.
- Shading model selector (Default Lit, Glass, and the future Clearcoat,
  Sheen, Anisotropic, Subsurface from the renderer roadmap).

---

## 8. Scripting, animation, other tools

### 8.1 Visual scripting
- Execution trace: highlight active nodes and links while running, with the
  debug service the `ScriptExecutionContext.h:10` TODO asks for.
- Breakpoints, step, watch values on pins.
- Copy / paste, comment boxes, node search, reroutes, collapse to function,
  variables panel, event list.
- Compile-time validation with errors listed in the Message Log.
- Script asset references through the registry; P4 (`ScriptEditor.cpp:245`).

### 8.2 Animation
- Timeline with scrub, loop range, playback speed, frame stepping.
- Notify track (events at time), curve editing, root motion toggle.
- Blend spaces and a simple state machine document.
- Retargeting between skeletons (optional).

### 8.3 Object definitions
- Parent definition picker and inheritance (`ObjectDefinitionDocument.cpp:308`).
- Script list panel (`:309`), component-style property groups.
- Thumbnail and description; "Create instance in level" button.

### 8.4 Navmesh
- Reconnect the existing Recast tool (`SceneDocument.cpp:77, 98, 270, 378`),
  run the build on a worker, draw the result in the viewport, save with the scene.

### 8.5 Play-in-editor
- Short term: keep the process launch but capture stdout into the Output Log,
  pass the current viewport camera, and add Stop / Restart.
- Long term: in-process simulate using the game world with a saved scene
  snapshot for state restore, eject to editor camera, possess, pause and
  frame step. Depends on the shared scene reader (3.1).

---

## 9. Shell polish

- Editor theme matching a dark-neutral palette; consistent icon set;
  hover tooltips everywhere; property tables with alternating rows.
- Main toolbar: Save, Play / Stop, transform tools, snap, camera speed,
  view mode, show flags, DLSS / renderer preset, exposure.
- Menus: File (New, Open, Recent, Save, Save As, Save All, Import, Export,
  Exit), Edit (Undo, Redo, History, Cut/Copy/Paste, Duplicate, Delete,
  Preferences, Project Settings), Window (every panel), Build (cook
  textures, bake GI, capture probes, build navmesh), Tools (profiler,
  renderer settings, texture importer), Help.
- Status bar: current scene, P4 state, background task progress, memory.
- Notifications toast for completed imports, bakes, saves and errors.
- Per-document tab bar with dirty markers, close all, close others.
- Startup project browser and recent projects.
- Autosave, session restore of open documents and layout.
- High-DPI scaling and font size preference.

---

## 10. Performance and robustness

- Move all long work off the UI thread with a task system: import, bake,
  probe capture, navmesh, thumbnail generation, registry scan. Progress in
  the status bar.
- Cache the asset registry and thumbnails on disk; target sub-second startup
  after the first run.
- Stop the 0.5 s full `SceneCache` reload; reload only what the watcher reports.
- Render only the focused or visible viewports each frame; throttle the rest.
- Replace the per-frame property-set rebuild in the filter bar with a cached
  index invalidated on change.
- Fix the aliasing `sprintf_s`, add a virtual destructor to `Document`,
  guard the null derefs in section 2.
- Replace the singletons for active scene and selection with an editor
  context passed to documents, so two scene documents can be open safely.

---

## 11. Suggested order

Each phase leaves the editor usable end to end.

### Phase 1: stop the bleeding (1-2 weeks)
1. Async cooker with progress and Release exe name.
2. Viewport parity: scene HDRI, GI volume, reflection probes, local shadows.
3. Light property editing in the inspector, undoable.
4. Route sun / ambient / env and light creation through commands.
5. Null-deref, UB and assert fixes; asset-browser mutex scope; property-set
   cache in the filter bar.

### Phase 2: foundations (3-5 weeks)
6. Asset registry with GUIDs, watcher, dependency graph, unified scene reader.
7. Reflection-driven property metadata and the new inspector: categories,
   override indicators, reset, multi-edit.
8. Transactions everywhere, undo history panel, per-document dirty state.
9. Editor settings, named layouts, keybindings.

### Phase 3: level authoring (3-4 weeks)
10. Transform hierarchy and components; outliner tree table with visibility
    and lock; layers.
11. Sky, Post Process, Fog, GI Volume, Reflection Probe actors, saved in the
    scene, read by the game; Renderer Settings panel moved into the editor.
12. View modes, show flags, stats overlay, camera bookmarks, ortho views,
    light and volume gizmos, billboard icons, surface snapping.

### Phase 4: content pipeline (2-3 weeks)
13. Thumbnails for all types, asset picker, rename / move with fixup,
    reimport, Output Log and Message Log.
14. Non-blocking P4 with submit / revert / diff, Git provider.
15. Camera actor and sequencer-lite feeding the showcase video capture.

### Phase 5: authoring depth (4-8 weeks)
16. Material instances, graph undo / copy / comments / functions, then HLSL
    generation for a runtime shader graph.
17. Script execution trace, breakpoints, copy / paste, search.
18. Animation timeline, notifies, blend spaces.
19. In-process play-in-editor with state restore.
20. Reconnect navmesh; placement tools; scatter brush.

---

## 12. Reviewer checklist

- [ ] Open the editor cold in under two seconds on a cached project.
- [ ] Drop an FBX into the viewport; the editor stays responsive and shows progress.
- [ ] Place a point light, edit its intensity in lumens, undo, redo.
- [ ] Rename a material in the Content Browser; every mesh still finds it.
- [ ] Switch view modes and toggle DXR / DLSS from the viewport toolbar.
- [ ] Author sky time of day, fog and post in the level, save, run the game, see the same image.
- [ ] Select five objects and edit their roughness in one go.
- [ ] Undo history panel shows named transactions for every edit made above.
- [ ] Close the editor with unsaved work; reopen; layout and documents are restored.
- [ ] Play in editor, move the camera, stop, and the scene is unchanged.
