/*
 * test_mpkg.c — index parsing, version ordering, install state, and planning.
 */

#include "core/mpkg.h"
#include "test_util.h"

#include <string.h>

static char _arena_buf[8192];
static json_arena_t _arena;

static const char *INDEX_JSON =
    "{"
    "\"schema\": 1,"
    "\"name\": \"Community\","
    "\"packages\": ["
    "  {\"name\": \"hello\", \"version\": \"1.0.0\", \"kind\": \"mapp\","
    "   \"file\": \"hello.mapp\", \"url\": \"https://example.test/hello.mapp\","
    "   \"sha256\": \"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\","
    "   \"size\": 1024, \"description\": \"Hello world\"},"
    "  {\"name\": \"ocean\", \"version\": \"2.1\", \"kind\": \"theme\","
    "   \"file\": \"ocean.thm\", \"url\": \"https://example.test/ocean.thm\","
    "   \"sha256\": \"BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD\"},"
    "  {\"name\": \"demo\", \"version\": \"0.1\", \"kind\": \"script\","
    "   \"file\": \"demo.bas\", \"url\": \"https://example.test/demo.bas\","
    "   \"sha256\": \"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1\"},"
    "  {\"name\": \"broken\", \"version\": \"1\", \"kind\": \"mapp\","
    "   \"file\": \"../evil.mapp\", \"url\": \"https://example.test/x\","
    "   \"sha256\": \"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\"},"
    "  {\"name\": \"badhash\", \"version\": \"1\", \"kind\": \"mapp\","
    "   \"file\": \"x.mapp\", \"url\": \"https://example.test/x\","
    "   \"sha256\": \"deadbeef\"}"
    "]"
    "}";

