#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "catalog/catalog_manager.h"
#include "metrics/query_stats.h"
#include "query/executor.h"
#include "query/parser.h"
#include "query/tokenizer.h"
#include "storage/disk_manager.h"
#include "storage/record_codec.h"
#include "storage/slotted_page.h"
#include "storage/table_storage.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace minidbms;

namespace {

void SetupDataset(
    const std::filesystem::path& db_path,
    std::size_t record_count,
    std::size_t pool_size
) {
    std::filesystem::remove(db_path);

    DiskManager disk_manager(db_path.string());
    ClockReplacer replacer(pool_size);
    BufferPoolManager bpm(pool_size, &disk_manager, &replacer);
    CatalogManager catalog(&bpm);

    Schema schema({
        {"id", TypeId::INTEGER, 4},
        {"name", TypeId::VARCHAR, 30},
        {"age", TypeId::INTEGER, 4},
        {"active", TypeId::BOOLEAN, 1}
    });

    PageId first_page_id = INVALID_PAGE_ID;
    Page* page = bpm.NewPage(&first_page_id);
    assert(page != nullptr);
    SlottedPage slotted(page);
    slotted.Init();
    bpm.UnpinPage(first_page_id, true);

    assert(catalog.CreateTable("users", schema, first_page_id).ok());

    TableStorage table_storage(&bpm, &catalog);

    for (std::size_t i = 1; i <= record_count; ++i) {
        std::string name = "User_" + std::to_string(i);
        int32_t age = static_cast<int32_t>(18 + (i % 60));
        bool active = (i % 2 == 0);

        std::vector<FieldValue> values = {
            static_cast<int32_t>(i),
            name,
            age,
            active
        };

        RecordID rid;
        assert(table_storage.InsertRecord("users", values, &rid).ok());
    }

    QueryExecutor executor(&catalog, &bpm);

    // CREATE INDEX idx_users_id ON users(id);
    Tokenizer tokenizer("CREATE INDEX idx_users_id ON users(id);");
    Parser parser(tokenizer.Tokenize());
    std::unique_ptr<SQLStatement> stmt;
    assert(parser.Parse(&stmt).ok());
    assert(executor.Execute(*stmt, nullptr).ok());

    assert(catalog.Flush().ok());
    bpm.FlushAllPages();
}

void ExecuteAndRecord(
    QueryExecutor& executor,
    const std::string& sql,
    QueryStats* stats
) {
    Tokenizer tokenizer(sql);
    Parser parser(tokenizer.Tokenize());
    std::unique_ptr<SQLStatement> stmt;
    assert(parser.Parse(&stmt).ok());
    assert(executor.Execute(*stmt, stats).ok());
}

} // namespace

