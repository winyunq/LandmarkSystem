# Scene Unit Placement Protocol

## Implemented Contract

LandmarkSystem has two placement paths.

### 1. Map Data Placement

Use JSON when the map contains many single placement points.

Each `FLandmarkInstanceData` entry represents one map point:

- `Type`: unit/building/city type key.
- `X`, `Y`: map position.
- `Team`: owner team. Defaults to `0`.
- `Value`: victory points or gameplay value. Defaults by type when omitted.

At world begin play, `ULandmarkSubsystem` loads the current map JSON and groups entries by `(Type, Team)`. Each group is spawned through Mass Battle with the matching `MassConfig` from `ULandmarkSettings`.

This path is intended for cities, buildings, resource points, neutral objectives, and other dense map-authored single entities.

### 2. MassUnitInHere Placement

Use `MassUnitInHere` when a level designer wants one placed actor to spawn many units.

The actor owns:

- `AgentConfig`
- `Quantity`
- `Team`
- `HealthOverride`
- `SpawnSpacing`
- health bar visibility overrides

At begin play, it spawns a rectangular group at its actor transform, then destroys itself.

This path is intended for initial armies, local defensive groups, scenario test groups, and hand-authored battle setups.

## Pending Protocol Decisions

These should be confirmed before the next implementation pass.

### Type Config Rename

Current settings still use `FCityLevelConfig` and `CityLevelConfigs`.

Recommended rename:

- `FCityLevelConfig` -> `FSceneUnitTypeConfig`
- `CityLevelConfigs` -> `SceneUnitTypeConfigs`

Reason: the system is no longer city-only. It now covers cities, buildings, resource points, and initial units.

### JSON File Naming

Current loader uses:

- `Landmarks_<MapName>_ZH.json`
- fallback `Landmarks_<MapName>.json`

Recommended next step:

- keep old names for compatibility
- add `SceneUnits_<MapName>.json` as the preferred new file

### Team Color Source

Recommended rule:

- JSON stores only `Team`.
- Per-map config stores team display names and colors.
- Minimap, FogOfWar, LandmarkSystem, and Mass UI resolve color from that shared per-map config.

### Building Initialization

Recommended rule:

- Buildings use the same placement structure as cities.
- City-specific values such as victory points remain optional fields.
- Building-specific behavior should come from `Type` config or Mass fragments, not hardcoded city branches.

### Proxy Asset Migration

If existing maps contain `AMassProxyActor` instances, Unreal may need a redirect from the old class path to `AMassUnitInHere`.

Recommended redirect if needed:

```ini
+ActiveClassRedirects=(OldClassName="/Script/Winyunq.MassProxyActor",NewClassName="/Script/LandmarkSystem.MassUnitInHere")
```

Only add this after confirming existing maps actually contain old proxy actors.
