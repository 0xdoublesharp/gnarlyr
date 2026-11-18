#include <string.h>

#include "check.h"
#include "core/drive_path.h"

static void a_path_under_the_marker_names_the_same_path_on_internal_storage(void)
{
    const char *path = drive_path_internal("/\"/Music/Artist/01 Song.flac");
    CHECK(path != NULL);
    CHECK(strcmp(path, "/Music/Artist/01 Song.flac") == 0);
}

static void the_marker_alone_names_the_internal_root(void)
{
    CHECK(strcmp(drive_path_internal(DRIVE_PATH_INTERNAL_ROOT), "/") == 0);
    CHECK(strcmp(drive_path_internal("/\"/"), "/") == 0);
}

static void card_paths_stay_on_the_music_volume(void)
{
    CHECK(drive_path_internal("/") == NULL);
    CHECK(drive_path_internal("/Music/01 Song.flac") == NULL);
    CHECK(drive_path_internal("") == NULL);
}

static void a_marker_inside_a_longer_name_or_deeper_in_the_path_is_not_the_drive(void)
{
    CHECK(drive_path_internal("/\"Quoted\"/song.mp3") == NULL);
    CHECK(drive_path_internal("/Music/\"/song.mp3") == NULL);
    CHECK(drive_path_internal("\"/song.mp3") == NULL);
}

static void a_long_path_is_routed_without_being_shortened(void)
{
    static char path[1100];
    memcpy(path, "/\"", 2);
    for (int index = 2; index < 1090; index++) {
        path[index] = index % 64 == 2 ? '/' : 'a';
    }
    path[1090] = '\0';
    const char *routed = drive_path_internal(path);
    CHECK(routed == path + 2);
    CHECK_EQUAL(1088, (int)strlen(routed));
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(a_path_under_the_marker_names_the_same_path_on_internal_storage),
        TEST_CASE(the_marker_alone_names_the_internal_root),
        TEST_CASE(card_paths_stay_on_the_music_volume),
        TEST_CASE(a_marker_inside_a_longer_name_or_deeper_in_the_path_is_not_the_drive),
        TEST_CASE(a_long_path_is_routed_without_being_shortened),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
