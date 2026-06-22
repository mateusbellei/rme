# Data Editor – Implementation Plan

## Objective

Add a **Data Editor** entry to the top menu that opens a UI to edit and save **borders**, **grounds**, and **tilesets** configs. The editor uses items from the **RAW Palette** (item IDs) to build these configs and saves to `borders.xml`, `grounds.xml`, and `tilesets.xml` in the current client version’s data folder.

## Phased Approach

- **Phase 1 — Data Editor UI (current focus):** Build the Data Editor as a whole: menu entry, window, and all 6 tabs (Borders, Doodads, Grounds, Creatures, Walls, Tilesets) with placeholder content. No feature logic yet; UI structure only.
- **Phase 2 — Borders:** Implement the Borders tab (list, 3×3 grid, Add/Remove, Save XML).
- **Phase 3+:** Grounds, then Tilesets; Doodads, Creatures, Walls as needed.

---

## 1. High-Level Architecture

| Component | Purpose |
|-----------|---------|
| **Menu** | New "Data Editor" in top menu bar → opens Data Editor window |
| **DataEditorWindow** | `wxDialog` with tabbed notebook: Borders \| Doodads \| Grounds \| Creatures \| Walls \| Tilesets |
| **Borders tab** | List borders, 3×3 grid editor per border (edge → item_id), Add/Remove/Save XML |
| **Grounds tab** | List ground brushes, LookId/Z-Order, Items list, Border refs, Add/Rename/Remove/Save XML |
| **Tilesets tab** | List tilesets, filter by palette type, add/remove entries (Brush or Item), Save XML |
| **Data path** | Use `g_gui.GetCurrentEditor()` → map version → `getLoadedVersion()->getDataPath()`; files are `borders.xml`, `grounds.xml`, `tilesets.xml` in that directory |

---

## 2. Menu Integration

**Files to change:**

- **`data/menubar.xml`**  
  - Add a top-level menu, e.g. after "Map" or before "About":
  - `<menu name="Data Editor">` with one item: `<item name="Data Editor..." action="DATA_EDITOR" help="Edit borders, grounds, tilesets configs."/>`

- **`source/main_menubar.h`**  
  - In `MenuBar::ActionID` enum, add `DATA_EDITOR`.
  - Declare handler: `void OnDataEditor(wxCommandEvent& event);`

- **`source/main_menubar.cpp`**  
  - Register: `MAKE_ACTION(DATA_EDITOR, wxITEM_NORMAL, OnDataEditor);`
  - In `OnDataEditor`: get `MainFrame*`, ensure a map is loaded (optional: show message if not), then create and show `DataEditorWindow` (e.g. `new DataEditorWindow(frame, *g_gui.GetCurrentEditor())` or pass editor reference as needed). Show modal or modeless per existing pattern (e.g. Edit Towns).

- **`source/gui.h`**  
  - Forward-declare `DataEditorWindow` if the dialog is in its own file.

- **`source/gui_ids.h`**  
  - Add IDs for Data Editor controls (tabs, listboxes, buttons, grid controls) to avoid clashes.

---

## 3. Data Editor Window (Shell)

**New files (suggested):**

- **`source/data_editor_window.h`**
- **`source/data_editor_window.cpp`**

**Responsibilities:**

- **Constructor:** Accept `wxWindow* parent` and `Editor& editor` (or equivalent to get data path and materials).
- **Layout:** `wxNotebook` with tabs: **Borders**, **Doodads**, **Grounds**, **Creatures**, **Walls**, **Tilesets**. For phase 1, only Borders, Grounds, Tilesets are fully implemented; others can show a “Not implemented” or placeholder panel.
- **Data path:** Resolve path to `borders.xml`, `grounds.xml`, `tilesets.xml` via the loaded client version (e.g. from `editor.getMap().getVersion()` → client version → `getDataPath()`). Handle “no version loaded” (e.g. no map open) by disabling Save or showing a message.
- **Close button:** Destroy/close the dialog.

Reference existing dialogs: **`EditTownsDialog`** in `common_windows.cpp` (list + details + buttons), **`PreferencesWindow`** for tab layout.

---

## 4. Borders Tab (UI → borders.xml)

**Data model (existing):**

- **`g_brushes.borders`** is `std::map<uint32_t, AutoBorder*>` (protected in `Brushes`). Each **AutoBorder** has:
  - `id`, `group`, `ground`
  - `tiles[13]`: item IDs for each edge (see `brush_enums.h`: n, s, e, w, cnw, cne, csw, cse, dnw, dne, dsw, dse).
- **borders.xml** format (see `data/1286/borders.xml`):
  - Root `<materials>`, children `<border id="...">`, optional `group="..."`, and `<borderitem edge="..." item="..."/>` for each edge.

**UI (match images 1–2):**

