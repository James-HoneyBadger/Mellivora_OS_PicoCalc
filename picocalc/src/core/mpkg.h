#pragma once
/*
 * core/mpkg.h — package index, install state, and planning logic.
 *
 * Portable half of the `mpkg` command: no network, filesystem, or display
 * access. It parses the repository index and the local install state, then
 * decides what an install or upgrade should do.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/json.h"
#include "core/sha256.h"

#define MPKG_MAX_PACKAGES 16
#define MPKG_NAME_MAX 32
#define MPKG_VERSION_MAX 24
#define MPKG_FILE_MAX 48
#define MPKG_URL_MAX 192
#define MPKG_DESC_MAX 64
#define MPKG_PATH_MAX 80
#define MPKG_SHA_MAX SHA256_HEX_SIZE

typedef enum {
    MPKG_KIND_MAPP = 0,
    MPKG_KIND_THEME,
    MPKG_KIND_SCRIPT,
    MPKG_KIND_UNKNOWN,
} mpkg_kind_t;

typedef enum {
    MPKG_ACTION_UNKNOWN = 0,
    MPKG_ACTION_INSTALL,
    MPKG_ACTION_UPGRADE,
    MPKG_ACTION_CURRENT,
    MPKG_ACTION_DOWNGRADE,
} mpkg_action_t;

typedef struct {
    char name[MPKG_NAME_MAX];
    char version[MPKG_VERSION_MAX];
    char file[MPKG_FILE_MAX];
    char url[MPKG_URL_MAX];
    char sha256[MPKG_SHA_MAX];
    char description[MPKG_DESC_MAX];
    mpkg_kind_t kind;
    uint32_t size;
} mpkg_package_t;

typedef struct {
    char name[MPKG_NAME_MAX + 16];
    mpkg_package_t packages[MPKG_MAX_PACKAGES];
    size_t count;
    size_t rejected;
    int schema;
} mpkg_index_t;

typedef struct {
    char name[MPKG_NAME_MAX];
    char version[MPKG_VERSION_MAX];
    char path[MPKG_PATH_MAX];
    char sha256[MPKG_SHA_MAX];
    mpkg_kind_t kind;
} mpkg_installed_t;

typedef struct {
    mpkg_installed_t entries[MPKG_MAX_PACKAGES];
    size_t count;
} mpkg_state_t;

mpkg_kind_t mpkg_kind_parse(const char *text);
const char *mpkg_kind_name(mpkg_kind_t kind);
const char *mpkg_kind_dir(mpkg_kind_t kind);

/* Numeric dotted version comparison. Returns false for unparsable input. */
bool mpkg_version_compare(const char *a, const char *b, int *ordering);

/* Index loading. `arena` backs the JSON parse; `out` survives it. */
bool mpkg_index_load(mpkg_index_t *out, json_arena_t *arena, const char *text, size_t len);
bool mpkg_state_load(mpkg_state_t *out, json_arena_t *arena, const char *text, size_t len);
int mpkg_state_serialize(const mpkg_state_t *state, char *out, size_t cap);

const mpkg_package_t *mpkg_index_find(const mpkg_index_t *index, const char *name);
const mpkg_installed_t *mpkg_state_find(const mpkg_state_t *state, const char *name);

bool mpkg_state_record(mpkg_state_t *state, const mpkg_package_t *pkg, const char *path);
bool mpkg_state_remove(mpkg_state_t *state, const char *name);

/* Resolve the on-card destination for a package ("/apps/name.mapp"). */
bool mpkg_package_path(const mpkg_package_t *pkg, char *out, size_t cap);

mpkg_action_t mpkg_plan(const mpkg_index_t *index, const mpkg_state_t *state, const char *name,
                        const mpkg_package_t **package);
const char *mpkg_action_name(mpkg_action_t action);
