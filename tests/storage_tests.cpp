#include "storage/disk_manager.h"
#include "storage/page.h"
#include "storage/record.h"
#include "storage/slotted_page.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

using namespace minidbms;

int main() {
    const std::filesystem::path database_directory = "data";
    const std::filesystem::path database_path =
        database_directory / "storage_sprint1_test.db";

    std::filesystem::create_directories(
        database_directory
    );

    std::filesystem::remove(database_path);

    PageId data_page_id = INVALID_PAGE_ID;
    SlotId first_slot = 0;
    SlotId second_slot = 0;

    {
        DiskManager disk_manager(
            database_path.string()
        );

        // Una base recién creada debe contener la Header Page.
        assert(disk_manager.GetNumPages() == 1);
        assert(disk_manager.GetFileSize() == PAGE_SIZE);

        // La primera página de datos debe ser PageId 1.
        data_page_id = disk_manager.AllocatePage();

        assert(data_page_id == 1);
        assert(disk_manager.GetNumPages() == 2);
        assert(
            disk_manager.GetFileSize() ==
            2 * PAGE_SIZE
        );

        Page page;
        page.SetPageId(data_page_id);

        SlottedPage slotted_page(&page);
        slotted_page.Init();

        const std::string first_value = "Alice";
        const std::string second_value = "Bob";

        Status status = slotted_page.InsertRecord(
            first_value.data(),
            static_cast<uint32_t>(first_value.size()),
            &first_slot
        );

        assert(status.ok());
        assert(first_slot == 0);

        status = slotted_page.InsertRecord(
            second_value.data(),
            static_cast<uint32_t>(second_value.size()),
            &second_slot
        );

        assert(status.ok());
        assert(second_slot == 1);

        Record first_record;
        status = slotted_page.ReadRecord(
            first_slot,
            &first_record
        );

        assert(status.ok());

        const std::string recovered_first(
            first_record.GetData(),
            first_record.GetSize()
        );

        assert(recovered_first == first_value);

        status = disk_manager.WritePage(
            data_page_id,
            page.GetData()
        );

        assert(status.ok());
    }

    // Reabrir el archivo para comprobar persistencia.
    {
        DiskManager disk_manager(
            database_path.string()
        );

        assert(disk_manager.GetNumPages() == 2);

        Page page;
        page.SetPageId(data_page_id);

        Status status = disk_manager.ReadPage(
            data_page_id,
            page.GetData()
        );

        assert(status.ok());

        SlottedPage slotted_page(&page);

        Record first_record;
        status = slotted_page.ReadRecord(
            first_slot,
            &first_record
        );

        assert(status.ok());

        const std::string recovered_first(
            first_record.GetData(),
            first_record.GetSize()
        );

        assert(recovered_first == "Alice");

        Record second_record;
        status = slotted_page.ReadRecord(
            second_slot,
            &second_record
        );

        assert(status.ok());

        const std::string recovered_second(
            second_record.GetData(),
            second_record.GetSize()
        );

        assert(recovered_second == "Bob");
    }

    std::cout
        << "Storage Sprint 1 tests passed.\n"
        << "Database file: "
        << database_path
        << '\n'
        << "Expected file size: "
        << 2 * PAGE_SIZE
        << " bytes\n";

    return 0;
}// storage_tests.cpp