void test_mpkg(void) {
    /* Version ordering. */
    int ordering = 0;
    CHECK(mpkg_version_compare("1.0.0", "1.0.0", &ordering) && ordering == 0);
    CHECK(mpkg_version_compare("1.0.1", "1.0.0", &ordering) && ordering == 1);
    CHECK(mpkg_version_compare("1.0", "1.0.0", &ordering) && ordering == 0);
    CHECK(mpkg_version_compare("1.10", "1.9", &ordering) && ordering == 1);
    CHECK(mpkg_version_compare("v2.0", "1.9.9", &ordering) && ordering == 1);
    CHECK(mpkg_version_compare("1.0.0-rc1", "1.0.0", &ordering) && ordering == -1);

    /* Kind mapping. */
    CHECK_EQ(mpkg_kind_parse("mapp"), MPKG_KIND_MAPP);
    CHECK_EQ(mpkg_kind_parse("theme"), MPKG_KIND_THEME);
    CHECK_EQ(mpkg_kind_parse("basic"), MPKG_KIND_SCRIPT);
    CHECK_EQ(mpkg_kind_parse("nonsense"), MPKG_KIND_UNKNOWN);
    CHECK_STR_EQ(mpkg_kind_dir(MPKG_KIND_MAPP), "/apps");
    CHECK_STR_EQ(mpkg_kind_dir(MPKG_KIND_THEME), "/themes");
    CHECK_STR_EQ(mpkg_kind_dir(MPKG_KIND_SCRIPT), "/bin");

    /* Index parsing. */
    json_arena_init(&_arena, _arena_buf, sizeof _arena_buf);
    mpkg_index_t index;
    CHECK(mpkg_index_load(&index, &_arena, INDEX_JSON, strlen(INDEX_JSON)));
    CHECK_EQ(index.schema, 1);
    CHECK_STR_EQ(index.name, "Community");
    CHECK_EQ(index.count, 3);
    CHECK_EQ(index.rejected, 2);

    const mpkg_package_t *hello = mpkg_index_find(&index, "hello");
    CHECK(hello != NULL);
    CHECK_STR_EQ(hello->version, "1.0.0");
    CHECK_EQ(hello->kind, MPKG_KIND_MAPP);
    CHECK_EQ(hello->size, 1024);
    CHECK_STR_EQ(hello->description, "Hello world");
    CHECK(mpkg_index_find(&index, "broken") == NULL);

    char path[MPKG_PATH_MAX];
    CHECK(mpkg_package_path(hello, path, sizeof path));
    CHECK_STR_EQ(path, "/apps/hello.mapp");
    const mpkg_package_t *ocean = mpkg_index_find(&index, "ocean");
    CHECK(mpkg_package_path(ocean, path, sizeof path));
    CHECK_STR_EQ(path, "/themes/ocean.thm");
    const mpkg_package_t *demo = mpkg_index_find(&index, "demo");
    CHECK(mpkg_package_path(demo, path, sizeof path));
    CHECK_STR_EQ(path, "/bin/demo.bas");

    /* A path-traversal file name must never be accepted. */
    mpkg_package_t hostile = *hello;
    strcpy(hostile.file, "..");
    CHECK(!mpkg_package_path(&hostile, path, sizeof path));
    strcpy(hostile.file, "sub/dir.mapp");
    CHECK(!mpkg_package_path(&hostile, path, sizeof path));

    /* Planning. */
    mpkg_state_t state;
    mpkg_state_load(&state, NULL, NULL, 0);
    CHECK_EQ(state.count, 0);
    CHECK_EQ(mpkg_plan(&index, &state, "hello", NULL), MPKG_ACTION_INSTALL);
    CHECK_EQ(mpkg_plan(&index, &state, "ghost", NULL), MPKG_ACTION_UNKNOWN);

    CHECK(mpkg_state_record(&state, hello, "/apps/hello.mapp"));
    CHECK_EQ(state.count, 1);
    CHECK_EQ(mpkg_plan(&index, &state, "hello", NULL), MPKG_ACTION_CURRENT);

    /* Re-recording the same name must update in place, not duplicate. */
    mpkg_package_t newer = *hello;
    strcpy(newer.version, "1.2.0");
    CHECK(mpkg_state_record(&state, &newer, "/apps/hello.mapp"));
    CHECK_EQ(state.count, 1);

    /* Round-trip through the on-card state file. */
    char text[1024];
    int n = mpkg_state_serialize(&state, text, sizeof text);
    CHECK(n > 0);
    json_arena_init(&_arena, _arena_buf, sizeof _arena_buf);
    mpkg_state_t reloaded;
    CHECK(mpkg_state_load(&reloaded, &_arena, text, (size_t)n));
    CHECK_EQ(reloaded.count, 1);
    CHECK_STR_EQ(reloaded.entries[0].name, "hello");
    CHECK_STR_EQ(reloaded.entries[0].version, "1.2.0");
    CHECK_STR_EQ(reloaded.entries[0].path, "/apps/hello.mapp");
    CHECK_EQ(reloaded.entries[0].kind, MPKG_KIND_MAPP);

    /* Downgrade detection uses the recorded state, not the index. */
    mpkg_state_t future;
    mpkg_state_load(&future, NULL, NULL, 0);
    mpkg_package_t v9 = *hello;
    strcpy(v9.version, "9.0.0");
    CHECK(mpkg_state_record(&future, &v9, "/apps/hello.mapp"));
    CHECK_EQ(mpkg_plan(&index, &future, "hello", NULL), MPKG_ACTION_DOWNGRADE);

    CHECK(mpkg_state_remove(&state, "hello"));
    CHECK_EQ(state.count, 0);
    CHECK(!mpkg_state_remove(&state, "hello"));

    /* Malformed documents are rejected. */
    json_arena_init(&_arena, _arena_buf, sizeof _arena_buf);
    const char *bad_schema = "{\"schema\":2,\"packages\":[]}";
    CHECK(!mpkg_index_load(&index, &_arena, bad_schema, strlen(bad_schema)));
    json_arena_init(&_arena, _arena_buf, sizeof _arena_buf);
    const char *no_packages = "{\"schema\":1}";
    CHECK(!mpkg_index_load(&index, &_arena, no_packages, strlen(no_packages)));
}
