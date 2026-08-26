# Nobelisk Plus

A [Satisfactory](https://www.satisfactorygame.com/) mod that makes the Pulse Nobelisk's
shockwave bigger and stronger — larger radius, harder push — without increasing its
damage, adds a Pulse Rebar with its own tweakable shockwave, and can make the Nobelisk
Detonator and Rebar Gun reload themselves and hold more ammo.

Built with [SML](https://ficsit.app/mod/SML) (Satisfactory Mod Loader).

## Features

- Bigger, stronger Pulse Nobelisk shockwave (radius and push only — damage unchanged)
- Pulse Rebar ammo with its own shockwave, researchable from the Quartz tree right after
  the Pulse Nobelisk
- Optional auto-reload for the Nobelisk Detonator and the Rebar Gun
- Optional magazine capacity overrides, per ammo type, for both weapons
- Every setting is configurable in-game from the mod settings menu and applies live,
  no restart needed. All defaults are vanilla behaviour — the mod is entirely opt-in.

## Configuration

### Shockwaves

Per weapon (Pulse Nobelisk / Pulse Rebar), all multipliers default to 1.0x (vanilla):

| Setting | Description |
| --- | --- |
| Shockwave Radius Multiplier | How much bigger the shockwave radius is |
| Shockwave Impulse Multiplier | How much stronger the one-shot push is |
| Shockwave Force Multiplier | How much stronger the continuous push is |
| Projectile Speed Multiplier (Pulse Rebar only) | How much faster the round travels after being fired |

### Nobelisk Detonator / Rebar Gun

Per weapon:

| Setting | Description | Default |
| --- | --- | --- |
| Automatically Reload | Reloads the weapon itself once its magazine empties, instead of requiring a manual reload | Off |
| General Capacity | Magazine size for any ammo type without its own override below. 0 = vanilla | 0 |
| Per-ammo-type Capacity | Overrides one ammo type's magazine size. 0 = use General Capacity (or vanilla if that's also 0) | 0 |

## Installation

Install via the [Satisfactory Mod Manager](https://ficsit.app/) or download from
[ficsit.app](https://ficsit.app/mod/NobeliskPlus).

## Building from source

1. Clone into `<SML>/Mods/GameFeatures/NobeliskPlus` (or symlink/junction it there).
2. Generate project files and build the `FactoryEditor` target as you would for SML.
3. Package with Alpakit.

## License

MIT — see [LICENSE](LICENSE).
