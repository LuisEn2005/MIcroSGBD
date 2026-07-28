#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "catalog/catalog_manager.h"
#include "catalog/schema.h"
#include "index/index_key.h"
#include "query/executor.h"
#include "query/operators/filter_operator.h"
#include "query/operators/seq_scan_operator.h"
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
#include <vector>

using namespace minidbms;

namespace {

constexpr int32_t TOTAL_RECORDS = 10000;
constexpr int32_t TARGET_ID = 7777;
constexpr std::size_t BUFFER_FRAMES = 3;

Schema BuildSchema() {
    return Schema({
        {"id", TypeId::INTEGER, sizeof(int32_t)},
        {"age", TypeId::INTEGER, sizeof(int32_t)}
    });
}

Record BuildRecord(
    const Schema& schema,
    int32_t id
) {
    Record record;

    const Status status = RecordCodec::Serialize(
        schema,
        {
            id,
            static_cast<int32_t>((id % 80) + 18)
        },
        &record
    );

    assert(status.ok());
    return record;
}

std::unique_ptr<SQLStatement> Parse(
    const std::string& sql
) {
    Tokenizer tokenizer(sql);
    Parser parser(tokenizer.Tokenize());

    std::unique_ptr<SQLStatement> statement;
    const Status status = parser.Parse(&statement);

    assert(status.ok());
    assert(statement != nullptr);
    return statement;
}

void CreateTableAndIndex(
    const std::filesystem::path& database_path,
    PageId* first_page_id,
    PageId* index_header_page_id
) {
    DiskManager disk_manager(database_path.string());
    ClockReplacer replacer(BUFFER_FRAMES);
    BufferPoolManager bpm(
        BUFFER_FRAMES,
        &disk_manager,
        &replacer
    );

    CatalogManager catalog(&bpm);
    assert(catalog.GetInitializationStatus().ok());

    Page* first_page = bpm.NewPage(first_page_id);
    assert(first_page != nullptr);

    SlottedPage slotted_page(first_page);
    assert(slotted_page.Init().ok());
    assert(bpm.UnpinPage(*first_page_id, true));

    const Schema schema = BuildSchema();

    assert(
        catalog.CreateTable(
            "usuarios",
            schema,
            *first_page_id
        ).ok()
    );

    HeapFile heap_file(&bpm, *first_page_id);

    for (int32_t id = 1;
         id <= TOTAL_RECORDS;
         ++id) {
        Record record = BuildRecord(schema, id);
        RecordID rid;

        const Status status =
            heap_file.InsertRecord(record, &rid);

        assert(status.ok());
    }

    QueryExecutor executor(&catalog, &bpm);
    std::unique_ptr<SQLStatement> create_index =
        Parse(
            "CREATE INDEX idx_usuarios_id "
            "ON usuarios (id);"
        );

    QueryStats create_stats;
    Status status = executor.Execute(
        *create_index,
        &create_stats
    );

    assert(status.ok());
    assert(catalog.HasIndex("usuarios", "id"));

    status = catalog.GetIndexHeaderPageId(
        "usuarios",
        "id",
        index_header_page_id
    );

    assert(status.ok());
    assert(*index_header_page_id > HEADER_PAGE_ID);
    assert(catalog.Flush().ok());
    bpm.FlushAllPages();
}

QueryStats RunIndexedQuery(
    const std::filesystem::path& database_path,
    PageId expected_first_page_id,
    PageId expected_index_header_page_id
) {
    DiskManager disk_manager(database_path.string());
    ClockReplacer replacer(BUFFER_FRAMES);
    BufferPoolManager bpm(
        BUFFER_FRAMES,
        &disk_manager,
        &replacer
    );

    CatalogManager catalog(&bpm);
    assert(catalog.GetInitializationStatus().ok());

    PageId first_page_id = INVALID_PAGE_ID;
    assert(
        catalog.GetTableFirstPageId(
            "usuarios",
            &first_page_id
        ).ok()
    );
    assert(first_page_id == expected_first_page_id);
    assert(catalog.HasIndex("usuarios", "id"));

    PageId index_header_page_id = INVALID_PAGE_ID;
    assert(
        catalog.GetIndexHeaderPageId(
            "usuarios",
            "id",
            &index_header_page_id
        ).ok()
    );
    assert(
        index_header_page_id ==
        expected_index_header_page_id
    );

    QueryExecutor executor(&catalog, &bpm);
    std::unique_ptr<SQLStatement> select =
        Parse(
            "SELECT id FROM usuarios "
            "WHERE id = 7777;"
        );

    QueryStats stats;
    const Status status = executor.Execute(
        *select,
        &stats
    );

    assert(status.ok());
    assert(stats.rows_returned == 1);
    assert(stats.records_examined == 1);
    assert(stats.pages_scanned == 1);

    return stats;
}

QueryStats RunSequentialQuery(
    const std::filesystem::path& database_path,
    PageId expected_first_page_id
) {
    DiskManager disk_manager(database_path.string());
    ClockReplacer replacer(BUFFER_FRAMES);
    BufferPoolManager bpm(
        BUFFER_FRAMES,
        &disk_manager,
        &replacer
    );

    CatalogManager catalog(&bpm);
    assert(catalog.GetInitializationStatus().ok());

    Schema schema({});
    PageId first_page_id = INVALID_PAGE_ID;

    assert(
        catalog.GetTableSchema(
            "usuarios",
            &schema
        ).ok()
    );
    assert(
        catalog.GetTableFirstPageId(
            "usuarios",
            &first_page_id
        ).ok()
    );
    assert(first_page_id == expected_first_page_id);

    QueryStats stats;
    bpm.ResetStats();

    std::unique_ptr<AbstractOperator> plan =
        std::make_unique<SeqScanOperator>(
            std::make_unique<HeapFile>(
                &bpm,
                first_page_id
            ),
            &stats
        );

    plan = std::make_unique<FilterOperator>(
        std::move(plan),
        schema,
        Condition{"id", "=", "7777"}
    );

    assert(plan->Open().ok());

    uint64_t rows_returned = 0;
    Record record;
    RecordID rid;

    while (plan->Next(&record, &rid)) {
        ++rows_returned;
    }

    assert(plan->Close().ok());
    assert(rows_returned == 1);
    assert(stats.records_examined == TOTAL_RECORDS);

    bpm.PopulateStats(&stats);
    stats.rows_returned = rows_returned;

    return stats;
}

void VerifyDirectPersistentLookup(
    const std::filesystem::path& database_path
) {
    DiskManager disk_manager(database_path.string());
    ClockReplacer replacer(BUFFER_FRAMES);
    BufferPoolManager bpm(
        BUFFER_FRAMES,
        &disk_manager,
        &replacer
    );

    CatalogManager catalog(&bpm);
    assert(catalog.GetInitializationStatus().ok());

    HashIndex* index =
        catalog.GetIndex("usuarios", "id");

    assert(index != nullptr);

    std::string encoded_key;
    assert(
        IndexKey::Encode(
            int32_t{TARGET_ID},
            &encoded_key
        ).ok()
    );

    std::vector<RecordID> results;
    assert(
        index->GetValue(
            encoded_key,
            &results
        ).ok()
    );
    assert(results.size() == 1);
}

} // namespace

