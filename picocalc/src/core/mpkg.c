/*
 * core/mpkg.c — repository index, install state, and planning.
 */

#include "core/mpkg.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static bool _copy(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return false;
    if (!src) src = "";
    size_t len = strlen(src);
    if (len >= cap) {
        memcpy(dst, src, cap - 1);
        dst[cap - 1] = '\0';
        return false;
    }
    memcpy(dst, src, len + 1);
    return true;
}

mpkg_kind_t mpkg_kind_parse(const char *text) {
    if (!text) return MPKG_KIND_UNKNOWN;
    if (!strcmp(text, "mapp") || !strcmp(text, "native") || !strcmp(text, "bytecode"))
        return MPKG_KIND_MAPP;
    if (!strcmp(text, "theme")) return MPKG_KIND_THEME;
    if (!strcmp(text, "script") || !strcmp(text, "basic") || !strcmp(text, "forth"))
        return MPKG_KIND_SCRIPT;
    return MPKG_KIND_UNKNOWN;
}

const char *mpkg_kind_name(mpkg_kind_t kind) {
    switch (kind) {
    case MPKG_KIND_MAPP:
        return "mapp";
    case MPKG_KIND_THEME:
        return "theme";
    case MPKG_KIND_SCRIPT:
        return "script";
    default:
        return "unknown";
    }
}

const char *mpkg_kind_dir(mpkg_kind_t kind) {
    switch (kind) {
    case MPKG_KIND_MAPP:
        return "/apps";
    case MPKG_KIND_THEME:
        return "/themes";
    case MPKG_KIND_SCRIPT:
        return "/bin";
    default:
        return "";
    }
}

static bool _take_number(const char **cursor, unsigned long *value) {
    const char *p = *cursor;
    unsigned long acc = 0;
    bool found = false;
    while (*p >= '0' && *p <= '9') {
        if (acc < 1000000ul) acc = acc * 10ul + (unsigned long)(*p - '0');
        p++;
        found = true;
    }
    *cursor = p;
    *value = acc;
    return found;
}

bool mpkg_version_compare(const char *a, const char *b, int *ordering) {
    if (!a) a = "";
    if (!b) b = "";
    if (*a == 'v' || *a == 'V') a++;
    if (*b == 'v' || *b == 'V') b++;

    const char *pa = a;
    const char *pb = b;
    for (int component = 0; component < 4; component++) {
        unsigned long na = 0;
        unsigned long nb = 0;
        bool da = _take_number(&pa, &na);
        bool db = _take_number(&pb, &nb);
        if (!da && !db) break;
        if (na != nb) {
            if (ordering) *ordering = na < nb ? -1 : 1;
            return true;
        }
        if (*pa == '.') pa++;
        if (*pb == '.') pb++;
        if (!*pa && !*pb) break;
    }

    /* Equal numeric core: a suffix (e.g. "-rc1") sorts before the release. */
    bool suffix_a = *pa != '\0';
    bool suffix_b = *pb != '\0';
    int ordering_value = 0;
    if (suffix_a != suffix_b) ordering_value = suffix_a ? -1 : 1;
    else if (suffix_a && suffix_b)
        ordering_value = strcmp(pa, pb);
    if (ordering) *ordering = ordering_value < 0 ? -1 : (ordering_value > 0 ? 1 : 0);
    return true;
}

static bool _valid_file_name(const char *name) {
    if (!name || !*name) return false;
    if (strchr(name, '/') || strchr(name, '\\')) return false;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return false;
    return true;
}

