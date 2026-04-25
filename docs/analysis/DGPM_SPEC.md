/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

# DGPM Spec v0.2 (RFC Draft)

DGPM is a no-dependency, cross-platform, safety-first package manager for DGTL.

## 1. Product Objective
- Build a world-class package manager with the smallest safe feature set.
- Ship as one binary, no bundled third-party runtime dependencies.
- Guarantee reproducible installs through lockfiles and content hashes.
- Make compromise detection first-class, not optional.

## 2. Hard Requirements
- No install-time script execution.
- No partial installs visible to users.
- No unsigned package install in strict mode.
- No network trust without signature and hash verification.
- No platform-specific behavior drift for lockfile resolution.

## 3. Threat Model
- Malicious registry mirror serving tampered artifacts.
- Compromised package account publishing backdoored version.
- Local cache tampering.
- Path traversal and archive breakout during extraction.
- Downgrade and replay attacks on index metadata.

## 4. Trust Model
- Root trust set is pinned in binary.
- Registry metadata is signed and versioned with monotonically increasing sequence.
- Package metadata and artifact digest are signed by publisher key.
- Optional transparency checkpoint is signed and verified.

## 5. Command Surface (MVP)
- dgpm init
- dgpm add <name>@<range>
- dgpm remove <name>
- dgpm install
- dgpm update [name]
- dgpm audit
- dgpm pack
- dgpm publish
- dgpm search <text>
- dgpm doctor

## 6. Files and Locations
- Project manifest: dgpm.json
- Lockfile: dgpm.lock.json
- Store root:
- Windows: %LOCALAPPDATA%/dgpm/store
- Linux: ~/.local/share/dgpm/store
- macOS: ~/Library/Application Support/dgpm/store
- Cache root:
- Windows: %LOCALAPPDATA%/dgpm/cache
- Linux: ~/.cache/dgpm
- macOS: ~/Library/Caches/dgpm

## 7. Manifest Schema (dgpm.json)

```json
{
	"schema": "dgpm.manifest.v1",
	"name": "my_app",
	"version": "1.2.0",
	"edition": "2026",
	"registry": "https://registry.dgpm.dev",
	"targets": ["x86_64-windows-msvc", "x86_64-linux-gnu"],
	"dependencies": {
		"dgtl-core": "^2.4.0",
		"dgtl-graph": "~1.9.3"
	},
	"devDependencies": {
		"dgtl-testkit": "^0.8.1"
	},
	"policy": {
		"mode": "strict",
		"allowPrerelease": false,
		"allowUnsigned": false,
		"minSignatureThreshold": 1
	}
}
```

Validation rules:
- name must match regex ^[a-z0-9][a-z0-9._-]{1,63}$
- version must be semver.
- dependencies keys must be unique across dependency groups.
- policy.mode allowed values are strict and balanced.

## 8. Lockfile Schema (dgpm.lock.json)

```json
{
	"schema": "dgpm.lock.v1",
	"generatedAt": "2026-04-14T12:00:00Z",
	"resolver": {
		"algorithm": "deterministic-min-change",
		"registry": "https://registry.dgpm.dev",
		"snapshot": {
			"sequence": 10452,
			"snapshotDigest": "sha256:1f7e...",
			"signedAt": "2026-04-14T11:59:00Z"
		}
	},
	"packages": [
		{
			"name": "dgtl-core",
			"version": "2.4.3",
			"target": "x86_64-windows-msvc",
			"artifact": {
				"digest": "sha256:abc123...",
				"size": 274311,
				"url": "https://registry.dgpm.dev/artifacts/sha256/abc123..."
			},
			"metadataDigest": "sha256:def456...",
			"signatures": [
				{
					"keyId": "publisher:team-dgtl:ed25519:2026q1",
					"sig": "base64:MEUCIQ..."
				}
			],
			"dependencies": {
				"dgtl-graph": "1.9.4"
			}
		}
	]
}
```

Validation rules:
- packages must be lexicographically sorted by name, version, target.
- no duplicate tuple of name and target.
- every dependency entry must resolve to exactly one package in lockfile scope.
- digest format is sha256:<lowercase hex>.

## 9. Package Metadata Schema (registry object)

```json
{
	"schema": "dgpm.package-meta.v1",
	"name": "dgtl-core",
	"version": "2.4.3",
	"target": "x86_64-windows-msvc",
	"artifact": {
		"digest": "sha256:abc123...",
		"size": 274311,
		"compression": "zstd",
		"format": "dgpkg"
	},
	"files": [
		{
			"path": "include/dgtl/core/context.hpp",
			"mode": "0644",
			"digest": "sha256:11aa..."
		}
	],
	"deps": {
		"dgtl-graph": "^1.9.0"
	},
	"publishedAt": "2026-04-14T11:55:00Z",
	"publisher": "team-dgtl"
}
```

