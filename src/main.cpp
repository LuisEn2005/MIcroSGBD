#include <iostream>
#include <string>
#include "query/tokenizer.h"
#include "query/parser.h"
#include "query/executor.h"

using namespace minidbms;

void PrintStats(const QueryStats& stats) {
    std::cout << "\n--- Statistics ---" << std::endl;
    std::cout << "Execution Time   : " << stats.execution_time_ms << " ms" << std::endl;
    std::cout << "Buffer Hits      : " << stats.buffer_hits << std::endl;
    std::cout << "Buffer Misses    : " << stats.buffer_misses << std::endl;
    std::cout << "Disk Reads       : " << stats.disk_reads << std::endl;
    std::cout << "Disk Writes      : " << stats.disk_writes << std::endl;
    std::cout << "Pages Scanned    : " << stats.pages_scanned << std::endl;
    std::cout << "Records Examined : " << stats.records_examined << std::endl;
    std::cout << "------------------\n" << std::endl;
}

int main() {
    std::string input;
    std::cout << "Mini-SGBD CLI v0.1.0" << std::endl;
    std::cout << "Escribe 'exit' para salir.\n" << std::endl;

    while (true) {
        std::cout << "minisgbd> ";
        std::getline(std::cin, input);

        if (input == "exit") break;
        if (input.empty()) continue;

        bool is_explain = false;
        if (input.rfind("EXPLAIN ANALYZE", 0) == 0) {
            is_explain = true;
            input = input.substr(16);
        }

        Tokenizer tokenizer(input);
        auto tokens = tokenizer.Tokenize();

        Parser parser(tokens);
        std::unique_ptr<SQLStatement> stmt;
        Status status = parser.Parse(&stmt);

        if (!status.ok()) {
            std::cerr << "Error de Sintaxis: " << status.message() << std::endl;
            continue;
        }

        QueryStats stats;
        // Aquí invocas el QueryExecutor pasándole 'stmt' y '&stats'

        if (is_explain) {
            PrintStats(stats);
        }
    }

    return 0;
}
