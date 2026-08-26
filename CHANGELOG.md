# Changelog

All notable changes to Nobelisk Plus, since the 1.0 release.

## 1.0.3 (unreleased)

### Added
- Optional "Automatically Reload" toggle for the Nobelisk Detonator and the
  Rebar Gun. Works around the Nobelisk Detonator only loading one round per
  reload pass, and around none of the obvious native signals firing usefully
  for either weapon, by polling ammo count instead of trying to catch a
  reload-trigger event directly.
- Optional magazine capacity overrides: a general capacity fallback plus a
  per-ammo-type override for every Nobelisk and Rebar variant. 0 means
  vanilla/unset throughout, so the feature is fully opt-in.

## 1.0.2

### Added
- Pulse Rebar research node grafted directly onto the base game's Quartz research
  tree, positioned right after the Pulse Nobelisk node, replacing the earlier
  standalone custom research tree.
- Impact VFX for the Pulse Rebar: reuses whatever particle system the Pulse
  Nobelisk uses on impact, discovered automatically at startup.
- Ammo icon for the Pulse Rebar.
- Visible mesh for the Pulse Rebar in flight (`RebarMesh`), created natively.
  Previously invisible: reparenting the round onto the mod's own projectile class
  had detached it from the shared Blueprint base that used to supply the mesh.
- Brief stun on creatures hit by the Pulse Rebar's shockwave, so their AI
  movement doesn't immediately overwrite the launch and cancel it out. Does not
  apply to the player.
- Projectile Speed Multiplier setting for the Pulse Rebar, for raising its
  muzzle velocity if the round feels slow to reach what you aimed at. Not
  present on the Pulse Nobelisk, since it's thrown rather than fired.

### Changed
- All shockwave multiplier defaults changed from 2.0x to 1.0x (vanilla
  behaviour), so the mod is opt-in-per-setting rather than doubling everything
  out of the box.
- Config settings now apply live, in-session, as intended — cleared
  `bRequiresWorldReload`, which had been defaulting to true and silently
  limiting edits to the main menu.

### Fixed
- ficsit.app rejected the first upload attempt with a backend "mod not found"
  error while creating the version. Per ficsit.app support, this was caused by
  listing `Niagara` — needed at compile time via `NobeliskPlus.Build.cs`, not
  as a plugin-level dependency — in the `.uplugin`'s `Plugins` array; removed
  it. Confirmed the mod still builds clean without that entry (UBT only emits
  a warning, not an error).

## 1.0

Initial tracked release. Pulse Nobelisk shockwave radius/impulse/force
multipliers, plus a Pulse Rebar with its own tweakable shockwave (via a
standalone research tree). See [README.md](README.md) for full feature details.
