/*
 * tools/hub/mpkg_check.c — load a repository index with the firmware parser.
 *
 * The Hub indexer emits JSON; this utility feeds that JSON to the very same
 * core/json.c + core/mpkg.c the PicoCalc runs, so a schema drift is caught
 * on the host instead of on the card. Used by tools/hub/test_hub.py.
 *
 *   mpkg_check INDEX.json
 *
 * Prints "ok: N package(s)" and exits 0, or prints the rejection and exits 1.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/json.h"
#include "core/mpkg.h"

#define ARENA_BYTES (16u * 1024u)

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s INDEX.json\n", argv[0]);
        return 2;
    }

    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        perror(argv[1]);
        return 2;
    }
    static char text[64 * 1024];
    size_t len = fread(text, 1, sizeof text - 1, file);
    fclose(file);
    text[len] = '\0';

    static char arena_buf[ARENA_BYTES];
    static mpkg_index_t index;
    json_arena_t arena;
    json_arena_init(&arena, arena_buf, sizeof arena_buf);

    if (!mpkg_index_load(&index, &arena, text, len)) {
        printf("rejected: mpkg would not load this index\n");
        return 1;
    }
    if (index.count == 0) {
        printf("rejected: index has no usable packages\n");
        return 1;
    }

    printf("ok: %zu package(s)", index.count);
    if (index.rejected) printf(", %zu rejected", index.rejected);
    printf("\n");

    for (size_t i = 0; i < index.count; i++) {
        const mpkg_package_t *pkg = &index.packages[i];
        char path[MPKG_PATH_MAX];
        if (!mpkg_package_path(pkg, path, sizeof path)) {
            printf("  %s: no destination could be resolved\n", pkg->name);
            return 1;
        }
        printf("  %-24s %-10s %-8s %s %s\n", pkg->name, pkg->version,
               mpkg_kind_name(pkg->kind), path, pkg->sha256[0] ? "signed" : "unsigned");
    }
    return 0;
}
