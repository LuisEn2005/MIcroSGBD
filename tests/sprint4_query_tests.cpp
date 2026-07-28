#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "catalog/catalog_manager.h"
#include "metrics/query_stats.h"
#include "query/executor.h"
#include "query/parser.h"
#include "query/tokenizer.h"
#include "storage/disk_manager.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace minidbms;

namespace {

Status ExecuteSQL(
    QueryExecutor& executor,
    const std::string& sql,
    QueryStats* stats = nullptr
) {
    Tokenizer tokenizer(sql);
    Parser parser(tokenizer.Tokenize());

    std::unique_ptr<SQLStatement> stmt;
    Status status = parser.Parse(&stmt);
    if (!status.ok()) {
        return status;
    }

    return executor.Execute(*stmt, stats);
}

} // namespace

int main() {
    const std::filesystem::path data_dir = "data";
    std::filesystem::create_directories(data_dir);
    const std::filesystem::path db_path = data_dir / "sprint4_query_test.db";
    std::filesystem::remove(db_path);

    std::cout << "Running Sprint 4 Integrante 3 Comprehensive Tests...\n";

    {
        DiskManager disk_manager(db_path.string());
        ClockReplacer replacer(10);
        BufferPoolManager bpm(10, &disk_manager, &replacer);
        CatalogManager catalog(&bpm);
        assert(catalog.GetInitializationStatus().ok());
        QueryExecutor executor(&catalog, &bpm);

        // 1. CREATE TABLE
        assert(ExecuteSQL(executor, "CREATE TABLE alumnos (id INT, nombre VARCHAR(30), edad INT);").ok());
        Schema schema({});
        assert(catalog.GetTableSchema("alumnos", &schema).ok());
        assert(schema.GetColumnCount() == 3);

        // 2. INSERT INTO
        assert(ExecuteSQL(executor, "INSERT INTO alumnos VALUES (1, 'Ana', 20);").ok());
        assert(ExecuteSQL(executor, "INSERT INTO alumnos VALUES (2, 'Carlos', 22);").ok());

        // 3. CREATE INDEX
        assert(ExecuteSQL(executor, "CREATE INDEX idx_alumnos_id ON alumnos(id);").ok());

        // 4. EXPLAIN ANALYZE SELECT (IndexScan)
        QueryStats index_stats;
        assert(ExecuteSQL(executor, "EXPLAIN ANALYZE SELECT * FROM alumnos WHERE id = 1;", &index_stats).ok());
        assert(index_stats.scan_type == ScanType::HASH_INDEX);
        assert(index_stats.rows_returned == 1);

        // 5. UPDATE
        QueryStats update_stats;
        assert(ExecuteSQL(executor, "UPDATE alumnos SET edad = 21 WHERE id = 1;", &update_stats).ok());
        assert(update_stats.rows_returned == 1);

        // Verificar el cambio de valor en SELECT
        QueryStats check_stats;
        assert(ExecuteSQL(executor, "SELECT * FROM alumnos WHERE id = 1;", &check_stats).ok());
        assert(check_stats.rows_returned == 1);

        // 6. DELETE
        QueryStats delete_stats;
        assert(ExecuteSQL(executor, "DELETE FROM alumnos WHERE id = 2;", &delete_stats).ok());
        assert(delete_stats.rows_returned == 1);

        QueryStats post_delete_stats;
        assert(ExecuteSQL(executor, "SELECT * FROM alumnos WHERE id = 2;", &post_delete_stats).ok());
        assert(post_delete_stats.rows_returned == 0);

        assert(catalog.Flush().ok());
        bpm.FlushAllPages();
    }

    // 7. Persistencia tras reapertura
    {
        DiskManager disk_manager(db_path.string());
        ClockReplacer replacer(10);
        BufferPoolManager bpm(10, &disk_manager, &replacer);
        CatalogManager catalog(&bpm);
        assert(catalog.GetInitializationStatus().ok());
        QueryExecutor executor(&catalog, &bpm);

        QueryStats stats;
        assert(ExecuteSQL(executor, "SELECT * FROM alumnos WHERE id = 1;", &stats).ok());
        assert(stats.rows_returned == 1);
        assert(stats.scan_type == ScanType::HASH_INDEX);
    }

    std::cout << "[SUCCESS] Sprint 4 Integrante 3 queries & EXPLAIN ANALYZE tests PASSED!\n";
    return 0;
}
