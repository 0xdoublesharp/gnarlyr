#include "library/library_content.h"

#include <stddef.h>

#include "core/hash.h"

enum {
    HEADER_PART = 0,
    PART_COUNT = LIBRARY_SECTION_COUNT + 1
};

static bool opened(const Library *library)
{
    return library->source.read != NULL && library->sections[LIBRARY_STRINGS].record_size != 0;
}

static uint32_t part_offset(const Library *library, uint8_t part)
{
    return part == HEADER_PART ? 0 : library->sections[part - 1].offset;
}

static uint32_t part_length(const Library *library, uint8_t part)
{
    if (part == HEADER_PART) {
        return LIBRARY_HEADER_SIZE;
    }
    LibrarySectionInfo info = library->sections[part - 1];
    return info.count * info.record_size;
}

void library_content_begin(LibraryContent *job, Library *library)
{
    library->content_known = false;
    library->content_fingerprint = 0;
    *job = (LibraryContent){.library = library,
                            .hash = HASH_START,
                            .state =
                                opened(library) ? LIBRARY_CONTENT_WORKING : LIBRARY_CONTENT_FAILED};
}

static void advance(LibraryContent *job)
{
    Library *library = job->library;
    while (job->part < PART_COUNT && job->position == part_length(library, job->part)) {
        job->part++;
        job->position = 0;
    }
    if (job->part == PART_COUNT) {
        library->content_fingerprint = job->hash;
        library->content_known = true;
        job->state = LIBRARY_CONTENT_READY;
    }
}

LibraryContentState library_content_step(LibraryContent *job)
{
    uint8_t block[LIBRARY_CONTENT_BLOCK_BYTES];
    if (job->state != LIBRARY_CONTENT_WORKING) {
        return job->state;
    }
    const Library *library = job->library;
    uint32_t left = part_length(library, job->part) - job->position;
    uint32_t count = left < sizeof block ? left : (uint32_t)sizeof block;
    uint32_t offset = part_offset(library, job->part) + job->position;
    if (!library->source.read(library->source.context, offset, block, count)) {
        job->state = LIBRARY_CONTENT_FAILED;
        return job->state;
    }
    job->hash = hash_update(job->hash, block, count);
    job->position += count;
    advance(job);
    return job->state;
}

bool library_content_fingerprint(const Library *library, uint32_t *fingerprint)
{
    *fingerprint = library->content_fingerprint;
    return library->content_known;
}
