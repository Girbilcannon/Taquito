# Taquito XML Attributes (as of build 0.0.1.18)
## Supported Attributes

| Guide group | Support | XML names, types and Taquito behavior |
| --- | --- | --- |
| Achievement | No | achievementId, achievementBit: integer; no account progress check. |
| Alpha | Yes | alpha: float default 1; clamped opacity 0–1, multiplied by distance fade. |
| AnimSpeed | Yes | animSpeed: float default 1; trail texture scroll, 0 stops it. |
| AutoTrigger | Yes | autoTrigger: bool; no activation state. (currently only for copy) |
| Behavior | No | behavior: integer; no activation state. |
| Bounce | No | bounce: no animation. |
| CanFade | No | canFade: bool; no item bubble exemption. |
| Color | Yes | color: color default #FFFFFF; tints image, optional last two hex digits set opacity. |
| Copy | Yes | copy, copy-message: no clipboard action. |
| Cull | No | cull: no culling mode. |
| DefaultToggle | No | defaultToggle: bool; user settings govern toggles, otherwise enabled. |
| DisplayName | Yes | DisplayName: string; category menu label, falls back to name. |
| Fade | Yes | fadeNear, fadeFar: float default -1; game inches, opacity fades from near to far. Either negative disables distance fading. Also applies to sticky images. |
| Festival | No | festival: no event feed. |
| GUID | Yes | GUID: string (usually base64); unique entry ID per map across loaded packs. The first valid marker or trail with a given GUID renders; later duplicates on that map are skipped with a warning. Missing GUIDs do not collide. No activation history. |
| HeightOffset | Yes | heightOffset: float default 1.5; world marker vertical offset, ignored for sticky image. |
| IconFile | Yes | iconFile: file; marker and sticky PNG inside archive. |
| IconSize | Yes | iconSize: float default 1; world size multiplier, ignored by sticky image. |
| Info | Yes | info, infoRange: no interaction text. |
| InvertBehavior | No | invertBehavior: no activation state. |
| IsHidden | Yes | isHidden: bool default 0; removes category menu row, markers still render. |
| IsSeparator | Yes | isSeparator: bool default 0; category menu heading without toggle. |
| IsWall | No | isWall: bool; trails face upward. |
| MapDisplaySize | Yes | mapDisplaySize: float default 20; map icon pixels at 1x before user multiplier. |
| MapID | Yes | MapID: integer; POI map. Trail map comes from .trl header. |
| MapType | No | mapType: list; no context filter. |
| Mount | No | mount: list; no context filter. |
| Occlude | No | occlude: no game depth access. |
| Position | Yes | xpos, ypos, zpos: floats default 0; POI location, also sticky distance. |
| Profession | No | profession: list; no context filter. |
| Race | No | race: list; no context filter. |
| Raid | No | raid: no account progress check. |
| ResetGUID | No | resetGUID: no activation reset. |
| ResetLength | No | resetLength: no timer. |
| Rotate | Yes | rotate: comma-separated X,Y,Z float degrees; rotate-x, rotate-y, rotate-z: float degrees. Fixed world orientation X then Y then Z, combined values override matching axes. Sticky image ignores rotation. |
| ScaleOnMapWithZoom | Yes | scaleOnMapWithZoom: bool default 1; map marker follows zoom. |
| Schedule | No | schedule, schedule-duration: no scheduler. |
| Script | No | script-* fields: no Lua execution. |
| ShowHide | No | show, hide: no category interaction. |
| Size | Yes | minSize: float default 5; maxSize: float default 2048; world billboard screen half extent bounds in pixels. |
| Specialization | No | specialization: list; no context filter. |
| Texture | Yes | texture: file; repeated trail image. |
| Tip | Yes | tip-name, tip-description: strings; map/minimap POI hover tooltip and category description hover. Name falls back to category. Explicit empty tip-name suppresses marker tooltip. |
| Toggle | No | toggle, toggleCategory: no interaction; manual category checkboxes still work. |
| TrailData | Yes | TrailData: file; .trl archive path, providing map ID and points. |
| TrailScale | Yes | trailScale: float default 1; multiplies 40 game inch world trail width. |
| TriggerRange | Yes | triggerRange: no activation. (currently only for copy) |
| Type | Yes | type: dot-delimited string matching category path. |
| Visibility | Yes | miniMapVisibility, mapVisibility, inGameVisibility: bool default 1; independent view switches. |

## Taquito screen sticky attributes

These Taquito-only attributes apply to MarkerCategory or POI and inherit like other style values. Check compatibility in other viewers before distributing a pack with unknown attributes.

| XML name | Type; default | Effect |
| --- | --- | --- |
| screenSticky | bool; true | true draws iconFile in fixed screen space. false keeps normal world placement and size, ignoring sticky attributes. |
| stickyHideOriginal | bool; true | true hides the original world billboard marker. false keeps the original marker while still showing the sticky. |
| stickyX | float; 0 pixels | Image center offset from screen center; positive moves right. |
| stickyY | float; 0 pixels | Image center offset from screen center; positive moves up. |
| stickyScale | float; 1 | Multiplier of source image pixel dimensions. |
| stickyDistance | float; 100 | The unit distance at which the sticky attributes take over. |

MapID, position, alpha, category toggle and inGameVisibility still govern a sticky POI. iconSize, heightOffset and rotate do not affect its screen placement. The nearest eligible image displays. It is moved inside screen edges and scaled down proportionally if needed; it disappears while the full map is open.
