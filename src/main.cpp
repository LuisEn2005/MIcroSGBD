#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "catalog/catalog_manager.h"
#include "common/config.h"
#include "metrics/query_stats.h"
#include "query/executor.h"
#include "query/parser.h"
#include "query/query_result.h"
#include "query/tokenizer.h"
#include "storage/disk_manager.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace minidbms;

namespace {

void PrintQueryResult(const QueryResult& result) {
    if (result.column_names.empty()) {
        std::cout << "(0 columns)\n";
        return;
    }

    std::vector<std::size_t> widths;
    widths.reserve(result.column_names.size());

    for (const std::string& name : result.column_names) {
        widths.push_back(name.size());
    }

    std::vector<std::vector<std::string>> formatted_rows;
    formatted_rows.reserve(result.rows.size());

    for (const auto& row : result.rows) {
        std::vector<std::string> formatted;
        formatted.reserve(row.size());

        for (std::size_t index = 0;
             index < row.size();
             ++index) {
            std::string value = FieldValueToString(row[index]);
            formatted.push_back(value);

            if (index < widths.size()) {
                widths[index] = std::max(
                    widths[index],
                    value.size()
                );
            }
        }

        formatted_rows.push_back(std::move(formatted));
    }

    auto print_separator = [&]() {
        std::cout << '+';
        for (std::size_t width : widths) {
            std::cout << std::string(width + 2, '-') << '+';
        }
        std::cout << '\n';
    };

    print_separator();
    std::cout << '|';
    for (std::size_t index = 0;
         index < result.column_names.size();
         ++index) {
        std::cout << ' '
                  << std::left
                  << std::setw(static_cast<int>(widths[index]))
                  << result.column_names[index]
                  << " |";
    }
    std::cout << '\n';
    print_separator();

    for (const auto& row : formatted_rows) {
        std::cout << '|';
        for (std::size_t index = 0;
             index < widths.size();
             ++index) {
            const std::string value =
                index < row.size() ? row[index] : "";

            std::cout << ' '
                      << std::left
                      << std::setw(static_cast<int>(widths[index]))
                      << value
                      << " |";
        }
        std::cout << '\n';
    }

    print_separator();
    std::cout << result.rows.size() << " row(s).\n";
}

void PrintExplain(const QueryStats& stats) {
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
        << '\n';
}

} // namespace

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

        const auto* select_statement =
            dynamic_cast<const SelectStatement*>(statement.get());
        const bool is_select = select_statement != nullptr;
        const bool is_explain =
            is_select && select_statement->is_explain;

        QueryStats stats;
        QueryResult result;

        status = executor.Execute(
            *statement,
            &stats,
            is_select && !is_explain ? &result : nullptr
        );

        if (!status.ok()) {
            std::cerr
                << "[Error de ejecucion] "
                << status.message()
                << "\n\n";
            continue;
        }

        if (is_explain) {
            PrintExplain(stats);
        } else if (is_select) {
            PrintQueryResult(result);
        } else if (stats.rows_affected > 0) {
            std::cout
                << stats.rows_affected
                << " row(s) affected.\n";
        } else {
            std::cout << "Command completed successfully.\n";
        }

        std::cout << '\n';
    }

    return 0;
}