- **Left:** Listbox “Borders:” listing “Border 1”, “Border 2”, … (by border id from `g_brushes`). Selection loads the selected border into the right panel.
- **Right:**
  - “Border ID: X (click to edit)” – display only or simple edit for ID.
  - **3×3 grid** representing the 12 edges (n,w,e,s, corners, diagonals). Each cell shows edge label (e.g. cse, s) and current item_id; clicking a cell allows assigning an item from the RAW Palette (e.g. current brush or a “Browse” / picker). Use **RAW Palette** item IDs only.
  - Status line: e.g. “Border ID 1 edited (edge cse -> 4538) via palette brush.”
- **Bottom:** “+ Add Border”, “- Remove”, “Save XML”.

**Logic:**

- **Listing borders:** Add a public getter on `Brushes` for the borders map (e.g. `const BorderMap& getBorders() const`) so the Data Editor can iterate and list them. Alternatively, the Data Editor can maintain its own list and sync from file on load (see below).
- **Add Border:** Create a new border (new id, empty edges), add to in-memory list and to `g_brushes.borders`, refresh list and grid.
- **Remove:** Remove selected border from `g_brushes.borders`, refresh list.
- **Grid edit:** On “assign” from RAW Palette (or picker), set `AutoBorder::tiles[edge_id] = item_id` for the selected edge; update status text.
- **Save XML:** Build XML from current `g_brushes.borders` (or from an editable copy used only by the Data Editor) and write to `dataPath + "borders.xml"`. Format: `<materials>` with `<border id="...">` and `<borderitem edge="..." item="..."/>`. Optionally reload materials or at least borders so the rest of the editor sees changes.

**Alternative (file-first):** Load borders from `borders.xml` into a local list in the Data Editor, edit in memory, and on “Save XML” write back. Then either reload materials or merge into `g_brushes.borders`. This avoids exposing `Brushes::borders` if preferred.

---

## 5. Grounds Tab (UI → grounds.xml)

**Data model (existing):**

- Ground brushes are loaded in **grounds.xml** as `<brush name="..." type="ground" server_lookid="..." z-order="...">` with:
  - `<item id="..." chance="..."/>` children.
  - `<border align="outer|inner" id="..." to="none"/>` etc.
- Stored in `g_brushes` as brushes; ground brushes are **GroundBrush** with items and border references.

**UI (match images 3–4):**

- **Left:** Listbox “Grounds:” (e.g. “Grass”, “void”, “dried grass”). Select one to edit.
- **Right:**
  - **LookId** (number, spinner).
  - **Z-Order** (number, spinner).
  - **Items (double-click to edit):** List of item IDs (and optionally chance). Buttons “+ Add Item”, “- Remove Item”. Adding an item should use RAW Palette (e.g. pick by current RAW brush or a “Browse” dialog showing RAW items).
  - **Border Refs (double-click to edit, [complex]=read-only):** List of border references (align, id, to). Buttons “+ Add Border Ref”, “- Remove”.
- **Bottom:** “+ Add”, “Rename”, “- Remove”, “Save XML”.
- **Status:** “Ground: Grass | N items | M border refs”.
- If no ground selected: show “Select a ground brush first.” (info dialog or status).

**Logic:**

- **Listing grounds:** Iterate brushes in `g_brushes` that are GroundBrush (by name or type), or parse grounds.xml into a local list for editing.
- **Add/Rename/Remove:** Operate on in-memory list of ground configs (or on GroundBrush objects if API allows).
- **Items list:** Add/remove item IDs; “Add Item” opens a picker or uses current RAW Palette selection.
- **Border refs:** Add/remove entries (align, border id, optional “to”).
- **Save XML:** Write `<materials>` with `<brush name="..." type="ground" server_lookid="..." z-order="...">`, `<item id="..." chance="..."/>`, and `<border .../>` to `dataPath + "grounds.xml"`.

---

## 6. Tilesets Tab (UI → tilesets.xml)

**Data model (existing):**

- **`g_materials.tilesets`** is `TilesetContainer` (map of tileset name → `Tileset*`). Each **Tileset** has categories (Terrain, Doodad, RAW, etc.) and each category has a list of brushes/items.
- **tilesets.xml** (see `data/1286/tilesets.xml`): `<materials>`, `<tileset name="Grounds">`, then `<raw>`, `<item id="..."/>` or `<item fromid="..." toid="..."/>`, etc.

**UI (match images 5–8):**

- **Left:** Listbox “Tilesets:” (e.g. “(A) Pisos”, “Grounds”). Filter dropdown: “ALL”, “Terrain Palette”, “Doodad Palette”, “Collections Palette”, “Item Palette”, “Waypoint Palette”, “RAW Palette”.
- **Right (when a tileset is selected):**
  - Breadcrumb/path: e.g. “terrain > brush: Grass”.
  - Preview (e.g. one tile or brush preview).
  - List or grid of entries in the tileset (depending on filter).
- **Bottom:** “+ Add”, “Rename”, “- Remove”, “Save XML”; “+ Add Entry”, “- Remove Entry” for entries inside the selected tileset. Status: “Tileset: (A) Pisos | N entries”.
- **Add Entry dialog:** “Choose entry type:” **Brush** or **Item**. If Brush: “Choose palette category for brush:” (Terrain, Doodad, Terrain & RAW, Doodad & RAW, RAW Palette, Items Palette). Then add the selected brush/item to the tileset.

