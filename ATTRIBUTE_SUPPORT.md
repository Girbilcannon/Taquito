# Taquito XML Attributes (as of build 1.0.1.2)
## Supported Attributes

| Guide group | Support | XML names, types and Taquito behavior |
| --- | --- | --- |
| Achievement | No | achievementId, achievementBit: integer; no account progress check. |
| Alpha | Yes | alpha: float default 1; clamped opacity 0–1, multiplied by distance fade. |
| AnimSpeed | Yes | animSpeed: float default 1; trail texture repeats per second in world and map views, independent of trail width or texture aspect ratio. 0 stops scrolling; negative values reverse it. |
| AutoTrigger | Yes | autoTrigger: bool default 0; entering triggerRange activates behavior and category actions. Automatic clipboard copying additionally requires Always clipboard mode and its cooldown. First observation inside range establishes a baseline; leave and re-enter to trigger. |
| Behavior | Partial | behavior: integer default 0. Modes 1 (map change), 2 (daily UTC), 3 (permanent), 4 (resetLength seconds), 6 (map/shard/instance per session), 7 (daily per character) and 101 (Monday 07:30 UTC) are implemented for markers. Mode 5 has no reset algorithm. |
| Bounce | Yes | bounce: string bounce or rise, absent/empty disables; bounce-height: float meters default 2; bounce-duration: float seconds per leg default 1; bounce-delay: float seconds default 0. Focus within triggerRange animates the world marker without modifying coordinates or requiring activation. |
| CanFade | Yes | canFade: bool default 1; 0 bypasses optional player-area fading for this marker/trail. Explicit alpha, distance fades, mount/category/behavior filters still apply. |
| Color | Yes | color: color default #FFFFFF; multiplies marker, sticky and trail image channels. AARRGGBB (without #) supplies alpha first; #RRGGBBAA supplies alpha last. Color alpha multiplies alpha and world distance fade. |
| Copy | Yes | copy: string default empty; clipboard text for POIs or explicit category actions. copy-message: string, default `'{0}' copied to clipboard.`; `{0}` expands to the copied value. POIs use triggerRange in meters and the separate Copy panel button/marker-interaction keybind (default Alt+F), or autoTrigger. Click separator labels or use category right-click Copy text. Clipboard mode can require manual interaction or disable all copying. |
| Cull | No | cull: no culling mode. |
| DefaultToggle | Yes | defaultToggle: bool default 1; initial state of this category only. Saved user choices override it. Not inherited as the state of child categories. |
| DisplayName | Yes | DisplayName: string; category menu label, falls back to name. |
| Fade | Yes | fadeNear, fadeFar: float default -1; game inches, world opacity fades from near to far. Either negative disables distance fading. Does not control the sticky screen copy. |
| Festival | No | festival: no event feed. |
| GUID | Yes | GUID: base64 16 bytes or textual GUID for activation identity. Equivalent valid forms normalize to one state key. Duplicate map/GUID entries are suppressed, with a separate variant allowing normal/inverted behavior marker pairs. Missing GUIDs do not collide; invalid/missing GUIDs cannot store behavior history. Ordinary duplicate trails remain suppressed. |
| HeightOffset | Yes | heightOffset: float default 1.5; world marker vertical offset, ignored for sticky image. |
| IconFile | Yes | iconFile: file; marker and sticky PNG inside archive. |
| IconSize | Yes | iconSize: float default 1; world size multiplier, ignored by sticky image. |
| Info | Yes | info: string default empty; text shown inside infoRange, preserving XML `&#xA;` newlines. inforange (also infoRange): float in meters; absent/invalid values fall back to triggerRange, default 2. Independent of copy range. Nearest eligible info marker appears in the Info HUD (initially top-center, 30px from the top); draggable with a saved, rescued screen position. X dismisses until that range is exited. |
| InvertBehavior | Yes | invertBehavior: bool default 0; reverses only the behavior filter. No effect for mode 0; mode 5 with inversion stays hidden because that mode has no implemented activation state. |
| IsHidden | Yes | isHidden: bool default 0; removes category menu row, markers still render. |
| IsSeparator | Yes | isSeparator: bool default 0; category menu heading without toggle. |
| IsWall | Yes | isWall: bool default 0; changes the world trail ribbon width vector to a vertical plane. Map/minimap geometry remains flat; positions and texture phase are unchanged. |
| MapDisplaySize | Yes | mapDisplaySize: float default 20; map icon pixels at 1x before user multiplier. |
| MapID | Yes | MapID: integer; POI map. Trail map comes from .trl header. |
| MapType | No | mapType: list; no context filter. |
| Mount | Yes | mount: comma-separated allow-list of none, jackal, griffon, springer, skimmer, raptor, rollerbeetle, warclaw, skyscale, skiff, siegeturtle. Omitted means unrestricted; explicit empty hides the marker/trail. Names are trimmed and compared without case sensitivity. |
| Occlude | No | occlude: no game depth access. |
| Position | Yes | xpos, ypos, zpos: floats default 0; POI location, also sticky distance. |
| Profession | No | profession: list; no context filter. |
| Race | No | race: list; no context filter. |
| Raid | No | raid: no account progress check. |
| ResetGUID | No | resetGUID: no activation reset. |
| ResetLength | Yes | resetLength: float nonnegative seconds default 0, for behavior 4. Fractional seconds work; zero expires immediately. Nonnegative integers are recommended for TacO portability. |
| Rotate | Yes | rotate: comma-separated X,Y,Z float degrees; rotate-x, rotate-y, rotate-z: float degrees. Fixed world orientation X then Y then Z, combined values override matching axes. Sticky image ignores rotation. |
| ScaleOnMapWithZoom | Yes | scaleOnMapWithZoom: bool default 1; map marker follows zoom. |
| Schedule | No | schedule, schedule-duration: no scheduler. |
| Script | No | script-filter, script-tick, script-focus, script-trigger, script-once: strings; nonempty effective values identify Lua usage. Any .lua file anywhere in the archive also identifies usage. Top-level pack categories show (Contains Lua) with a compatibility tooltip. Scripts do not execute; ordinary categories, markers and trails remain usable. No Lua blocking, red child flags, dependency scanning or hide setting. |
| ShowHide | Yes | show, hide: one category namespace string each, default absent. Activation enables/disables target preferences across loaded packs, without forcing ancestors on. Toggle, then show, then hide is Taquito dispatch order; avoid conflicting actions on the same target. |
| Size | Yes | minSize: float default 5; maxSize: float default 2048; world billboard screen half extent bounds in pixels. |
| Specialization | No | specialization: list; no context filter. |
| Texture | Yes | texture: file; repeated trail image. |
| Tip | Yes | tip-name, tip-description: strings; map/minimap POI hover tooltip and category description hover. Name falls back to category. Explicit empty tip-name suppresses marker tooltip. |
| Toggle | Yes | toggle, toggleCategory: one category namespace string, default absent. Activation reverses target preference. toggleCategory takes precedence even when explicitly empty; display names are not target identities. |
| TrailData | Yes | TrailData: file; .trl archive path, providing map ID and points. |
| TrailScale | Yes | trailScale: float default 1; multiplies 40 game inch world trail width and map/minimap trail width. Independent minimap/full-map trail line scale settings multiply the width; icon scale settings affect only marker icons. Map/minimap trails use the normalized full-mapDisplaySize width baseline, within existing pixel-width bounds. World repeats occupy 1.016 meters; map repeats use the logical ribbon width. Source bitmap dimensions/aspect ratio do not change these repeat lengths. |
| TriggerRange | Yes | triggerRange: float default 2 meters; 3D activation radius for copy, category actions and behavior, plus proximity focus for bounce. Supplies the fallback infoRange when absent/invalid. Inherits from categories. |
| Type | Yes | type: dot-delimited string matching category path. |
| Visibility | Yes | miniMapVisibility, mapVisibility, inGameVisibility: bool default 1; independent view switches. |

