#include <cassert>
#include <filesystem>
#include <iostream>

#include "../include/buffer/buffer_pool_manager.h"
#include "../include/buffer/clock_replacer.h"
#include "../include/storage/disk_manager.h"
#include "../include/storage/heap_file.h"
#include "../include/storage/slotted_page.h"
#include "../include/query/tokenizer.h"
#include "../include/query/parser.h"
#include "../include/query/executor.h"

using namespace minidbms;

void TestInsertAndSelect() {
    std::filesystem::create_directories("data");
    std::filesystem::path db_path = "data/integration_test.db";
    std::filesystem::remove(db_path);

    PageId table_page_id = INVALID_PAGE_ID;

    {
        DiskManager disk_manager(db_path.string());
        ClockReplacer replacer(5);
        BufferPoolManager bpm(5, &disk_manager, &replacer);

        Page* page = bpm.NewPage(&table_page_id);
        assert(page != nullptr);

        SlottedPage slotted(page);
        slotted.Init();

        // 4. Insertar 10 registros
        for (int i = 0; i < 10; i++) {
            int value = 100 + i;
            Record record;
            record.SetData(reinterpret_cast<char*>(&value), sizeof(int));

            SlotId slot;
            Status status = slotted.InsertRecord(
                    record.GetData(),
                    record.GetSize(),
                    &slot
                    );
            assert(status.ok());
        }

        bpm.UnpinPage(table_page_id, true);
        bpm.FlushAllPages();
    }

    {
        DiskManager disk_manager(db_path.string());
        ClockReplacer replacer(5);
        BufferPoolManager bpm(5, &disk_manager, &replacer);

        Page* page = bpm.FetchPage(table_page_id);
        assert(page != nullptr);

        SlottedPage slotted(page);

        int count = 0;
        for (SlotId i = 0; i < 10; i++) {
            Record rec;
            Status status = slotted.ReadRecord(i, &rec);
            if (status.ok()) {
                count++;
            }
        }

        assert(count == 10);
        bpm.UnpinPage(table_page_id, false);
    }

    std::cout << "[PASSED] TestInsertAndSelect" << std::endl;
}

int main() {
    TestInsertAndSelect();

    return 0;
}
