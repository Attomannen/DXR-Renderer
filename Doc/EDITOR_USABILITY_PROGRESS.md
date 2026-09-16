# Editor Usability Progress

This is the implementation checklist for the Unity-inspired editor usability pass.
Items marked complete have been implemented in the current workspace and compiled in
the `GameEditor.sln` Debug/x64 build.

## Completed

- [x] Shared Inspector UI framework
  - Persistent ImGui foldouts.
  - Consistent two-column `Label | Control` property tables.
  - Shared reset buttons, delayed tooltips, and inline warning presentation.
- [x] Material Inspector usability
  - Categorized Material, Surface, Emission, and Texture Maps sections.
  - Contextual controls: Alpha Cutoff only for masked materials; emission controls
    are disabled until emission is enabled; Normal Strength is disabled without a
    normal map.
  - Reset Material action.
  - Texture slots accept DDS drag/drop or the selected Asset Browser texture instead
    of requiring paths to be typed.
- [x] Material preview workflow
  - Preview mesh selection: sphere, cube, cylinder, cone, torus, and plane.
  - Preview mesh reloads only when the selected mesh changes.
  - Folded Preview Lighting and Environment controls.
  - Resettable lighting rig and Asset Browser cubemap assignment/clear action.
- [x] Hierarchy workflow
  - Search and property-type filtering.
  - Folder tree and drag/drop grouping.
  - Multi-selection.
  - Inline rename using F2 or the context menu.
  - Undoable Duplicate and Delete context-menu actions.
  - Delete key for hierarchy selection, guarded while text input is active.
- [x] Inspector selection feedback
  - Clear empty-selection guidance.
  - Selected Sun and Ambient lights use the shared inspector section layout.
  - Multi-selection feedback states that transform edits remain one undoable action.
- [x] Asset Browser usability
  - Case-insensitive search.
  - Type filters: All, Materials, Textures, Scenes, Meshes, and Other.
  - Alphabetically sorted folders and assets.
  - Distinct Material, Texture, Scene, Mesh, Definition/Clip type icons.
  - Create Folder modal with name validation and inline error reporting.
  - File context actions: Select and Copy Path.
- [x] Centralized creation menu
  - A top-level `Create` menu for Scene, Object Definition, Animation Clip,
    Material, and FBX Import Settings.
- [x] Build environment reliability
  - `build_game.ps1` sanitizes the spawned MSBuild environment so duplicate `Path`
    and `PATH` entries cannot prevent `cl.exe` from launching.
  - The wrapper accepts `-Solution`, allowing the same safe build flow for the game
    and editor solutions.

## Remaining Priorities

- [ ] Metadata-driven material/shader property descriptions.
  - Display name, category, tooltip, ranges, defaults, texture type, advanced and
    conditional visibility should become shader/material metadata instead of being
    specific to the current PBR material struct.
- [ ] Asset picker popup and asset thumbnails for all asset classes.
  - Texture assignment works through selected assets and drag/drop today; a searchable
    picker popup is the next step.
- [ ] Full component-oriented Scene Object Inspector.
  - Transform, Renderer, Light, and authored properties should become separately
    collapsible component panels with per-component actions.
- [ ] True mixed-value multi-edit.
  - Safely show mixed values and apply compatible property edits across selected
    objects without overwriting unrelated data.
- [ ] File operations for assets.
  - Rename, duplicate, and delete require source-control-aware implementation and
    confirmation before they should be exposed in the Asset Browser.
- [ ] Global renderer settings asset.
  - The runtime render tuning UI is already organized into tabs, but settings are not
    yet a dedicated editor-authored asset with quality presets and override tracking.
- [ ] Renderer debug/profiling expansion.
  - Add GPU timing presentation and consolidate debug views in the editor-facing
    renderer settings workflow.

## Verification

Latest successful command:

```powershell
.\build_game.ps1 -Solution GameEditor.sln -Configuration Debug -Platform x64 -Target GameEditor
```

Known non-blocking build warning: `GameMain` shares an intermediate directory with
the filtering/settings preview projects (`MSB8028`).
