#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "storage/disk_manager.h"
#include "storage/heap_file.h"
#include "storage/slotted_page.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace minidbms;

namespace {

std::string BuildPayload(int index) {
    std::ostringstream output;
    output << "record-" << index << '-';

    const char fill = static_cast<char>('A' + (index % 26));
    output << std::string(90, fill);

    return output.str();
}

Record MakeRecord(const std::string& value) {
    Record record;
    record.SetData(
        value.data(),
        static_cast<uint32_t>(value.size())
    );
    return record;
}

std::string RecordToString(const Record& record) {
    return std::string(record.GetData(), record.GetSize());
}

} // namespace

int main() {
    constexpr int RECORD_COUNT = 120;

    const std::filesystem::path test_directory = "data";
    const std::filesystem::path test_file =
        test_directory / "heap_file_test.db";

    std::filesystem::create_directories(test_directory);
    std::filesystem::remove(test_file);

    PageId first_page_id = INVALID_PAGE_ID;
    std::vector<RecordID> record_ids;
    std::vector<std::string> expected_values;

    record_ids.reserve(RECORD_COUNT);
    expected_values.reserve(RECORD_COUNT);

    for (int index = 0; index < RECORD_COUNT; ++index) {
        expected_values.push_back(BuildPayload(index));
    }

    RecordID replacement_rid{};
    const std::string updated_value = "record-10-updated";
    const std::string replacement_value = "replacement-record";

    {
        DiskManager disk_manager(test_file.string());
        ClockReplacer replacer(3);
        BufferPoolManager buffer_pool(3, &disk_manager, &replacer);

        Page* first_page = buffer_pool.NewPage(&first_page_id);
        assert(first_page != nullptr);
        assert(first_page_id == 1);

        SlottedPage first_slotted_page(first_page);
        assert(first_slotted_page.Init().ok());
        assert(buffer_pool.UnpinPage(first_page_id, true));

        HeapFile heap_file(&buffer_pool, first_page_id);
        std::set<PageId> used_pages;

        for (int index = 0; index < RECORD_COUNT; ++index) {
            Record record = MakeRecord(expected_values[index]);
            RecordID rid;

            const Status status = heap_file.InsertRecord(record, &rid);
            assert(status.ok());

            record_ids.push_back(rid);
            used_pages.insert(rid.page_id);
        }

        assert(used_pages.size() >= 3);
        assert(disk_manager.GetNumPages() >= 4);

        for (int index = 0; index < RECORD_COUNT; ++index) {
            Record recovered;
            const Status status = heap_file.GetRecord(
                record_ids[index],
                &recovered
            );

            assert(status.ok());
            assert(RecordToString(recovered) == expected_values[index]);
        }

        Record updated(
            record_ids[10],
            static_cast<uint32_t>(updated_value.size()),
            updated_value.data()
        );

        assert(heap_file.UpdateRecord(updated).ok());
        expected_values[10] = updated_value;

        assert(heap_file.DeleteRecord(record_ids[20]).ok());
        assert(heap_file.DeleteRecord(record_ids[50]).ok());
        assert(heap_file.DeleteRecord(record_ids[90]).ok());

        Record deleted_record;
        assert(
            heap_file.GetRecord(record_ids[20], &deleted_record).code() ==
            StatusCode::NOT_FOUND
        );

        Record replacement = MakeRecord(replacement_value);
        assert(heap_file.InsertRecord(replacement, &replacement_rid).ok());

        // InsertRecord recorre desde la primera página, por lo que debe
        // reutilizar el primer slot eliminado que tenga espacio suficiente.
        assert(replacement_rid == record_ids[20]);

        Record first_scanned;
        RecordID current_rid;
        Status scan_status = heap_file.GetFirstRecord(
            &first_scanned,
            &current_rid
        );

        assert(scan_status.ok());

        int scanned_count = 1;

        while (true) {
            Record next_record;
            RecordID next_rid;

            scan_status = heap_file.GetNextRecord(
                current_rid,
                &next_record,
                &next_rid
            );

            if (scan_status.code() == StatusCode::NOT_FOUND) {
                break;
            }

            assert(scan_status.ok());
            current_rid = next_rid;
            ++scanned_count;
        }

        assert(scanned_count == RECORD_COUNT - 2);
        buffer_pool.FlushAllPages();
    }

    {
        DiskManager disk_manager(test_file.string());
        ClockReplacer replacer(2);
        BufferPoolManager buffer_pool(2, &disk_manager, &replacer);
        HeapFile heap_file(&buffer_pool, first_page_id);

        for (int index = 0; index < RECORD_COUNT; ++index) {
            if (index == 20) {
                continue;
            }

            Record recovered;
            const Status status = heap_file.GetRecord(
                record_ids[index],
                &recovered
            );

            if (index == 50 || index == 90) {
                assert(status.code() == StatusCode::NOT_FOUND);
                continue;
            }

            assert(status.ok());
            assert(RecordToString(recovered) == expected_values[index]);
        }

        Record replacement;
        assert(heap_file.GetRecord(replacement_rid, &replacement).ok());
        assert(RecordToString(replacement) == replacement_value);

        Record first_scanned;
        RecordID current_rid;
        Status scan_status = heap_file.GetFirstRecord(
            &first_scanned,
            &current_rid
        );

        assert(scan_status.ok());
        int scanned_count = 1;

        while (true) {
            Record next_record;
            RecordID next_rid;

            scan_status = heap_file.GetNextRecord(
                current_rid,
                &next_record,
                &next_rid
            );

            if (scan_status.code() == StatusCode::NOT_FOUND) {
                break;
            }

            assert(scan_status.ok());
            current_rid = next_rid;
            ++scanned_count;
        }

        assert(scanned_count == RECORD_COUNT - 2);
    }

    std::cout
        << "HeapFile Sprint 2 tests passed.\n"
        << "Inserted records: " << RECORD_COUNT << '\n'
        << "Updated records: 1\n"
        << "Deleted records: 3\n"
        << "Reused deleted slots: 1\n"
        << "Persistent live records: " << RECORD_COUNT - 2 << '\n';

    return 0;
}