int main() {
    const std::filesystem::path database_path =
        "data/sprint3_acceptance.db";

    std::filesystem::create_directories("data");
    std::filesystem::remove(database_path);

    PageId first_page_id = INVALID_PAGE_ID;
    PageId index_header_page_id = INVALID_PAGE_ID;

    CreateTableAndIndex(
        database_path,
        &first_page_id,
        &index_header_page_id
    );

    const QueryStats index_stats =
        RunIndexedQuery(
            database_path,
            first_page_id,
            index_header_page_id
        );

    const QueryStats sequential_stats =
        RunSequentialQuery(
            database_path,
            first_page_id
        );

    VerifyDirectPersistentLookup(database_path);

    assert(index_stats.records_examined <
           sequential_stats.records_examined);
    assert(index_stats.disk_reads <
           sequential_stats.disk_reads);

    std::cout
        << "Sprint 3 acceptance test passed.\n"
        << "Records: " << TOTAL_RECORDS << '\n'
        << "Buffer frames: " << BUFFER_FRAMES << '\n'
        << "Persistent catalog: OK\n"
        << "Persistent hash index: OK\n"
        << "Indexed records examined: "
        << index_stats.records_examined << '\n'
        << "Sequential records examined: "
        << sequential_stats.records_examined << '\n'
        << "Indexed disk reads: "
        << index_stats.disk_reads << '\n'
        << "Sequential disk reads: "
        << sequential_stats.disk_reads << '\n';

    return 0;
}
