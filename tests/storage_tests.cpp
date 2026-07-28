#include "storage/disk_manager.h"
#include "storage/page.h"
#include "storage/record.h"
#include "storage/slotted_page.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace minidbms;

namespace {

std::string BuildRecordValue(int index) {
    std::ostringstream output;

    output << "record-"
           << std::setw(3)
           << std::setfill('0')
           << index
           << "-persistent-payload";

    return output.str();
}

} // namespace

int main() {
    constexpr int RECORD_COUNT = 100;

    const std::filesystem::path database_directory = "data";
    const std::filesystem::path database_path =
        database_directory / "storage_sprint1_test.db";

    std::filesystem::create_directories(database_directory);
    std::filesystem::remove(database_path);

    PageId data_page_id = INVALID_PAGE_ID;

    std::vector<SlotId> slots;
    std::vector<std::string> expected_records;

    slots.reserve(RECORD_COUNT);
    expected_records.reserve(RECORD_COUNT);

    for (int index = 0; index < RECORD_COUNT; ++index) {
        expected_records.push_back(
            BuildRecordValue(index)
        );
    }

    // ==================================================
    // Fase 1: crear, insertar 100 registros y cerrar
    // ==================================================

    {
        DiskManager disk_manager(
            database_path.string()
        );

        // Una base nueva contiene únicamente la Header Page.
        assert(disk_manager.GetNumPages() == 1);
        assert(disk_manager.GetFileSize() == PAGE_SIZE);

        // Primera página de datos.
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

        const Status init_status =
            slotted_page.Init();

        assert(init_status.ok());
        assert(slotted_page.IsInitialized());
        assert(slotted_page.GetSlotCount() == 0);
        assert(
            slotted_page.GetNextPageId() ==
            INVALID_PAGE_ID
        );

        // Insertar 100 registros.
        for (int index = 0;
             index < RECORD_COUNT;
             ++index) {

            const std::string& value =
                expected_records[index];

            SlotId slot_id = 0;

            const Status insert_status =
                slotted_page.InsertRecord(
                    value.data(),
                    static_cast<uint32_t>(
                        value.size()
                    ),
                    &slot_id
                );

            assert(insert_status.ok());
            assert(
                slot_id ==
                static_cast<SlotId>(index)
            );

            slots.push_back(slot_id);
        }

        assert(
            slotted_page.GetSlotCount() ==
            RECORD_COUNT
        );

        assert(slotted_page.GetFreeSpace() > 0);

        // Verificar los registros antes de escribir.
        for (int index = 0;
             index < RECORD_COUNT;
             ++index) {

            Record record;

            const Status read_status =
                slotted_page.ReadRecord(
                    slots[index],
                    &record
                );

            assert(read_status.ok());

            assert(
                record.GetRecordID().page_id ==
                data_page_id
            );

            assert(
                record.GetRecordID().slot_id ==
                slots[index]
            );

            const std::string recovered(
                record.GetData(),
                record.GetSize()
            );

            assert(
                recovered ==
                expected_records[index]
            );
        }

        // Persistir la página completa.
        const Status write_status =
            disk_manager.WritePage(
                data_page_id,
                page.GetData()
            );

        assert(write_status.ok());
    }

    // Al salir del bloque, el primer DiskManager fue destruido
    // y el archivo quedó cerrado.

    assert(
        std::filesystem::file_size(database_path) ==
        2 * PAGE_SIZE
    );

    // ==================================================
    // Fase 2: abrir nuevamente y recuperar los 100
    // ==================================================

    {
        DiskManager disk_manager(
            database_path.string()
        );

        assert(disk_manager.GetNumPages() == 2);

        assert(
            disk_manager.GetFileSize() ==
            2 * PAGE_SIZE
        );

        Page page;
        page.SetPageId(data_page_id);

        const Status read_page_status =
            disk_manager.ReadPage(
                data_page_id,
                page.GetData()
            );

        assert(read_page_status.ok());

        SlottedPage slotted_page(&page);

        assert(slotted_page.IsInitialized());

        assert(
            slotted_page.GetSlotCount() ==
            RECORD_COUNT
        );

        assert(
            slotted_page.GetNextPageId() ==
            INVALID_PAGE_ID
        );

        // Recuperar y comparar los 100 registros.
        for (int index = 0;
             index < RECORD_COUNT;
             ++index) {

            Record record;

            const Status read_status =
                slotted_page.ReadRecord(
                    slots[index],
                    &record
                );

            assert(read_status.ok());

            assert(
                record.GetRecordID().page_id ==
                data_page_id
            );

            assert(
                record.GetRecordID().slot_id ==
                slots[index]
            );

            const std::string recovered(
                record.GetData(),
                record.GetSize()
            );

            assert(
                recovered ==
                expected_records[index]
            );
        }

        // El slot 100 no debe existir.
        Record missing_record;

        const Status missing_status =
            slotted_page.ReadRecord(
                static_cast<SlotId>(RECORD_COUNT),
                &missing_record
            );

        assert(
            missing_status.code() ==
            StatusCode::NOT_FOUND
        );
    }

    std::cout
        << "Storage Sprint 1 acceptance test passed.\n"
        << "Inserted records: "
        << RECORD_COUNT
        << '\n'
        << "Recovered records after reopening: "
        << RECORD_COUNT
        << '\n'
        << "Database file: "
        << database_path
        << '\n'
        << "Database size: "
        << 2 * PAGE_SIZE
        << " bytes\n";

    return 0;
}
