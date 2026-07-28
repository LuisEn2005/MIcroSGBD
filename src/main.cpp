#include <iostream>
#include <string>
#include <memory>

#include "query/tokenizer.h"
#include "query/parser.h"
#include "query/executor.h"
#include "metrics/query_stats.h"
#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "storage/disk_manager.h"
#include "catalog/catalog_manager.h"

using namespace minidbms;

int main() {
    DiskManager disk_manager("minisgbd.db");
    ClockReplacer replacer(10);
    BufferPoolManager bpm(10, &disk_manager, &replacer);
    CatalogManager catalog;
    QueryExecutor executor(&catalog, &bpm);

    std::string sql_input;

    while (true) {
        std::cout << "minidbms> ";
        if (!std::getline(std::cin, sql_input) || sql_input == "exit;" || sql_input == "exit") {
            std::cout << "¡Hasta luego!\n";
            break;
        }

        if (sql_input.empty()) {
            continue;
        }

        Tokenizer tokenizer(sql_input);
        auto tokens = tokenizer.Tokenize();

        Parser parser(tokens);
        std::unique_ptr<SQLStatement> stmt;
        Status parse_status = parser.Parse(&stmt);

        if (!parse_status.ok()) {
            std::cerr << "[Error sintactico] " << parse_status.message() << "\n\n";
            continue;
        }

        if (!stmt) {
            std::cerr << "[Error] No se pudo generar la sentencia SQL AST.\n\n";
            continue;
        }

        QueryStats stats;
        Status exec_status = executor.Execute(*stmt, &stats);

        if (!exec_status.ok()) {
            std::cerr << "[Error de ejecucion] " << exec_status.message() << "\n\n";
        } else {
            std::cout << "-> Consulta ejecutada correctamente.\n";
            std::cout << "   [Filas examinadas: " << stats.records_examined 
                << " | Tiempo: " << stats.execution_time_ms << " ms]\n\n";
        }
    }

    return 0;
}