int main() {
    const std::filesystem::path data_dir = "data";
    const std::filesystem::path results_dir = "results";
    std::filesystem::create_directories(data_dir);
    std::filesystem::create_directories(results_dir);

    const std::filesystem::path db_path = data_dir / "sprint4_benchmark.db";
    constexpr std::size_t RECORD_COUNT = 10000;

    std::ofstream csv_out(results_dir / "sprint4_benchmark_results.csv");
    csv_out << "records,pool_size,query_type,cache_state,iteration,plan,rows_returned,records_examined,pages_scanned,buffer_hits,buffer_misses,disk_reads,disk_writes,time_ms\n";

    std::cout << "Running Sprint 4 Integrante 2 Benchmark Suite...\n";

    // 1. Prueba de Aceptación con 3 frames
    {
        SetupDataset(db_path, RECORD_COUNT, 3);

        DiskManager disk_manager(db_path.string());
        ClockReplacer replacer(3);
        BufferPoolManager bpm(3, &disk_manager, &replacer);
        CatalogManager catalog(&bpm);
        assert(catalog.GetInitializationStatus().ok());
        QueryExecutor executor(&catalog, &bpm);

        QueryStats index_stats;
        ExecuteAndRecord(executor, "SELECT * FROM users WHERE id = 7777;", &index_stats);

        // Sin índice (consulta de igualdad sobre columna no indexada)
        QueryStats seq_stats;
        ExecuteAndRecord(executor, "SELECT * FROM users WHERE name = 'User_7777';", &seq_stats);

        assert(index_stats.rows_returned == 1);
        assert(seq_stats.rows_returned == 1);
        assert(index_stats.scan_type == ScanType::HASH_INDEX);
        assert(seq_stats.scan_type == ScanType::SEQUENTIAL);

        assert(index_stats.records_examined < seq_stats.records_examined);
        assert(index_stats.disk_reads < seq_stats.disk_reads);

        std::cout << "[PASS] Acceptance test (10,000 records, 3 frames) verified successfully.\n"
                  << "  IndexScan disk reads: " << index_stats.disk_reads << " vs SeqScan disk reads: " << seq_stats.disk_reads << "\n"
                  << "  IndexScan records examined: " << index_stats.records_examined << " vs SeqScan records examined: " << seq_stats.records_examined << "\n";
    }

    // 2. Experimentos multivariados (Pool sizes: 3, 10, 50 | Iteraciones: 5 | Cold vs Hot cache)
    const std::vector<std::size_t> pool_sizes = {3, 10, 50};

    for (std::size_t pool_size : pool_sizes) {
        SetupDataset(db_path, RECORD_COUNT, pool_size);

        for (int iter = 1; iter <= 5; ++iter) {
            // Cold Cache setup
            DiskManager disk_manager(db_path.string());
            ClockReplacer replacer(pool_size);
            BufferPoolManager bpm(pool_size, &disk_manager, &replacer);
            CatalogManager catalog(&bpm);
            QueryExecutor executor(&catalog, &bpm);

            // Cold execution
            QueryStats cold_index;
            ExecuteAndRecord(executor, "SELECT * FROM users WHERE id = 7777;", &cold_index);
            csv_out << RECORD_COUNT << "," << pool_size << ",equality_indexed,cold," << iter << ","
                    << ScanTypeToString(cold_index.scan_type) << ","
                    << cold_index.rows_returned << "," << cold_index.records_examined << ","
                    << cold_index.pages_scanned << "," << cold_index.buffer_hits << ","
                    << cold_index.buffer_misses << "," << cold_index.disk_reads << ","
                    << cold_index.disk_writes << "," << cold_index.execution_time_ms << "\n";

            // Hot execution (Misma consulta en caché caliente)
            QueryStats hot_index;
            ExecuteAndRecord(executor, "SELECT * FROM users WHERE id = 7777;", &hot_index);
            csv_out << RECORD_COUNT << "," << pool_size << ",equality_indexed,hot," << iter << ","
                    << ScanTypeToString(hot_index.scan_type) << ","
                    << hot_index.rows_returned << "," << hot_index.records_examined << ","
                    << hot_index.pages_scanned << "," << hot_index.buffer_hits << ","
                    << hot_index.buffer_misses << "," << hot_index.disk_reads << ","
                    << hot_index.disk_writes << "," << hot_index.execution_time_ms << "\n";

            // SeqScan execution
            QueryStats seq_scan;
            ExecuteAndRecord(executor, "SELECT * FROM users WHERE name = 'User_7777';", &seq_scan);
            csv_out << RECORD_COUNT << "," << pool_size << ",equality_unindexed,cold," << iter << ","
                    << ScanTypeToString(seq_scan.scan_type) << ","
                    << seq_scan.rows_returned << "," << seq_scan.records_examined << ","
                    << seq_scan.pages_scanned << "," << seq_scan.buffer_hits << ","
                    << seq_scan.buffer_misses << "," << seq_scan.disk_reads << ","
                    << seq_scan.disk_writes << "," << seq_scan.execution_time_ms << "\n";

            // Range query execution (SeqScan)
            QueryStats range_scan;
            ExecuteAndRecord(executor, "SELECT * FROM users WHERE age > 40;", &range_scan);
            csv_out << RECORD_COUNT << "," << pool_size << ",range,cold," << iter << ","
                    << ScanTypeToString(range_scan.scan_type) << ","
                    << range_scan.rows_returned << "," << range_scan.records_examined << ","
                    << range_scan.pages_scanned << "," << range_scan.buffer_hits << ","
                    << range_scan.buffer_misses << "," << range_scan.disk_reads << ","
                    << range_scan.disk_writes << "," << range_scan.execution_time_ms << "\n";

            // Non-existent key (rows_returned = 0)
            QueryStats not_found;
            ExecuteAndRecord(executor, "SELECT * FROM users WHERE id = 999999;", &not_found);
            assert(not_found.rows_returned == 0);
            csv_out << RECORD_COUNT << "," << pool_size << ",not_found,cold," << iter << ","
                    << ScanTypeToString(not_found.scan_type) << ","
                    << not_found.rows_returned << "," << not_found.records_examined << ","
                    << not_found.pages_scanned << "," << not_found.buffer_hits << ","
                    << not_found.buffer_misses << "," << not_found.disk_reads << ","
                    << not_found.disk_writes << "," << not_found.execution_time_ms << "\n";
        }
    }

    csv_out.close();
    std::cout << "[SUCCESS] Sprint 4 Integrante 2 benchmark completed. Results written to results/sprint4_benchmark_results.csv\n";
    return 0;
}
