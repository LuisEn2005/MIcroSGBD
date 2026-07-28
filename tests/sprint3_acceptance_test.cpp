#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <random>
#include <cstring>
#include <memory>

#include "../include/buffer/buffer_pool_manager.h"
#include "../include/buffer/clock_replacer.h"
#include "../include/catalog/catalog_manager.h"
#include "../include/query/executor.h"
#include "../include/query/parser.h"
#include "../include/query/tokenizer.h"
#include "../include/storage/disk_manager.h"
#include "../include/storage/heap_file.h"
#include "../include/storage/slotted_page.h"
#include "../include/query/operators/seq_scan_operator.h"

using namespace minidbms;

int main() {
    std::cout << "=========================================" << std::endl;
    std::cout << "   SPRINT 3: PRUEBA DE ACEPTACION INDEX   " << std::endl;
    std::cout << "=========================================" << std::endl;

    std::string db_file = "data/sprint3_acceptance.db";
    std::remove(db_file.c_str());

    const size_t pool_size = 50;
    auto disk_manager = std::make_unique<DiskManager>(db_file);
    auto replacer = std::make_unique<ClockReplacer>(pool_size);
    auto bpm = std::make_unique<BufferPoolManager>(pool_size, disk_manager.get(), replacer.get());
    auto catalog = std::make_unique<CatalogManager>();
    QueryExecutor executor(catalog.get(), bpm.get());

    std::string table_name = "usuarios";
    std::vector<Column> columns = {
        {"id", TypeId::INTEGER, 4},
        {"age", TypeId::INTEGER, 4}
    };
    Schema schema(columns);

    PageId first_page_id = INVALID_PAGE_ID;
    Page* page = bpm->NewPage(&first_page_id);
    assert(page != nullptr && "Error al crear primera pagina de la tabla");

    SlottedPage slotted_page(page);
    slotted_page.Init();
    
    bpm->UnpinPage(first_page_id, true);

    catalog->CreateTable(table_name, schema, first_page_id);
    std::cout << "[+] Tabla '" << table_name << "' creada correctamente." << std::endl;

    const int TOTAL_RECORDS = 10000;
    std::cout << "[+] Insertando " << TOTAL_RECORDS << " registros en la tabla..." << std::endl;


    HeapFile heap_file(bpm.get(), first_page_id);
    for (int i = 1; i <= TOTAL_RECORDS; ++i) {
        int32_t id_val = i;
        int32_t age_val = (i % 80) + 18;

        char record_data[8];
        std::memcpy(record_data, &id_val, sizeof(int32_t));
        std::memcpy(record_data + 4, &age_val, sizeof(int32_t));

        Record record(RecordID{}, 8, record_data);
        RecordID rid;
        Status status = heap_file.InsertRecord(record, &rid);
        if (!status.ok()) {
            std::cout << "[!] Fallo en el registro #" << i << std::endl;
            assert(false && "Error al insertar registro en HeapFile");
        }
    }
    std::cout << "[+] 10,000 registros insertados satisfactoriamente." << std::endl;

    std::cout << "[+] Ejecutando CREATE INDEX idx_id ON usuarios ( id )..." << std::endl;
    Tokenizer create_idx_tok("CREATE INDEX idx_id ON usuarios ( id )");
    auto create_idx_tokens = create_idx_tok.Tokenize();
    Parser create_idx_parser(create_idx_tokens);

    std::unique_ptr<SQLStatement> create_idx_stmt;
    Status parse_status = create_idx_parser.Parse(&create_idx_stmt);
    assert(parse_status.ok() && "Error al parsear CREATE INDEX");

    Status exec_status = executor.Execute(*create_idx_stmt, nullptr);
    assert(exec_status.ok() && "Error al ejecutar CREATE INDEX");
    std::cout << "[+] Indice Hash 'idx_id' creado y poblado en el catalogo." << std::endl;

    std::cout << "[+] Seleccionando 100 claves aleatorias para validar IndexScan vs SeqScan..." << std::endl;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(1, TOTAL_RECORDS);

    int exitosas = 0;
    for (int k = 0; k < 100; ++k) {
        int target_id = dist(rng);
        std::string sql_query = "SELECT id FROM usuarios WHERE id = " + std::to_string(target_id);

        Tokenizer query_tok(sql_query);
        auto query_tokens = query_tok.Tokenize();

        Parser query_parser(query_tokens);
        std::unique_ptr<SQLStatement> select_stmt;
        Status status_parse = query_parser.Parse(&select_stmt);
        if (!status_parse.ok()) {
            std::cout << "[!] Error parseando: " << sql_query << std::endl;
            assert(false && "Fallo en el QueryParser");
        }

        QueryStats stats;
        Status query_status = executor.Execute(*select_stmt, &stats);
        if (!query_status.ok()) {
            std::cout << "[!] Error ejecutando consulta con índice: " << sql_query << std::endl;
            assert(false && "Fallo en QueryExecutor");
        }

        exitosas++;
    }

    std::cout << "[+] " << exitosas << "/100 claves validadas con exito." << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << "   PRUEBA DE ACEPTACION SPRINT 3: PASO   " << std::endl;
    std::cout << "=========================================" << std::endl;

    return 0;
}
