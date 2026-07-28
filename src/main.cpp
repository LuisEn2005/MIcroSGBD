#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "catalog/catalog_manager.h"
#include "metrics/query_stats.h"
#include "query/executor.h"
#include "query/parser.h"
#include "query/tokenizer.h"
#include "storage/disk_manager.h"

#include <iostream>
#include <memory>
#include <string>

using namespace minidbms;

int main() {
    DiskManager disk_manager("minisgbd.db");
    ClockReplacer replacer(DEFAULT_BUFFER_POOL_SIZE);
    BufferPoolManager bpm(
        DEFAULT_BUFFER_POOL_SIZE,
        &disk_manager,
        &replacer
    );

    CatalogManager catalog(&bpm);

    if (!catalog.GetInitializationStatus().ok()) {
        std::cerr
            << "[Error de catalogo] "
            << catalog.GetInitializationStatus().message()
            << '\n';
        return 1;
    }

    QueryExecutor executor(&catalog, &bpm);
    std::string sql_input;

    while (true) {
        std::cout << "minidbms> ";

        if (!std::getline(std::cin, sql_input) ||
            sql_input == "exit;" ||
            sql_input == "exit") {
            std::cout << "Hasta luego.\n";
            break;
        }

        if (sql_input.empty()) {
            continue;
        }

        Tokenizer tokenizer(sql_input);
        Parser parser(tokenizer.Tokenize());

        std::unique_ptr<SQLStatement> statement;
        Status status = parser.Parse(&statement);

        if (!status.ok()) {
            std::cerr
                << "[Error sintactico] "
                << status.message()
                << "\n\n";
            continue;
        }

        QueryStats stats;
        status = executor.Execute(
            *statement,
            &stats
        );

        if (!status.ok()) {
            std::cerr
                << "[Error de ejecucion] "
                << status.message()
                << "\n\n";
            continue;
        }

        std::cout
            << "-> Plan: "
            << ScanTypeToString(stats.scan_type)
            << "\n   Execution time: "
            << stats.execution_time_ms
            << " ms\n   Disk reads: "
            << stats.disk_reads
            << "\n   Disk writes: "
            << stats.disk_writes
            << "\n   Buffer hits: "
            << stats.buffer_hits
            << "\n   Buffer misses: "
            << stats.buffer_misses
            << "\n   Pages scanned: "
            << stats.pages_scanned
            << "\n   Records examined: "
            << stats.records_examined
            << "\n   Rows returned: "
            << stats.rows_returned
            << "\n\n";
    }

    return 0;
}
