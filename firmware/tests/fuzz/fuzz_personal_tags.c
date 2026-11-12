#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "fake_hal.h"
#include "fixture_library.h"
#include "fuzz_source.h"
#include "personal_tags.h"

enum {
    ACTION_BYTES = 5,
    ACTION_LIMIT = 64
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void finish(PersonalTags *tags)
{
    for (int step = 0; step < 4096 && tags->result == STORE_WORKING; step++) {
        personal_tags_step(tags);
    }
    fuzz_require(tags->result != STORE_WORKING);
}

static void make_name(char *name, const uint8_t *bytes)
{
    int length = 1 + bytes[0] % (PERSONAL_TAG_NAME_BYTES - 1);
    for (int index = 0; index < length; index++) {
        name[index] = (char)('!' + bytes[1 + index % 3] % 94);
    }
    name[length] = 0;
}

static void check_tags(PersonalTags *tags, uint16_t tracks)
{
    fuzz_require(tags->load >= STORE_EMPTY && tags->load <= STORE_CORRUPT);
    fuzz_require(tags->result >= STORE_IDLE && tags->result <= STORE_UNCERTAIN);
    fuzz_require(personal_tags_progress(tags) <= 100);
    for (uint8_t tag = 0; tag < PERSONAL_TAG_LIMIT; tag++) {
        const char *name = personal_tags_name(tags, tag);
        fuzz_require(((tags->catalog.active >> tag) & 1u) == (name != NULL));
    }
    if (tags->result != STORE_WORKING && tracks > 0) {
        uint32_t mask = 0;
        bool matches = false;
        uint16_t track = (uint16_t)(tags->position % tracks);
        if (personal_tags_mask(tags, track, &mask)) {
            fuzz_require(personal_tags_matches(tags, track, mask, &matches));
            fuzz_require(matches);
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FixtureLibrary fixture;
    PersonalTags tags;
    fake_hal_reset();
    fixture_library_open(&fixture);
    if (size > 1 && (data[0] & 1u) != 0) {
        fake_hal_add_file(PERSONAL_TAG_PATH_A, data + 1, (uint32_t)(size - 1));
    }
    if (size > 2 && (data[0] & 2u) != 0) {
        fake_hal_add_file(PERSONAL_TAG_PATH_B, data + 2, (uint32_t)(size - 2));
    }
    if ((size > 0 ? data[0] : 0) & 4u) {
        fake_hal_fail_file_opens(PERSONAL_TAG_PATH_A);
    }
    if ((size > 0 ? data[0] : 0) & 8u) {
        fake_hal_fail_file_reads(PERSONAL_TAG_PATH_B);
    }
    personal_tags_init(&tags, &fixture.library);
    uint16_t tracks = (uint16_t)library_count(&fixture.library, LIBRARY_TRACKS);
    size_t actions = size / ACTION_BYTES;
    if (actions > ACTION_LIMIT) {
        actions = ACTION_LIMIT;
    }
    for (size_t action = 0; action < actions; action++) {
        const uint8_t *bytes = data + action * ACTION_BYTES;
        if (tags.result == STORE_WORKING) {
            int steps = 1 + bytes[1] % 8;
            while (steps-- > 0 && tags.result == STORE_WORKING) {
                personal_tags_step(&tags);
            }
            check_tags(&tags, tracks);
            continue;
        }
        char name[PERSONAL_TAG_NAME_BYTES];
        make_name(name, bytes + 1);
        uint8_t tag = bytes[1] % (PERSONAL_TAG_LIMIT + 2);
        uint16_t track = tracks == 0 ? 0 : bytes[2] % tracks;
        switch (bytes[0] % 8) {
        case 0:
            personal_tags_change(
                &tags,
                (PersonalTagChange){.operation = PERSONAL_TAG_CREATE, .name = name, .tag = tag});
            break;
        case 1:
            personal_tags_change(
                &tags,
                (PersonalTagChange){.operation = PERSONAL_TAG_RENAME, .name = name, .tag = tag});
            break;
        case 2:
            personal_tags_change(&tags,
                                 (PersonalTagChange){.operation = PERSONAL_TAG_DELETE, .tag = tag});
            break;
        case 3:
            personal_tags_change(&tags,
                                 (PersonalTagChange){.operation = PERSONAL_TAG_ASSIGN,
                                                     .track = track,
                                                     .mask = (uint32_t)bytes[3] << 24 |
                                                             (uint32_t)bytes[4] << 8 | bytes[1]});
            break;
        case 4:
            personal_tags_cancel(&tags);
            break;
        case 5:
            finish(&tags);
            personal_tags_init(&tags, &fixture.library);
            break;
        case 6:
            fake_hal_fail_music_writes((bytes[1] & 1u) != 0);
            break;
        default: {
            uint32_t mask = 0;
            bool matches = false;
            personal_tags_mask(&tags, track, &mask);
            personal_tags_matches(&tags, track, (uint32_t)bytes[3] << 24, &matches);
            break;
        }
        }
        check_tags(&tags, tracks);
    }
    finish(&tags);
    check_tags(&tags, tracks);
    return 0;
}
