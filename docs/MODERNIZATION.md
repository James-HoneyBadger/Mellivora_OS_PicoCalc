# Modernization Program

This document defines the operating model for the Mellivora PicoCalc modernization effort.

## Scope

The modernization program targets:

- architecture modularization
- professional UX consistency
- performance and responsiveness
- reliability and data integrity
- new feature delivery
- sample program quality
- documentation quality and release discipline

## Release Train

- monthly canary release (`vX.Y.Z-canary.N`)
- quarterly stable release (`vX.Y.Z`)
- urgent patch releases as needed (`vX.Y.Z+1`)

Every stable release must include migration notes and a release scorecard.

## Change Governance

All non-trivial changes require a short design note in pull request description covering:

1. problem statement
2. proposed approach
3. risk and rollback
4. migration impact
5. test and verification plan

## Quality Gates

A change is not release-ready unless it passes:

1. build matrix (`picocalc`, `pico2`, `pico2w`)
2. baseline regression checks (no unapproved degradation)
3. docs consistency checks
4. migration policy checks for user-facing changes

## Deprecation Rules

- commands or file formats marked for removal must emit warnings for at least one stable cycle
- deprecations must have a documented replacement
- removals require a migration note in `docs/CHANGELOG.md`

## Ownership

Subsystems should have an explicit owner in PR labels:

- shell-core
- apps
- storage
- display-input
- network
- docs
- build-release