Rotate and Tip are Blish HUD additions. Many shared TacO and Blish HUD attributes have the same XML spelling. rotate and rotate-x/y/z are two forms of the same orientation. toggle and toggleCategory name the same category interaction, with toggleCategory taking precedence. Raw retention of an excluded XML string does not imply active support.

## Taquito screen sticky attributes

These Taquito-only values apply to MarkerCategory or POI and inherit like other style values. Check compatibility in other viewers before distributing a pack with unknown attributes.

| XML name | Type; default | Effect |
| --- | --- | --- |
| screenSticky | bool; false | Enables an additional screen copy of iconFile when the player enters stickyDistance. The original remains a normal world marker, subject to stickyHideOriginal. false disables the screen copy. |
| stickyX | float; 0 pixels | Image center offset from screen center; positive moves right. |
| stickyY | float; 0 pixels | Image center offset from screen center; positive moves up. |
| stickyScale | float; 1 | Multiplier of source image pixel dimensions. |
| stickyDistance | float; 350 game inches | Maximum distance from the player to the POI's original coordinates for its screen copy to appear. Includes the boundary; leaving this range hides the screen copy. Independent of fadeNear/fadeFar. Nonnegative values; negatives clamp to 0. |
| stickyHideOriginal | bool; false | true hides the world marker only while its screen copy is actually displayed. false keeps both. Leaving sticky range restores the original subject to its world fade. Map/minimap icons are unaffected. |


MapID, position, alpha, category toggle and inGameVisibility still govern a sticky POI. iconSize, heightOffset and rotate do not affect its screen placement. The nearest eligible image displays. It is moved inside screen edges and scaled down proportionally if needed; it disappears while the full map is open.