## 10. Signature Envelope Schema

```json
{
	"schema": "dgpm.sig.v1",
	"payloadType": "package-metadata",
	"payloadDigest": "sha256:def456...",
	"algorithm": "ed25519",
	"keyId": "publisher:team-dgtl:ed25519:2026q1",
	"issuedAt": "2026-04-14T11:55:02Z",
	"signature": "base64:Q2hhbmdlTWU..."
}
```

Rules:
- payloadDigest is over canonical JSON bytes.
- canonical JSON means UTF-8, sorted keys, no insignificant whitespace, LF line endings.
- keyId must exist in signed key registry.

## 11. Archive Format (dgpkg)
- Container: deterministic tar stream compressed with zstd level 10.
- Entry ordering: lexical by normalized path.
- Path rules:
- must be relative
- must not contain .. segments
- must not contain drive letter prefixes
- must use forward slash in metadata
- Metadata normalization:
- uid and gid set to 0
- mtime set to 0
- owner and group names empty
- permissions normalized to 0644 or 0755 only

## 12. Resolver Contract
- Input: manifest, lockfile optional, signed index snapshot.
- Output: fully pinned graph.
- Determinism tie-break order:
- highest compatible version
- highest metadata sequence
- lexical package name
- lexical target triplet
- update command may change graph.
- install command must refuse graph drift if lockfile exists.

## 13. Install Algorithm (Atomic)
1. Resolve graph from lockfile or produce lockfile.
2. Download missing artifacts to cache.
3. Verify snapshot signature, metadata signatures, and artifact digest.
4. Extract into temp root with breakout checks.
5. Materialize project links from content store.
6. Commit via atomic directory swap.
7. Write install report.

Rollback:
- Any failure before commit removes temp root.
- Failure during commit restores previous root pointer.

## 14. Registry HTTP API (v1)

GET /v1/snapshot/latest
- Returns signed snapshot descriptor with sequence and digest.

GET /v1/index/<sequence>
- Returns canonical index JSON blob.

GET /v1/package/<name>/<version>/<target>
- Returns package metadata JSON and detached signature envelope.

GET /v1/artifact/sha256/<hex>
- Returns artifact bytes.

POST /v1/publish/metadata
- Accepts signed package metadata.

POST /v1/publish/artifact
- Accepts artifact keyed by digest.

POST /v1/publish/commit
- Advances snapshot sequence on successful verification.

## 15. Error Codes
- DGPM1001 invalid manifest schema
- DGPM1002 lockfile mismatch
- DGPM1003 dependency conflict
- DGPM1004 signature verification failed
- DGPM1005 artifact digest mismatch
- DGPM1006 replayed or stale snapshot
- DGPM1007 unsafe archive path
- DGPM1008 atomic commit failed
- DGPM1009 policy blocked install

## 16. Audit Output Contract
- Command: dgpm audit --json
- Output fields:
- snapshot sequence and digest
- package count and verified signature count
- vulnerability summary by severity
- unsigned package count
- non-reproducible archive count
- final status pass or fail

## 17. Performance Budgets
- Warm install from lockfile and local cache: median under 250 ms for 10 packages.
- Cold install over network for 10 packages: median under 2.0 s on broadband.
- Peak RSS during install: under 64 MB for 100 MB total artifact set.
- Binary size goal:
- core profile under 6 MB stripped
- net profile under 10 MB stripped

## 18. No-Dependency Implementation Profile
- Language: C++ standard library only for core.
- Crypto and TLS:
- Windows via CNG and SChannel
- macOS via Security.framework and URLSession stack
- Linux via pluggable adapter that uses system-available crypto and TLS provider
- JSON handling: internal parser and canonical serializer in repository.
- Compression: internal zstd-compatible implementation or uncompressed fallback in core profile.

## 19. Compatibility and Evolution
- manifest schema versioned independently from lockfile schema.
- lockfile readers must support one major version back.
- registry API supports explicit version in URL path.
- deprecated fields require two minor versions of overlap.

## 20. MVP Delivery Plan
1. Phase 1: local core
2. Phase 2: lockfile and deterministic resolver
3. Phase 3: signed metadata verification and digest validation
4. Phase 4: registry read path and offline mirror mode
5. Phase 5: publish path and audit JSON output
6. Phase 6: transparency checkpoint verification

## 21. Definition of Done for v1.0
- Reproducible install test passes across Windows, Linux, macOS.
- Tamper tests fail closed for all modified metadata and artifacts.
- Replay test for stale snapshot is blocked.
- Archive breakout tests are blocked.
- Crash recovery test preserves previous install root.
