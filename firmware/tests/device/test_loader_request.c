#include <string.h>

#include "check.h"
#include "device/loader_request.h"

typedef struct FakeFile {
    uint8_t bytes[64];
    uint32_t size;
    bool write_fails;
    bool write_is_ignored;
} FakeFile;

static bool fake_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    FakeFile *file = context;
    if (offset > file->size || length > file->size - offset) {
        return false;
    }
    memcpy(buffer, file->bytes + offset, length);
    return true;
}

static bool fake_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    FakeFile *file = context;
    if (file->write_fails || offset > file->size || length > file->size - offset) {
        return false;
    }
    if (!file->write_is_ignored) {
        memcpy(file->bytes + offset, bytes, length);
    }
    return true;
}

static FakeFile request_file(const char *text)
{
    FakeFile file = {.size = (uint32_t)strlen(text)};
    memcpy(file.bytes, text, file.size);
    return file;
}

static LoaderRequestFile view(FakeFile *file)
{
    return (LoaderRequestFile){fake_read, fake_write, file->size, file};
}

static void a_file_holding_the_request_text_is_consumed(void)
{
    FakeFile file = request_file("MASKROM REQ");
    LoaderRequestFile request = view(&file);
    CHECK(loader_request_consume(&request));
    CHECK_EQUAL(0, file.bytes[0]);
}

static void a_consumed_request_does_not_trigger_again(void)
{
    FakeFile file = request_file("MASKROM REQ\n");
    LoaderRequestFile request = view(&file);
    CHECK(loader_request_consume(&request));
    CHECK(!loader_request_consume(&request));
}

static void text_after_the_request_is_allowed(void)
{
    FakeFile file = request_file("MASKROM REQ please");
    LoaderRequestFile request = view(&file);
    CHECK(loader_request_consume(&request));
    CHECK_EQUAL(' ', file.bytes[11]);
}

static void other_or_short_content_is_not_a_request(void)
{
    FakeFile other = request_file("maskrom req");
    LoaderRequestFile request = view(&other);
    CHECK(!loader_request_consume(&request));
    FakeFile shorter = request_file("MASKROM RE");
    request = view(&shorter);
    CHECK(!loader_request_consume(&request));
    FakeFile empty = request_file("");
    request = view(&empty);
    CHECK(!loader_request_consume(&request));
}

static void a_failed_write_means_no_reboot(void)
{
    FakeFile file = request_file("MASKROM REQ");
    file.write_fails = true;
    LoaderRequestFile request = view(&file);
    CHECK(!loader_request_consume(&request));
    CHECK_EQUAL('M', file.bytes[0]);
}

static void a_write_that_does_not_stick_means_no_reboot(void)
{
    FakeFile file = request_file("MASKROM REQ");
    file.write_is_ignored = true;
    LoaderRequestFile request = view(&file);
    CHECK(!loader_request_consume(&request));
}

static void the_request_file_is_named_maskrom_req_at_the_root(void)
{
    CHECK(strcmp("/MASKROM.REQ", LOADER_REQUEST_PATH) == 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_file_holding_the_request_text_is_consumed),
        TEST_CASE(a_consumed_request_does_not_trigger_again),
        TEST_CASE(text_after_the_request_is_allowed),
        TEST_CASE(other_or_short_content_is_not_a_request),
        TEST_CASE(a_failed_write_means_no_reboot),
        TEST_CASE(a_write_that_does_not_stick_means_no_reboot),
        TEST_CASE(the_request_file_is_named_maskrom_req_at_the_root),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
