# Level format

Levels are XML property lists (`Levels/<chapter>/<Name>.plist`), AES-encrypted (see 01). Reader/writer:
`st::LevelLayoutUtils::LoadPlist / SavePlist` → `st::(anon)::InitLevelLayoutFromDict / GetDictFromLevelLayout`
→ `st::LevelLayoutUtils::Apply` (layout → `GameState`). All facts below are **[verified]** unless tagged.

## 1. Chapter index — `0_Location.plist`

```xml
<dict>
  <key>levels</key><array><string>Playtime</string><string>CatchBall</string>…</array>  <!-- file names, play order -->
  <key>name</key><string>CHAPTER_NAME_CHAPTER1</string>                                 <!-- localisation id -->
</dict>
```
Chapters: `00_Classroom` (16 levels), `01_Backyard` (32), `02_Room` (32), `03_Treehouse` (36) = **116 levels**
(by file count; the Treehouse index lists only 32 — `HoneyBucket`, `OpenFire`, `RescuePiggy`, `Ricochet` exist as
files but are not in `0_Location.plist`, so the *original* never offered them, older-format leftovers (`version`
6 vs. the shipped levels' 7) with the title/tip stored as literal English text rather than a locale id;
`import_assets.py` imports them under `unlisted` in `index.json`, 12 §1. The remake restores them: `AppState::
loadCatalogue` (`core/ui/app_state.cpp`) appends the chapter's `unlisted` names after its `levels`, so they play
as Treehouse levels 33-36 — new `LocationState` slots, existing saves' indices 0-31 untouched. They have no
thumbnail in the source assets (none were ever shipped) and no non-English text (`Localization::text` falls back
to the id, which for them is already the literal string) — both cosmetic. `gen_levels_catalog.py`/07 still lists
only the original's 112 in play order, since it (docs/07) reflects the original's own index.
`backgroundIndex` of the levels equals the chapter index (0..3). User levels live in the documents folder
with the same schema (`LocationInfoUtils::LoadFromDocs`, `GenerateUniqueFilename`).

## 2. Level dictionary

| Key | Type | Meaning |
|-----|------|---------|
| `version` | int | layout version; shipped levels use 5, 6 or **7** (current). Loader special-cases version 4 goals and the legacy type-40 item in versions ≤ 6 (see §5) |
| `title` | string | localisation id `TEXT_LEVEL_NAME_<cc>_<nn>` (user levels: literal title) |
| `description` | string | localisation id `TEXT_LEVEL_TIP_<cc>_<nn>` — the tip shown on the loading screen and in-game |
| `authorName` | string | `"Amazing Alex"`, `""`, or a designer name (Noel, Miguel) — display only |
| `serverPath` | string | server id for shared levels, empty for bundled ones |
| `backgroundIndex` | int | 0..3 → `LocationBackground0N.pvr`, foreground and **world-bound type** (3 = Treehouse has a floor hole, see 04) |
| `toolboxSlotCount` | int | number of toolbox slots, followed by `toolboxSlots_<i>` dicts |
| `toolboxSlots_<i>` | dict | `{type: ItemType, amount: int}` — items the player may place |
| `itemCount` | int | number of pre-placed items, followed by `itemInfos_<i>` |
| `itemInfos_<i>` | dict | item record (§3) |
| `goal` | dict | win condition (§5) |
| `rewardId` | int | 0 = none; 1..21 = index into the `st::Rewards` table (sticker + item types, listed in 05 §4; 19 HD levels carry one, rewards 9 Doll and 19 Bouncy Ball are never referenced). **Loaded and saved, but effectively unused**: its only consumer `GameProgressUtils::ApplyRewards` is never called — sandbox items unlock per completed chapter (`UnlockItems`, 05 §4) [verified] |
| `tested` | bool | editor flag "level has been solved by its author" (sharing requires it) |
| `sharedPublicly` | bool | sharing flag, always `false` in bundled levels |

Data-mined ranges (116 levels, `tools/dump_levels.py`): `toolboxSlotCount` 0..7, `itemCount` up to ~40,
1791 items total, `flags` ∈ {0,1,2,3}, `attachmentCount` ∈ {0,1,2}.

## 3. Item record `itemInfos_<i>`

```xml
<dict>
  <key>type</key><integer>23</integer>          <!-- st::ItemType (03-game-items.md) -->
  <key>handle</key><integer>1543524377</integer>
  <key>center_x</key><real>2.947</real>          <!-- world metres, x → right, y → up, origin bottom-left -->
  <key>center_y</key><real>0.899</real>
  <key>angle</key><real>0</real>                 <!-- radians, CCW -->
  <key>flags</key><integer>0</integer>
  <key>ropeEndPos_x</key><real>0</real>          <!-- second endpoint, only for Rope(9)/Slingshot(34)/ZipLine(42) -->
  <key>ropeEndPos_y</key><real>0</real>
  <key>itemData</key><integer>0</integer>
  <key>attachmentCount</key><integer>0</integer>
  <key>attachments_<k></key><dict> state, objectIndex, index </dict>   <!-- k < attachmentCount -->
</dict>
```

Runtime struct `LevelLayout::Item` (64 bytes): `+0 type, +4 handle, +8 cx, +0xC cy, +0x10 angle, +0x14 flags,
+0x18/+0x1C ropeEnd, +0x20 itemData, +0x24 attachmentCount, +0x28.. attachments[2] {state, objectIndex, index}`
(max **2** attachments per item — matches the data).

### 3.1 `handle`

`handle = type << 26 | generation << 12 | slot` (`st::HandleManager::Add`): top 6 bits are the item type
(verified for all 1791 items: `handle >> 26 == type`), bits 12..25 a 14-bit generation counter, bits 0..11 the
slot in the handle table (max 4096 live objects). Handles are stored as signed 32-bit ints (types ≥ 32 give negative
values). On load `WorldStateUtils::AddItemWithHandle` re-registers the item under the stored handle, so
handles are the identity used by `goal.itemHandles*` and `itemData`. Every item in a level needs a unique handle;
`LevelLayoutUtils::Apply` rejects layouts where `(handle >> 24) & 0xFC == 0` or `handle > 0xABFFFFFF`
(i.e. type must be 1..42).

### 3.2 `flags` (bit mask)

| Bit | Value | Effect in `LevelLayoutUtils::Apply` |
|-----|-------|-------------------------------------|
| 0 | 1 | **fixed**: sets flag bit 2 (value 4) of the physics object's flags byte (`PhysicsObject+0xC`, see 04 §4) [verified]; fixed items are part of the level and cannot be picked up, moved or deleted by the player: `GameTouchHandler::Process` checks `+0xC & 4` on the touched object and, instead of starting a drag, queues the "buzz" action 0xE (`UISelectBuzz` + shake, 05 §7) [verified]. In the sandbox editor this is the "lock" toggle. Items from the toolbox are never fixed (`WorldStateUtils::MarkAllSolutionItemsFromToolboxNotFixed`) |
| 1 | 2 | **flipped** horizontally: object scale.x = −1 (`GameItemUtils::Flip`; double-tap in game). Sprites and body shapes are mirrored; direction-sensitive items (glove, truck, plane, dart, scissors, seesaw…) use the sign |

Note: in `LevelLayoutUtils::Get` (runtime → layout) bit 0 is written from flags bit 2 (`+0xC & 4`) and bit 1 from `scale.x < 0`.
Both pre-placed *and* toolbox-placed items appear in `itemInfos` when a solution is saved (`saveSandboxLevelSolution`).

### 3.3 `itemData`

Type-specific integer, filled by `LevelLayoutUtils::Get`:

| Item type | Content |
|-----------|---------|
| 35 RCTruck | handle of its RCController (36) |
| 36 RCController | handle of the controlled item (35 truck or 39 helicopter) |
| 37 Trapdoor | handle of its TrapdoorLever (38) — `0` if unlinked |
| 38 TrapdoorLever | handle of its Trapdoor (37) |
| 39 Helicopter | handle of its controller (36 or 40) |
| 40 (helicopter controller) | handle of the Helicopter (39) |
| 15 Book | book colour 0..3 → sprite frame `7 + value` (BookBlue/BookGreen/BookRed/BookYellow) and the hard-coded size table (03 §15) |
| 24 Billboard | low nibble = hint picture: 2 → `Shelf.png`, 3 → `Book.png` (anything else draws nothing); high nibble = authoring ordinal (1 or 2), never read (03 §24) [verified] |
| others | 0 |

### 3.4 `ropeEndPos_x/y`

**Offset from `center`** (metres, not a world position) of the second end of two-ended items: Rope (9) — far
end (`GameItem+0x8`); ZipLine (42) — far anchor (`+0x8`); Slingshot (34) — pouch position (`+0xC/+0x10`). For
everything else `0,0`. `CreatePhysics` uses `center + ropeEndPos` for the far end
[verified: stage-2 G3 — the stage-1 harness treated the field as a world position, see 09 §1].

### 3.5 `attachments_<k>` — `{state, objectIndex, index}`

Attachment points are defined per item type in the physics template (03-game-items.md §2).
Each record describes what *this* item's attachment point `k` is connected to:

| Field | Meaning |
|-------|---------|
| `state` | 0 = free, 1 = **snapped** (position locked, no joint — used while dragging), 2 = **attached** (revolute joint created, `AttachmentUtils::Attach`) |
| `objectIndex` | index (into `itemInfos`) of the other item, −1 if free |
| `index` | attachment-point index on the other item, −1 if free |

Both sides of a connection carry a record (e.g. rope end ↔ bucket handle). `LevelLayoutUtils::CleanAttachments`
removes records pointing at stripped items. On load `GamePhysicsUtils::CreateAttachments` re-creates the joints
and then calls `RopeUtils::UpdatePosFromAttachedObjects` for every rope: the rope is moved onto the attached
objects, its end vector is recomputed from their world positions and, when the resulting link count differs from
the one built from `center`/`ropeEndPos`, the link bodies are destroyed and rebuilt (SlamDunk's rope goes from the
plist's count to 10 links this way) [verified with the physics harness, 09 §1].

## 4. Toolbox

`toolboxSlots_<i> = {type, amount}`; at most 7 slots in shipped levels. Slot rendering and drag/drop:
`st::Toolbox*`, `ToolboxUtils::AddItem/RemoveItem/GetSlotForPos`, icon sizes from `UIElements` atlas
(`ToolboxUtils::InitializeButtonSizesFromTextures`). Items dragged out become non-fixed world items;
dragging an item back onto the toolbox (`IsOverToolbox`) returns it to its slot.

## 5. Goal

```xml
<key>goal</key><dict>
  <key>type</key><integer>7</integer>
  <key>itemCount</key><integer>1</integer>
  <key>itemHandles</key><array>  9 integers </array>     <!-- target items; only first itemCount used -->
  <key>itemHandles2</key><array> 9 integers </array>     <!-- paired items for types 2 and 7 -->
  <key>timeLimit</key><integer>0</integer>              <!-- unused, always 0 -->
  <key>height</key><real>0.9</real>                     <!-- goal marker y (and y threshold) -->
  <key>width</key><real>1.2</real>                      <!-- goal marker x (and x threshold) -->
  <key>angle</key><real>230.1</real>                    <!-- marker rotation, degrees (values 0, 5.1, 225.1, 230.1, 295.1 seen) -->
  <key>negated</key><false/>                            <!-- stored and re-saved but never read by the game [verified: no reader in the binary]; true in 2 levels -->
</dict>
```
Runtime `st::Goal`: `+0 type, +4 itemCount, +8 handles[9], +0x2C handles2[9], +0x50 timeLimit, +0x54 height,
+0x58 width, +0x5C angle, +0x60 negated`. Maximum **9** targets.

### Goal types (`GoalStateUtils::IsGoalComplete`, `WorldContactListener::PreSolve`)

| type | Levels | Condition (all targets must satisfy) | Marker (`VisualWorldStateUtils::SetGoalMarkers`) |
|------|--------|--------------------------------------|---------------------------------------------------|
| 1 | 0 | target touches any fixture whose category ≠ Static and ≠ Debris (contact, instant) | — |
| 2 | 12 | `itemHandles[i]` **touches** `itemHandles2[i]` — detected in `PreSolve` on first contact point (instant) | type-8 marker (floor-style) at (width, height) |
| 3 | 7 | target is **in contact with the floor** (`GamePhysicsUtils::IsFloorCollidingWith`, i.e. touching the WorldBound item) | type-8 marker at (width, height) |
| 4 | 0 | never completes | — |
| 5 | 18 | target's state byte (`PhysicsObject+0xD`) bit 0 set = **activated**: balloon popped, piggy bank broken (`GameItemUtils::Break`) | marker on the item (kind 6) |
| 6 | 14 | target position **y ≥ height** | up-arrow marker (kind 5) at (clamp(width,0,3.41), clamp(height,0,2.12459)) |
| 7 | 41 | for each pair, `itemHandles[i]` (a container: LaundryBasket 32, Bucket 7) has been in contact with `itemHandles2[i]` through its **goal sensor fixture** (filter group −7) for ≥ **0.3 s** cumulative (`GoalStateUtils::Update` accumulates `dt` while the contact with the sensor exists, resets to 0 when it ends) | marker on item (kind 1) |
| 8 | 20 | target **y ≤ height** | down-arrow marker |
| 9 | 2 | target **x ≥ width** | right-arrow marker |
| 10 | 2 | target **x ≤ width** | left-arrow marker |

For types 5–10 the per-target marker is placed on the item (`kind` 6 for type 5, else 1) and one extra marker
at the threshold. Type 2/3/6/7/8/9/10 checks run every simulation tick in `GameScreen::UpdateSimulation`
after the physics step; the moment `IsGoalComplete` returns true an `Action` of type 11 (goal complete) is
queued. Types 1 and 2 are instead triggered from `WorldContactListener::PreSolve` [verified]: when the goal is not yet
complete and one fixture belongs to the target item (handle `goal.itemHandles[0]`) and is not `Debris`
(category 8), then type 1 completes if the other fixture is not `Static` (category 1), type 2 if the other fixture's
item handle equals `itemHandles2[0]`. The helper `FUN_000fb998(bodyA, bodyB, manifold, queue)` then computes
`|(m_B·v_B(p) − m_A·v_A(p)) · n|` (mass-weighted relative velocity at the contact point along the normal) and
queues `Action(11)` if it is `≥ 0` — i.e. the impact threshold is effectively **zero**, the first contact counts. Stars (`GoalStar`, type 23) are independent of the goal — see 05.

### Version-4 compatibility

`LevelLayoutUtils::Apply`: if `version == 4`, goal type 7 lists were stored as a flat list
(`handles[0]` = container, `handles[1..]` = objects) and are converted to the pair form; type 2 likewise
(`handles2[0] = handles[1]`, `handles[1] = 0`); `itemCount` is decremented **unconditionally** in both branches
[verified: `Apply` +0x25b8], so a count of 0 or 1 turns into −1 or 0 — the importer mirrors this rather than guarding
it. Shipped levels are ≥ 5, so this only matters for very old user levels.

### Legacy type 40 (versions ≤ 6)

`LevelLayoutUtils::LoadPlist` [verified]: for `version < 8 && version != 7` every item of type **40**
(`RCHelicopterController` in old layouts) is rewritten to type **36 `RCController`** — its handle's type bits become
`36 << 26` — and every Helicopter (39) `itemData` handle is rewritten the same way, so the pair stays linked. Two
shipped levels (`03_Treehouse/Helipad`, `03_Treehouse/HoneyBucket`, both version 6) contain such an item. At runtime
type 40 is reused for the editor's `SelectionArea` helper object (`GameScreenController::CreateSelectionAreaObject`),
which is never saved.

## 6. Coordinate system & units

* World coordinates are metres; 1024 px at the reference resolution = 3.41 m (`WorldStateUtils::GetPixelToMetersFactor` = 3.41/1024 = 0.0033300782).
* Playable area x ∈ [0, 3.41], y ∈ [0, 2.12459] (768 px minus the 130 px floor strip); the camera may pan/zoom (`CameraUtils`).
* Angles are radians CCW; `goal.angle` is the exception (degrees).
* Number parsing [verified: `Containers::DataDictionary::Load`, `GetValueFloat`, the item reader `FUN_000d7990`]: every
  `<real>` is converted with `strtod` (Bionic's gdtoa implementation; its correct rounding is assumed, not tested) and stored as a **double**; the level loader reads
  it through `GetValueFloat`, i.e. `(float)double` — decimal → double → float32 with round-to-nearest at both steps.
  An importer that does `float(text)` in Python and packs it as float32 (`struct.pack('<f')`) produces the same bits.
* Item `center` is the physics body origin (= sprite centre for simple items; compound items define offsets from it).

## 7. Related files

* Solutions: `st::SolutionInfo`, `SerializationUtils::AllocSolutionsFilePath` — a solution is a full level layout
  (same schema) containing the player's placed items, keyed by level; used for "replay" and sharing.
* Progress: `st::GameProgress` / `st::LocationState` (encrypted `DataDictionary`, see 05).
* `Tips.plist`: array of `{Image, Text, objectType, Platform?}` — loading-screen tips keyed by item type
  (`objectType` = ItemType, 0 = generic; `Platform` 1 = iOS-only tip).
