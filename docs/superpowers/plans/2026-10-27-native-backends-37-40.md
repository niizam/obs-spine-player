# Native 3.7 and 4.0 Backends (stage 4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Native draws Spine 3.7, 4.0 and 4.1 skeletons from one plugin module.

**Architecture:**
- One shared 4.x adapter compiled per version.
- A separate 3.7 adapter.
- Per-backend blend tables.
- The backend is carried with each load result.

**Tech Stack:** C11, spine-c 3.7, 4.0 and 4.1 (vendored, prefixed), libobs, CTest, Node (references).

**Spec:** `docs/superpowers/specs/2026-10-27-native-backends-37-40-design.md`

## Global Constraints

- **Branch:** `feature/native-renderer`. No merge and no PR until stage 5.
- **Vendored code:** never edit vendored runtimes. Pinned commits: 3.7 `9639bcc81722d7178fd9d1cdc1a3d55a4c91f989`, 4.0 `425ce416bb218b28caeec47b317aa57cd7140375`.
- **Exported names:** every backend library exports only prefixed names plus its `spine_backend_<ver>`.
- **Parity:** match the bundled web runtime for each version. That covers blend factors, framing, dark colour, and which slots are skipped.
- **Allocation:** zero-byte allocations are rounded up in every backend, and each backend has a `shutdown` that disposes its statics.

## Review Focus

1. **A stale load result from another version being freed through the wrong vtable.** Expected: each result is freed with its own backend. Covered in Task 5, by carrying the backend in `load_result` plus an assert.
2. **The 3.7 overlay sharing timelines with `spAnimation_dispose`.** Expected: no double free. Covered by the backend-test leak and overlay tests for 3.7.
3. **Duplicate symbols across the three runtimes.** Expected: the link succeeds, and every prefix check is green on MSVC and GCC.
4. **3.7 rigs without bones' `active` flag, and rigs with clipping.** Expected: drawn correctly. Covered by parity with the 3.7 reference.
5. **The runtime override set to 3.7 or 4.0 while the asset is another version.** Expected: a clear load error, not a crash. spine-c 3.7 and 4.0 don't check versions, so the binary pre-check must enforce the format.

### Task 1: Vendor 3.7 and 4.0, fixtures, prefixed libraries

- [ ] Copy each `spine-c/spine-c/{include,src}` and the LICENSE into `third_party/spine-c-3.7` and `third_party/spine-c-4.0`, each with a `VERSION` file. Copy Spineboy's `-pro.skel`, `-pro.json`, `-pma.atlas` and `-pma.png` into `tests/fixtures/spine37` and `spine40`.
- [ ] Add `spine_add_raw_runtime(3.7)` and `spine_add_raw_runtime(4.0)`, dump their symbols, and generate `src/native/spine37-prefix.h` and `spine40-prefix.h`.
- [ ] Update THIRD_PARTY_NOTICES. Commit: `chore: vendor spine-c 3.7 and 4.0 with Spineboy fixtures`.

### Task 2: References and the shared backend test

- [ ] `tools/generate-native-reference.js <3.7|4.0|4.1>` writes `tests/fixtures/spine<ver>/reference.h`. Besides the existing values, it adds `REF_SLOT_COUNT`, `REF_BONE_COUNT`, `REF_MOUTH_SLOT` (index of `mouth`), and `REF_MOUTH_BONE_NAME` and `REF_MOUTH_PARENT_NAME`.
- [ ] Rename `tests/backend-41-test.c` to `tests/backend-test.c`, parameterised by `TEST_FIXTURE_VERSION_DIR`, `BACKEND_ENTRY` and the reference header. Replace hard-coded Spineboy 4.1 numbers with `REF_*`. Animation-specific checks that depend on `run` or `walk` existing stay, provided the reference says so.
- [ ] CMake: one test executable per version. The 3.7 and 4.0 tests are expected to fail until Tasks 3 and 4 (their backends are stubs).

### Task 3: Shared 4.x adapter and blend tables

- [ ] Rename `backend-41.c` to `backend-4x.c` and add the version `#if`s. `spine_add_backend` passes `SPINE_BACKEND_VERSION`.
- [ ] Add `blend_factors` to the interface, give 4.x the spine-ts 4.x table, and make the renderer use the table.
- [ ] The 4.0 and 4.1 backend tests pass, along with all prefix checks. Commit: `feat(native): Spine 4.0 backend from the shared 4.x adapter; per-backend blend factors`.

### Task 4: 3.7 adapter

- [ ] `src/native/backend-37.c`: the full interface for the 3.7 API, as in the spec, with the spine-ts 3.7 blend table and 3.7 framing.
- [ ] The 3.7 backend test passes, along with the prefix check. Commit: `feat(native): Spine 3.7 backend`.

### Task 5: Renderer and source dispatch

- [ ] `spine_backend_for_family`; `load_result.backend`; free and adopt with the right backend; set allocators on all backends; shut down all of them at unload; only unknown versions fall back to Browser.
- [ ] Update the wording in the log, locale and README.
- [ ] Commit: `feat(native): draw Spine 3.7 and 4.0 natively`.

### Task 6: Verification and docs

- [ ] In OBS:
  - Spineboy 4.0;
  - Curian 3.7 with auto yap (overlay log, tone toggles);
  - MekamiShifty 4.1 as a regression check;
  - switching between assets of different versions on one source;
  - 0 leaks.
- [ ] GCC check with all three backends, and `nm` prefix checks for each.
- [ ] Update `docs/architecture.md`, the README and the spec erratum if needed. Commit.
