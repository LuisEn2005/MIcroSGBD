#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "catalog/catalog_manager.h"
#include "query/executor.h"
#include "query/parser.h"
#include "query/query_result.h"
#include "query/tokenizer.h"
#include "storage/disk_manager.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

using namespace minidbms;

namespace {

Status ParseSQL(
    const std::string& sql,
    std::unique_ptr<SQLStatement>* statement
) {
    Tokenizer tokenizer(sql);
    Parser parser(tokenizer.Tokenize());
    return parser.Parse(statement);
}

Status ExecuteSQL(
    QueryExecutor& executor,
    const std::string& sql,
    QueryStats* stats = nullptr,
    QueryResult* result = nullptr
) {
    std::unique_ptr<SQLStatement> statement;
    Status status = ParseSQL(sql, &statement);
    if (!status.ok()) {
        return status;
    }

    return executor.Execute(*statement, stats, result);
}

} // namespace

int main() {
    const std::filesystem::path data_dir = "data";
    std::filesystem::create_directories(data_dir);

    const std::filesystem::path db_path =
        data_dir / "sprint5_stabilization.db";
    std::filesystem::remove(db_path);

    // El tokenizer/parser no debe aceptar caracteres desconocidos ni
    // cadenas sin cerrar.
    {
        std::unique_ptr<SQLStatement> statement;
        assert(!ParseSQL(
            "SELECT * FROM users WHERE id = 1 @;",
            &statement
        ).ok());
        assert(!ParseSQL(
            "INSERT INTO users VALUES (1, 'broken);",
            &statement
        ).ok());
        assert(!ParseSQL(
            "EXPLAIN SELECT * FROM users;",
            &statement
        ).ok());
    }

    PageId reused_page_id = INVALID_PAGE_ID;

    {
        DiskManager disk_manager(db_path.string());
        ClockReplacer replacer(3);
        BufferPoolManager bpm(3, &disk_manager, &replacer);
        CatalogManager catalog(&bpm);
        assert(catalog.GetInitializationStatus().ok());
        QueryExecutor executor(&catalog, &bpm);

        assert(ExecuteSQL(
            executor,
            "CREATE TABLE users ("
            "id INT, name VARCHAR(20), score INT, active BOOLEAN);"
        ).ok());

        // Enteros negativos, comillas SQL escapadas y NULL.
        QueryStats insert_stats;
        assert(ExecuteSQL(
            executor,
            "INSERT INTO users VALUES "
            "(-5, 'O''Brien', NULL, true);",
            &insert_stats
        ).ok());
        assert(insert_stats.rows_affected == 1);

        assert(ExecuteSQL(
            executor,
            "INSERT INTO users VALUES (2, 'Ana', 10, false);"
        ).ok());

        // Los tipos no deben convertirse silenciosamente desde cadenas.
        assert(!ExecuteSQL(
            executor,
            "INSERT INTO users VALUES ('3', 'Bad', 1, true);"
        ).ok());
        assert(!ExecuteSQL(
            executor,
            "INSERT INTO users VALUES (3, Bad, 1, true);"
        ).ok());

        // AND y resultados tipados/proyectados.
        QueryStats select_stats;
        QueryResult result;
        assert(ExecuteSQL(
            executor,
            "SELECT id, name, score FROM users "
            "WHERE id >= -5 AND active = true;",
            &select_stats,
            &result
        ).ok());

        assert(select_stats.rows_returned == 1);
        assert(result.column_names.size() == 3);
        assert(result.rows.size() == 1);
        assert(std::get<int32_t>(result.rows[0][0]) == -5);
        assert(std::get<std::string>(result.rows[0][1]) == "O'Brien");
        assert(std::holds_alternative<std::monostate>(result.rows[0][2]));

        QueryStats update_stats;
        assert(ExecuteSQL(
            executor,
            "UPDATE users SET score = 7 "
            "WHERE id = -5 AND active = true;",
            &update_stats
        ).ok());
        assert(update_stats.rows_affected == 1);
        assert(update_stats.rows_returned == 0);

        result.Reset();
        assert(ExecuteSQL(
            executor,
            "SELECT score FROM users WHERE id = -5;",
            nullptr,
            &result
        ).ok());
        assert(result.rows.size() == 1);
        assert(std::get<int32_t>(result.rows[0][0]) == 7);

        assert(ExecuteSQL(
            executor,
            "CREATE INDEX idx_users_id ON users(id);"
        ).ok());

        QueryStats explain_stats;
        assert(ExecuteSQL(
            executor,
            "EXPLAIN ANALYZE SELECT * FROM users WHERE id = -5;",
            &explain_stats
        ).ok());
        assert(explain_stats.scan_type == ScanType::HASH_INDEX);
        assert(explain_stats.rows_returned == 1);

        // CREATE TABLE duplicado no debe reservar una página huérfana.
        const PageId pages_before_duplicate_table =
            disk_manager.GetNumPages();
        assert(!ExecuteSQL(
            executor,
            "CREATE TABLE users (other INT);"
        ).ok());
        assert(
            disk_manager.GetNumPages() ==
            pages_before_duplicate_table
        );

        // Un nombre de índice duplicado tampoco debe crear páginas.
        const PageId pages_before_duplicate_index =
            disk_manager.GetNumPages();
        assert(!ExecuteSQL(
            executor,
            "CREATE INDEX idx_users_id ON users(name);"
        ).ok());
        assert(
            disk_manager.GetNumPages() ==
            pages_before_duplicate_index
        );

        // Las páginas desasignadas deben reutilizar su PageId.
        Page* temporary_page = bpm.NewPage(&reused_page_id);
        assert(temporary_page != nullptr);
        assert(bpm.UnpinPage(reused_page_id, false));
        assert(bpm.DeletePage(reused_page_id));

        const PageId high_water_mark = disk_manager.GetNumPages();
        PageId replacement_page_id = INVALID_PAGE_ID;
        Page* replacement_page = bpm.NewPage(&replacement_page_id);
        assert(replacement_page != nullptr);
        assert(replacement_page_id == reused_page_id);
        assert(disk_manager.GetNumPages() == high_water_mark);
        assert(bpm.UnpinPage(replacement_page_id, false));
        assert(bpm.DeletePage(replacement_page_id));

        assert(catalog.Flush().ok());
        bpm.FlushAllPages();
    }

    // La lista libre, el catálogo, los registros y el índice sobreviven
    // a una reapertura completa.
    {
        DiskManager disk_manager(db_path.string());
        ClockReplacer replacer(3);
        BufferPoolManager bpm(3, &disk_manager, &replacer);
        CatalogManager catalog(&bpm);
        assert(catalog.GetInitializationStatus().ok());
        QueryExecutor executor(&catalog, &bpm);

        QueryResult result;
        QueryStats stats;
        assert(ExecuteSQL(
            executor,
            "SELECT name, score FROM users WHERE id = -5;",
            &stats,
            &result
        ).ok());

        assert(stats.scan_type == ScanType::HASH_INDEX);
        assert(result.rows.size() == 1);
        assert(std::get<std::string>(result.rows[0][0]) == "O'Brien");
        assert(std::get<int32_t>(result.rows[0][1]) == 7);

        PageId replacement_page_id = INVALID_PAGE_ID;
        Page* replacement_page = bpm.NewPage(&replacement_page_id);
        assert(replacement_page != nullptr);
        assert(replacement_page_id == reused_page_id);
        assert(bpm.UnpinPage(replacement_page_id, false));
    }

    std::cout
        << "Sprint 5 stabilization tests passed.\n"
        << "Typed SQL literals: OK\n"
        << "SELECT result materialization: OK\n"
        << "AND predicates: OK\n"
        << "Affected-row accounting: OK\n"
        << "DDL cleanup: OK\n"
        << "Persistent page reuse: OK\n";

    return 0;
}
