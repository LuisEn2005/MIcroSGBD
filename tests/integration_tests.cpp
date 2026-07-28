#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "catalog/catalog_manager.h"
#include "catalog/schema.h"
#include "query/executor.h"
#include "query/parser.h"
#include "query/tokenizer.h"
#include "storage/disk_manager.h"
#include "storage/heap_file.h"
#include "storage/record_codec.h"
#include "storage/slotted_page.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

using namespace minidbms;

namespace {

Schema BuildSchema() {
    return Schema({
        {"id", TypeId::INTEGER, sizeof(int32_t)},
        {"name", TypeId::VARCHAR, 3500}
    });
}

Record BuildLargeRecord(
    const Schema& schema,
    int32_t id
) {
    const std::string large_name =
        "row-" + std::to_string(id) + "-" + std::string(3000, 'x');

    Record record;
    Status status = RecordCodec::Serialize(
        schema,
        {id, large_name},
        &record
    );
    assert(status.ok());
    return record;
}

void TestSelectPipelineWithThreeFramesAndTenPages() {
    const std::filesystem::path database_path =
        "data/sprint2_integration.db";

    std::filesystem::create_directories("data");
    std::filesystem::remove(database_path);

    const Schema schema = BuildSchema();
    PageId first_page_id = INVALID_PAGE_ID;

    // Fase 1: crear 10 páginas persistentes usando únicamente 3 frames.
    {
        DiskManager disk_manager(database_path.string());
        ClockReplacer replacer(3);
        BufferPoolManager buffer_pool(3, &disk_manager, &replacer);

        Page* first_page = buffer_pool.NewPage(&first_page_id);
        assert(first_page != nullptr);
        assert(first_page_id == 1);

        SlottedPage first_slotted_page(first_page);
        assert(first_slotted_page.Init().ok());
        assert(buffer_pool.UnpinPage(first_page_id, true));

        HeapFile heap_file(&buffer_pool, first_page_id);

        for (int32_t id = 0; id < 10; ++id) {
            Record record = BuildLargeRecord(schema, id);
            RecordID rid;

            Status status = heap_file.InsertRecord(record, &rid);
            assert(status.ok());

            // Cada registro ocupa aproximadamente 3 KB, por lo que
            // solamente cabe uno por página.
            assert(rid.page_id == id + 1);
        }

        assert(disk_manager.GetNumPages() == 11);
        assert(buffer_pool.GetDiskWrites() > 0);
        buffer_pool.FlushAllPages();
    }

    assert(
        std::filesystem::file_size(database_path) ==
        11 * PAGE_SIZE
    );

    // Fase 2: reabrir, construir SELECT y recorrer:
    // SELECT -> Projection -> Filter -> SeqScan -> Buffer -> Disk.
    {
        DiskManager disk_manager(database_path.string());
        ClockReplacer replacer(3);
        BufferPoolManager buffer_pool(3, &disk_manager, &replacer);

        CatalogManager catalog;
        assert(
            catalog.CreateTable(
                "items",
                schema,
                first_page_id
            ).ok()
        );

        Tokenizer tokenizer(
            "SELECT name FROM items WHERE id >= 5;"
        );
        Parser parser(tokenizer.Tokenize());

        std::unique_ptr<SQLStatement> statement;
        Status status = parser.Parse(&statement);
        assert(status.ok());
        assert(statement != nullptr);

        QueryExecutor executor(&catalog, &buffer_pool);
        QueryStats stats;

        status = executor.Execute(*statement, &stats);
        assert(status.ok());

        assert(stats.records_examined == 10);
        assert(stats.rows_returned == 5);
        assert(stats.pages_scanned == 10);
        assert(stats.buffer_misses == 10);
        assert(stats.disk_reads == 10);

        // Con 3 frames y 10 páginas hubo reemplazo real.
        assert(stats.buffer_misses > buffer_pool.GetPoolSize());
    }

    std::cout
        << "Sprint 2 integration test passed.\n"
        << "Frames: 3\n"
        << "Data pages read: 10\n"
        << "Records examined: 10\n"
        << "Rows returned: 5\n";
}

} // namespace

int main() {
    TestSelectPipelineWithThreeFramesAndTenPages();
    return 0;
}
