# Changelog

All notable changes to Nobelisk Plus, since the 1.0 release.

## Unreleased

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
- Added the `Niagara` module/plugin dependency, needed for the impact VFX work.

### Fixed
- N/A

## 1.0

Initial tracked release. Pulse Nobelisk shockwave radius/impulse/force
multipliers, plus a Pulse Rebar with its own tweakable shockwave (via a
standalone research tree). See [README.md](README.md) for full feature details.