static bool _package_from_json(const json_value_t *node, mpkg_package_t *out) {
    if (!node || json_type(node) != JSON_OBJECT) return false;
    memset(out, 0, sizeof *out);

    const char *name = json_string(json_get(node, "name"));
    const char *version = json_string(json_get(node, "version"));
    const char *kind = json_string(json_get(node, "kind"));
    const char *file = json_string(json_get(node, "file"));
    const char *url = json_string(json_get(node, "url"));
    const char *sha = json_string(json_get(node, "sha256"));
    const char *desc = json_string(json_get(node, "description"));

    if (!name || !*name || strlen(name) >= MPKG_NAME_MAX) return false;
    if (!version || !*version || strlen(version) >= MPKG_VERSION_MAX) return false;
    if (!_valid_file_name(file) || strlen(file) >= MPKG_FILE_MAX) return false;
    if (!url || !*url || strlen(url) >= MPKG_URL_MAX) return false;
    if (!sha || strlen(sha) != SHA256_HEX_LEN) return false;
    uint8_t digest[SHA256_DIGEST_SIZE];
    if (!sha256_parse_hex(sha, digest)) return false;

    mpkg_kind_t parsed = mpkg_kind_parse(kind);
    if (parsed == MPKG_KIND_UNKNOWN) return false;

    _copy(out->name, sizeof out->name, name);
    _copy(out->version, sizeof out->version, version);
    _copy(out->file, sizeof out->file, file);
    _copy(out->url, sizeof out->url, url);
    _copy(out->sha256, sizeof out->sha256, sha);
    _copy(out->description, sizeof out->description, desc ? desc : "");
    out->kind = parsed;
    long long size = json_int_or(json_get(node, "size"), 0);
    out->size = size > 0 ? (uint32_t)size : 0;
    return true;
}

bool mpkg_index_load(mpkg_index_t *out, json_arena_t *arena, const char *text, size_t len) {
    if (!out || !arena || !text) return false;
    memset(out, 0, sizeof *out);

    json_value_t *root = json_parse(arena, text, len);
    if (!root || json_type(root) != JSON_OBJECT) return false;

    out->schema = (int)json_int_or(json_get(root, "schema"), 0);
    if (out->schema != 1) return false;

    const char *repo = json_string(json_get(root, "name"));
    _copy(out->name, sizeof out->name, repo ? repo : "repository");

    const json_value_t *packages = json_get(root, "packages");
    if (!packages || json_type(packages) != JSON_ARRAY) return false;

    size_t total = json_count(packages);
    for (size_t i = 0; i < total; i++) {
        mpkg_package_t parsed;
        if (!_package_from_json(json_at(packages, i), &parsed)) {
            out->rejected++;
            continue;
        }
        if (out->count >= MPKG_MAX_PACKAGES) {
            out->rejected++;
            continue;
        }
        out->packages[out->count++] = parsed;
    }
    return true;
}

bool mpkg_state_load(mpkg_state_t *out, json_arena_t *arena, const char *text, size_t len) {
    if (!out) return false;
    memset(out, 0, sizeof *out);
    if (!arena || !text || len == 0) return true; /* empty state is valid */

    json_value_t *root = json_parse(arena, text, len);
    if (!root || json_type(root) != JSON_OBJECT) return false;
    if ((int)json_int_or(json_get(root, "schema"), 0) != 1) return false;

    const json_value_t *list = json_get(root, "installed");
    if (!list || json_type(list) != JSON_ARRAY) return true;

    size_t total = json_count(list);
    for (size_t i = 0; i < total && out->count < MPKG_MAX_PACKAGES; i++) {
        const json_value_t *node = json_at(list, i);
        if (!node || json_type(node) != JSON_OBJECT) continue;
        const char *name = json_string(json_get(node, "name"));
        const char *version = json_string(json_get(node, "version"));
        const char *path = json_string(json_get(node, "path"));
        const char *sha = json_string(json_get(node, "sha256"));
        const char *kind = json_string(json_get(node, "kind"));
        if (!name || !*name || !path || !*path) continue;

        mpkg_installed_t *entry = &out->entries[out->count++];
        memset(entry, 0, sizeof *entry);
        _copy(entry->name, sizeof entry->name, name);
        _copy(entry->version, sizeof entry->version, version ? version : "0");
        _copy(entry->path, sizeof entry->path, path);
        _copy(entry->sha256, sizeof entry->sha256, sha ? sha : "");
        entry->kind = mpkg_kind_parse(kind);
    }
    return true;
}

static int _append(char *out, size_t cap, size_t used, const char *fmt, ...) {
    if (!out || used >= cap) return -1;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + used, cap - used, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - used) return -1;
    return (int)(used + (size_t)n);
}

