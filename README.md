# PhilosophyVR_544

Unreal Engine 5.4.4 VR Prototype

---

## Current Development Progress (-20260225)

### Completed

### Core Interaction Systems

- [x] Door whitebox interaction prototype  
- [x] Basic grab system implemented   
- [x] Collision profile debugging  
- [x] Git cleanup for Unreal generated files  

---

### Interaction Experiments

- [ ] Paint interaction on window  
- [ ] Piano interaction prototype  
- [ ] Evaluate plugin-based interaction system  

---

## Interaction Systems Status

### Door Interaction

Current version uses:

- Overlap trigger
- Grip input
- Relative rotation
- Angle clamp
- Physics blocking

Previous approach using continuous delta yaw calculation was removed due to instability.

Timeline-based door animation is under consideration as a cleaner alternative to tick-driven rotation.

Attempted migration to VR Expansion Plugin system but integration was not successful at this stage.  
Decision: continue using the tutorial-based interaction framework (tut01) for stability and clarity.

---

## Git Stabilization

- Fixed nested Unreal project folder `.gitignore` mismatch
- Added explicit ignore rules for:
  - Binaries
  - Intermediate
  - DerivedDataCache
  - Saved
  - AssetRegistryCache
  - Autosave files
- Removed tracked generated files via `git rm --cached`
- Cleaned Unreal build artifacts from repository
