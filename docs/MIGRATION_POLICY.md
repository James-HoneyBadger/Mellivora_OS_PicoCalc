# Migration Policy

This policy defines how command, behavior, and data-format changes are introduced.

## Compatibility Modes

- `legacy`: old behavior retained via compatibility shims
- `current`: default behavior for new releases

Compatibility shims may be removed after one stable release cycle with prior warning.

## Command Migration Rules

1. renamed commands must keep the old name as an alias for at least one stable release
2. deprecated commands must print a replacement hint
3. removed commands must be listed in `docs/CHANGELOG.md`

## Data Format Migration Rules

1. persistent files should include versioned headers where practical
2. readers must tolerate missing optional fields
3. migrations should be idempotent and safe to retry
4. conversion utilities should preserve original files when feasible

## Required Migration Note Template

Every breaking or behavior-changing PR should document:

- old behavior
- new behavior
- upgrade steps
- rollback steps
- data compatibility notes

## Verification Requirements

For each release:

1. run migration smoke checks against representative legacy files
2. confirm deprecated aliases still function as documented
3. confirm warning messages are clear and actionable