int mpkg_state_serialize(const mpkg_state_t *state, char *out, size_t cap) {
    if (!out || cap == 0) return -1;
    size_t used = 0;
    int rc = _append(out, cap, used, "{\n  \"schema\": 1,\n  \"installed\": [");
    if (rc < 0) return -1;
    used = (size_t)rc;

    size_t total = state ? state->count : 0;
    /* Only include entries with a usable path so a corrupt slot is ignored. */
    bool first = true;
    for (size_t i = 0; i < total; i++) {
        const mpkg_installed_t *entry = &state->entries[i];
        if (!entry->name[0] || !entry->path[0]) continue;
        rc = _append(out, cap, used,
                     "%s\n    {\"name\": \"%s\", \"version\": \"%s\", "
                     "\"kind\": \"%s\", \"path\": \"%s\", \"sha256\": \"%s\"}",
                     first ? "" : ",", entry->name, entry->version, mpkg_kind_name(entry->kind),
                     entry->path, entry->sha256);
        if (rc < 0) return -1;
        used = (size_t)rc;
        first = false;
    }
    rc = _append(out, cap, used, "%s]\n}\n", first ? "" : "\n  ");
    if (rc < 0) return -1;
    return rc;
}

const mpkg_package_t *mpkg_index_find(const mpkg_index_t *index, const char *name) {
    if (!index || !name) return NULL;
    for (size_t i = 0; i < index->count; i++) {
        if (!strcmp(index->packages[i].name, name)) return &index->packages[i];
    }
    return NULL;
}

const mpkg_installed_t *mpkg_state_find(const mpkg_state_t *state, const char *name) {
    if (!state || !name) return NULL;
    for (size_t i = 0; i < state->count; i++) {
        if (!strcmp(state->entries[i].name, name)) return &state->entries[i];
    }
    return NULL;
}

bool mpkg_state_record(mpkg_state_t *state, const mpkg_package_t *pkg, const char *path) {
    if (!state || !pkg || !path) return false;
    mpkg_installed_t *slot = NULL;
    for (size_t i = 0; i < state->count; i++) {
        if (!strcmp(state->entries[i].name, pkg->name)) {
            slot = &state->entries[i];
            break;
        }
    }
    if (!slot) {
        if (state->count >= MPKG_MAX_PACKAGES) return false;
        slot = &state->entries[state->count++];
    }
    memset(slot, 0, sizeof *slot);
    _copy(slot->name, sizeof slot->name, pkg->name);
    _copy(slot->version, sizeof slot->version, pkg->version);
    _copy(slot->path, sizeof slot->path, path);
    _copy(slot->sha256, sizeof slot->sha256, pkg->sha256);
    slot->kind = pkg->kind;
    return true;
}

bool mpkg_state_remove(mpkg_state_t *state, const char *name) {
    if (!state || !name) return false;
    for (size_t i = 0; i < state->count; i++) {
        if (strcmp(state->entries[i].name, name) != 0) continue;
        memmove(&state->entries[i], &state->entries[i + 1],
                (state->count - i - 1) * sizeof state->entries[0]);
        state->count--;
        return true;
    }
    return false;
}

bool mpkg_package_path(const mpkg_package_t *pkg, char *out, size_t cap) {
    if (!pkg || !out || cap == 0) return false;
    const char *dir = mpkg_kind_dir(pkg->kind);
    if (!*dir || !_valid_file_name(pkg->file)) return false;
    int n = snprintf(out, cap, "%s/%s", dir, pkg->file);
    if (n < 0 || (size_t)n >= cap) {
        out[0] = '\0';
        return false;
    }
    return true;
}

mpkg_action_t mpkg_plan(const mpkg_index_t *index, const mpkg_state_t *state, const char *name,
                        const mpkg_package_t **package) {
    if (package) *package = NULL;
    const mpkg_package_t *available = mpkg_index_find(index, name);
    if (!available) return MPKG_ACTION_UNKNOWN;
    if (package) *package = available;

    const mpkg_installed_t *installed = mpkg_state_find(state, name);
    if (!installed) return MPKG_ACTION_INSTALL;

    int ordering = 0;
    if (!mpkg_version_compare(available->version, installed->version, &ordering))
        return MPKG_ACTION_UPGRADE;
    if (ordering > 0) return MPKG_ACTION_UPGRADE;
    if (ordering < 0) return MPKG_ACTION_DOWNGRADE;
    return MPKG_ACTION_CURRENT;
}

const char *mpkg_action_name(mpkg_action_t action) {
    switch (action) {
    case MPKG_ACTION_INSTALL:
        return "install";
    case MPKG_ACTION_UPGRADE:
        return "upgrade";
    case MPKG_ACTION_CURRENT:
        return "current";
    case MPKG_ACTION_DOWNGRADE:
        return "downgrade";
    default:
        return "unknown";
    }
}