**Logic:**

- **Listing tilesets:** From `g_materials.tilesets` (names).
- **Filter:** Filter entries shown in the right panel by palette type (Terrain, Doodad, RAW, etc.).
- **Add Entry:** Modal “Add Entry” → Brush or Item → if Brush, choose palette category → pick from that palette (or RAW) and add to the selected tileset’s appropriate category.
- **Remove Entry:** Remove selected entry from tileset.
- **Save XML:** Serialize `g_materials.tilesets` (or edited copy) to `dataPath + "tilesets.xml"` in the existing tileset XML format.

---

## 7. RAW Palette Integration

- **Borders:** Assigning an item to an edge = choosing an **item_id** from the RAW Palette. Use either the current RAW brush’s item ID when the user “clicks” the grid cell, or a “Browse” dialog that lists RAW items (from `g_items` filtered by what the RAW palette shows, or from the current palette’s brush list).
- **Grounds – Items list:** “+ Add Item” should add an item by **item_id** from RAW Palette (picker or current selection).
- **Tilesets:** “Add Entry” with “Item” or “RAW Palette” adds that item/brush to the tileset.

Ensure the editor has access to the current palette or a way to resolve “current RAW brush” / “browse RAW items” (e.g. via `g_gui` or palette window references).

---

## 8. File I/O and Data Path

- **Resolve path:** From the current map’s client version: e.g. `Editor& e` → `e.getMap().getVersion()` → get `ClientVersion*` → `getDataPath()`. Concatenate with `"borders.xml"`, `"grounds.xml"`, `"tilesets.xml"`.
- **Load:** For a “file-first” approach, the Data Editor can load the three XMLs on open and populate its lists; for “memory-first”, it can use `g_brushes` / `g_materials` and optionally re-read files to sync.
- **Save:** Write XML to the resolved path. Use existing XML writer (e.g. pugixml) and match existing format exactly so the rest of the app can reload without breakage.
- **Reload:** After “Save XML”, consider calling the same reload logic used by “Reload” (F5) for materials so the main editor sees new borders/grounds/tilesets without restart.

---

## 9. Implementation Order

1. **Menu + shell**
   - Add “Data Editor” to `menubar.xml`, `main_menubar.h/cpp`, `gui_ids.h`.
   - Implement `DataEditorWindow` with notebook and tabs; only Borders, Grounds, Tilesets panels have real content; others show placeholder.
2. **Borders tab**
   - Expose or replicate border list; implement list + 3×3 grid + Add/Remove; wire RAW Palette (or picker) for edge → item_id; implement Save to `borders.xml`.
3. **Grounds tab**
   - List ground brushes; LookId, Z-Order, Items list, Border refs; Add/Rename/Remove ground; Add/Remove item and border ref; Save to `grounds.xml`.
4. **Tilesets tab**
   - List tilesets; filter; Add/Remove entry with “Add Entry” dialog (Brush/Item, palette category); Save to `tilesets.xml`.
5. **Polish**
   - Status messages, “Saved” confirmation (e.g. “tilesets.xml saved!”), validation, and optional materials reload after save.

---

## 10. Files to Add/Modify (Summary)

| Action | File |
|--------|------|
| Add | `source/data_editor_window.h` |
| Add | `source/data_editor_window.cpp` |
| Edit | `data/menubar.xml` – add Data Editor menu + DATA_EDITOR action |
| Edit | `source/main_menubar.h` – ActionID DATA_EDITOR, OnDataEditor declaration |
| Edit | `source/main_menubar.cpp` – MAKE_ACTION, OnDataEditor implementation |
| Edit | `source/gui.h` – forward declare DataEditorWindow (if used from menu) |
| Edit | `source/gui_ids.h` – IDs for Data Editor controls |
| Edit | `source/brush.h` – optional: `const BorderMap& getBorders() const` for Borders tab |
| Edit | `source/CMakeLists.txt` – add `data_editor_window.cpp` (and `.h` if listed) to build |

---

## 11. Reference – XML Snippets

**borders.xml:**
```xml
<materials>
  <border id="1">
    <borderitem edge="n"   item="891"/>
    <borderitem edge="cse" item="897"/>
    ...
  </border>
</materials>
```

**grounds.xml:**
```xml
<materials>
  <brush name="grass" type="ground" server_lookid="4526" z-order="3500">
    <item id="4526" chance="2500"/>
    <border align="outer" id="2"/>
    <border align="inner" to="none" id="1"/>
  </brush>
</materials>
```

**tilesets.xml:**
```xml
<materials>
  <tileset name="Grounds">
    <raw>
      <item id="100"/>
      <item fromid="4609" toid="4625"/>
    </raw>
  </tileset>
</materials>
```

This plan aligns with the described UI (images 1–8) and focuses on **borders**, **grounds**, and **tilesets** first, using the RAW Palette for item IDs and saving to the version-specific XML files.
