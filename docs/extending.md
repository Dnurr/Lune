# Extending Lune

Recipes for the most common changes. Each one lists every file you need to touch.

## Add a component

Example: a simplified version of the real `Light` component (see `lightComponent()` in
`engine/scene/components.cpp` for the full one).

1. **Data**: add the fields to `Entity` (`engine/scene/entity.h`), or a struct held by it:

   ```cpp
   struct Light { float intensity = 3.0f; QColor color = Qt::white; };
   std::optional<Light> light; // absent = the entity has no light
   ```

2. **Descriptor**: in `engine/scene/components.cpp`, write a function returning a
   `ComponentDesc` and add it to `components()`:

   ```cpp
   ComponentDesc lightComponent()
   {
       ComponentDesc c;
       c.name = QStringLiteral("Light");
       c.properties = {
           { QStringLiteral("intensity"), PropertyType::Float, 0.01, 0.0, 1000.0 },
           { QStringLiteral("color"), PropertyType::Color },
       };
       c.present = [](const Entity &e) { return e.light.has_value(); };
       c.get = [](const Entity &e, int i) -> QVariant {
           return i == 0 ? QVariant(double(e.light->intensity)) : QVariant(e.light->color);
       };
       c.set = [](Entity &e, int i, const QVariant &v) {
           if (i == 0) e.light->intensity = float(v.toDouble());
           else e.light->color = v.value<QColor>();
       };
       c.add = [](Entity &e) { e.light = Light {}; }; // optional component: lets files add it
       return c;
   }
   ```

That's all for the editor: the **Inspector** shows a "LIGHT" section with a number field
and a color swatch, edits are **undoable**, and the component is **saved and loaded**
in `.lune` files. Use it from the renderer as needed.

Notes:

- `PropertyDesc::dragStep` is the change per pixel when dragging the label.
- Mark derived values `readOnly` (shown as text, not saved).
- A choice between a few values is `PropertyType::Enum`: the value is an `int` index into
  `PropertyDesc::options`, the Inspector shows a drop-down, files store the option id
  (`"point"`), so reordering options doesn't break old files.
- A file reference is `PropertyType::File`: the value is an absolute path (`QString`, empty
  = none), the Inspector shows a file picker filtered by `PropertyDesc::fileFilter`, files
  store the path relative to the scene.
- An optional component (present only on some entities) needs `add`: loading a file that
  has the component calls it before setting the properties.
- Property and component names are part of the file format: renaming them makes old
  files lose that value (it falls back to the default).
- To let the user *add* the component, add an action that pushes a command setting
  `light` (see below).

## Add an action

1. Create a `QAction` in `Editor::createMenus()` (`frontend/editor.cpp`) and add it to a
   menu:

   ```cpp
   auto *frame = new QAction("Frame Selection", this);
   frame->setShortcut(QKeySequence(Qt::Key_F));
   connect(frame, &QAction::triggered, this, &Editor::frameSelection);
   viewMenu->addAction(frame);
   ```

2. It appears in the **command palette** automatically, as `View: Frame Selection` with
   its shortcut. Disabled actions are hidden from the palette; set
   `action->setProperty("paletteHidden", true)` to always hide one.

If the action changes the scene, do it through a command (next section).

## Add an undoable operation

1. Subclass `QUndoCommand` in `frontend/core/commands.h/.cpp`. `redo()` applies the
   change to the `Scene`, `undo()` reverts it; store everything needed to do both.
2. Push it: `m_undo->push(new MyCommand(...))`. Never change the scene directly from UI
   code, or the change can't be undone and the title won't show it as unsaved.
3. For continuous edits (drags), either implement `id()` and `mergeWith()` (see
   `SetPropertyCommand`), or preview live and push one command at the end with the
   explicit "before" state (see `SetTransformCommand` and the gizmo in `Viewport`).
4. Add a test in `tests/tst_commands.cpp`.

## Add a mesh format

1. Write a loader in `engine/io/` returning a `MeshPtr` (see `objloader.cpp`): fill
   `vertices`, `indices` (counter-clockwise triangles), `name`, set `source` to the
   absolute file path and call `computeBounds()`.
2. Register the extension in `importMesh()` and `meshFileFilter()`
   (`engine/io/meshimport.cpp`). Import and scene loading pick it up from there.
3. Add the `.cpp` to `lune_engine` in `CMakeLists.txt` and a test in
   `tests/tst_meshimport.cpp` (write the test file from the test itself).

## Add a shader or a render pass

1. Put the GLSL (`#version 440`) in `src/engine/render/shaders/` and list it in
   `qt6_add_shaders()` in `CMakeLists.txt`. It's loaded as
   `:/shaders/<file name>.qsb`.
2. In `Renderer`: create the resources in `initialize()` /
   `createSizeDependentResources()`, the pipeline in `createPipelines()`, release them in
   `release()`, and record the pass in `render()`.
3. Fullscreen passes: use the `fullscreen` vertex shader already loaded in
   `createPipelines()`, so texture coordinates are consistent on every backend (see
   [Rendering](rendering.md#coordinate-conventions)).
4. Use a separate `Dynamic` uniform buffer per pass if a buffer would otherwise be
   updated twice in one frame.

## Add a third-party library

1. In `cmake/dependencies.cmake`, declare it with `FetchContent_Declare` pinned to a
   **release tag**, then `FetchContent_MakeAvailable`.
2. Expose a target (the library's own, or an `INTERFACE` target for header-only ones, as
   for cgltf) and link it in `CMakeLists.txt`, `PRIVATE` unless its headers appear in
   Lune's headers.
3. Add it to "Third-party" in the README with its license.

## Add a test

1. Create `tests/tst_<name>.cpp` with a `QObject` test class, `private slots` for the
   test functions, then:

   ```cpp
   QTEST_GUILESS_MAIN(TestName)
   #include "tst_<name>.moc"
   ```

2. Add `lune_add_test(tst_<name>)` to `tests/CMakeLists.txt`.
3. Run `test.bat` (or `test.bat -R <name>`).

Tests link `lune_editor_core` (and through it `lune_engine`), so they can use
everything except widgets. Create any test files at runtime in a `QTemporaryDir`.
