#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "catalog/catalog_manager.h"
#include "catalog/schema.h"
#include "index/index_key.h"
#include "query/executor.h"
#include "query/parser.h"
#include "query/query_result.h"
#include "query/tokenizer.h"
#include "storage/disk_manager.h"
#include "storage/table_storage.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace minidbms;

namespace {

constexpr std::size_t BUFFER_FRAMES = 3;
constexpr int32_t TOTAL_USERS = 10000;
constexpr int32_t TARGET_ID = 7777;
constexpr int32_t DELETED_ID = 3333;
constexpr int32_t NULL_EMAIL_ID = 9000;

Status ExecuteSQL(
    QueryExecutor& executor,
    const std::string& sql,
    QueryStats* stats = nullptr,
    QueryResult* result = nullptr
) {
    Tokenizer tokenizer(sql);
    Parser parser(tokenizer.Tokenize());

    std::unique_ptr<SQLStatement> statement;
    Status status = parser.Parse(&statement);
    if (!status.ok()) {
        return status;
    }

    return executor.Execute(*statement, stats, result);
}

std::string UserName(int32_t id) {
    return "User_" + std::to_string(id);
}

std::string UserEmail(int32_t id) {
    return "user_" + std::to_string(id) + "@example.com";
}

void AssertSingleIntResult(
    const QueryResult& result,
    int32_t expected
) {
    assert(result.rows.size() == 1);
    assert(result.rows[0].size() == 1);
    assert(std::get<int32_t>(result.rows[0][0]) == expected);
}

void AssertIndexContains(
    CatalogManager& catalog,
    const std::string& table,
    const std::string& column,
    const FieldValue& value,
    RecordID expected_rid
) {
    HashIndex* index = catalog.GetIndex(table, column);
    assert(index != nullptr);

    std::string key;
    assert(IndexKey::Encode(value, &key).ok());

    std::vector<RecordID> matches;
    assert(index->GetValue(key, &matches).ok());
    assert(matches.size() == 1);
    assert(matches[0] == expected_rid);
}

void AssertIndexDoesNotContain(
    CatalogManager& catalog,
    const std::string& table,
    const std::string& column,
    const FieldValue& value
) {
    HashIndex* index = catalog.GetIndex(table, column);
    assert(index != nullptr);

    std::string key;
    assert(IndexKey::Encode(value, &key).ok());

    std::vector<RecordID> matches;
    assert(index->GetValue(key, &matches).ok());
    assert(matches.empty());
}

} // namespace

int main() {
    const std::filesystem::path data_dir = "data";
    std::filesystem::create_directories(data_dir);

    const std::filesystem::path db_path =
        data_dir / "sprint6_final_acceptance.db";
    std::filesystem::remove(db_path);

    RecordID target_rid;
    RecordID deleted_rid;
    RecordID replacement_rid;
    RecordID audit_rid;
    RecordID null_email_rid;
    PageId reusable_page_id = INVALID_PAGE_ID;

    // -----------------------------------------------------------------
    // Fase 1: creación, carga, DML e índices con solo tres frames.
    // -----------------------------------------------------------------
    {
        DiskManager disk_manager(db_path.string());
        ClockReplacer replacer(BUFFER_FRAMES);
        BufferPoolManager bpm(
            BUFFER_FRAMES,
            &disk_manager,
            &replacer
        );
        CatalogManager catalog(&bpm);
        assert(catalog.GetInitializationStatus().ok());
        QueryExecutor executor(&catalog, &bpm);
        TableStorage storage(&bpm, &catalog);

        assert(ExecuteSQL(
            executor,
            "CREATE TABLE users ("
            "id INT, name VARCHAR(30), age INT, "
            "active BOOLEAN, email VARCHAR(50));"
        ).ok());

        assert(ExecuteSQL(
            executor,
            "CREATE TABLE audit (id INT, message VARCHAR(40));"
        ).ok());

        assert(ExecuteSQL(
            executor,
            "CREATE INDEX idx_users_id ON users(id);"
        ).ok());

        assert(ExecuteSQL(
            executor,
            "CREATE INDEX idx_users_email ON users(email);"
        ).ok());

        for (int32_t id = 1; id <= TOTAL_USERS; ++id) {
            std::vector<FieldValue> values = {
                id,
                UserName(id),
                static_cast<int32_t>(18 + (id % 60)),
                id % 2 == 0,
                id == NULL_EMAIL_ID
                    ? FieldValue{std::string{"null-user@example.com"}}
                    : FieldValue{UserEmail(id)}
            };

            RecordID rid;
            assert(storage.InsertRecord("users", values, &rid).ok());

            if (id == TARGET_ID) {
                target_rid = rid;
            }
            if (id == DELETED_ID) {
                deleted_rid = rid;
            }
            if (id == NULL_EMAIL_ID) {
                null_email_rid = rid;
            }
        }

        assert(target_rid.page_id > 0);
        assert(deleted_rid.page_id > 0);

        assert(storage.InsertRecord(
            "audit",
            {int32_t{1}, std::string{"audit-record"}},
            &audit_rid
        ).ok());

        // Un RID de otra tabla nunca debe aceptarse.
        assert(!storage.DeleteRecord("users", audit_rid).ok());

        QueryStats index_stats;
        QueryResult index_result;
        assert(ExecuteSQL(
            executor,
            "SELECT id FROM users WHERE id = 7777;",
            &index_stats,
            &index_result
        ).ok());
        assert(index_stats.scan_type == ScanType::HASH_INDEX);
        AssertSingleIntResult(index_result, TARGET_ID);

        QueryStats seq_stats;
        QueryResult seq_result;
        assert(ExecuteSQL(
            executor,
            "SELECT id FROM users WHERE name = 'User_7777';",
            &seq_stats,
            &seq_result
        ).ok());
        assert(seq_stats.scan_type == ScanType::SEQUENTIAL);
        AssertSingleIntResult(seq_result, TARGET_ID);
        assert(index_stats.records_examined < seq_stats.records_examined);

        // UPDATE de una clave indexada: se elimina la antigua y se agrega la nueva.
        QueryStats update_stats;
        assert(ExecuteSQL(
            executor,
            "UPDATE users SET email = 'updated_7777@example.com' "
            "WHERE id = 7777;",
            &update_stats
        ).ok());
        assert(update_stats.rows_affected == 1);

        AssertIndexDoesNotContain(
            catalog,
            "users",
            "email",
            UserEmail(TARGET_ID)
        );
        AssertIndexContains(
            catalog,
            "users",
            "email",
            std::string{"updated_7777@example.com"},
            target_rid
        );

        // Transición valor -> NULL en una columna indexada.
        AssertIndexContains(
            catalog,
            "users",
            "email",
            std::string{"null-user@example.com"},
            null_email_rid
        );

        assert(ExecuteSQL(
            executor,
            "UPDATE users SET email = NULL WHERE id = 9000;"
        ).ok());

        AssertIndexDoesNotContain(
            catalog,
            "users",
            "email",
            std::string{"null-user@example.com"}
        );

        // DELETE debe limpiar todos los índices.
        QueryStats delete_stats;
        assert(ExecuteSQL(
            executor,
            "DELETE FROM users WHERE id = 3333;",
            &delete_stats
        ).ok());
        assert(delete_stats.rows_affected == 1);

        AssertIndexDoesNotContain(
            catalog,
            "users",
            "id",
            int32_t{DELETED_ID}
        );
        AssertIndexDoesNotContain(
            catalog,
            "users",
            "email",
            UserEmail(DELETED_ID)
        );

        // La siguiente inserción debe reutilizar el slot liberado.
        assert(storage.InsertRecord(
            "users",
            {
                int32_t{10001},
                std::string{"Replacement"},
                int32_t{33},
                true,
                std::string{"replacement@example.com"}
            },
            &replacement_rid
        ).ok());
        assert(replacement_rid == deleted_rid);

        AssertIndexContains(
            catalog,
            "users",
            "id",
            int32_t{10001},
            replacement_rid
        );

        // Reutilización física de PageId.
        Page* temporary_page = bpm.NewPage(&reusable_page_id);
        assert(temporary_page != nullptr);
        assert(bpm.UnpinPage(reusable_page_id, false));
        assert(bpm.DeletePage(reusable_page_id));

        PageId immediate_reuse = INVALID_PAGE_ID;
        Page* reused_page = bpm.NewPage(&immediate_reuse);
        assert(reused_page != nullptr);
        assert(immediate_reuse == reusable_page_id);
        assert(bpm.UnpinPage(immediate_reuse, false));
        assert(bpm.DeletePage(immediate_reuse));

        assert(catalog.Flush().ok());
        bpm.FlushAllPages();
        assert(bpm.GetPinnedPageCount() == 0);
        assert(bpm.GetResidentPageCount() <= BUFFER_FRAMES);
        assert(disk_manager.GetFileSize() % PAGE_SIZE == 0);
    }

    // -----------------------------------------------------------------
    // Fase 2: reapertura completa y verificación de persistencia.
    // -----------------------------------------------------------------
    {
        DiskManager disk_manager(db_path.string());
        ClockReplacer replacer(BUFFER_FRAMES);
        BufferPoolManager bpm(
            BUFFER_FRAMES,
            &disk_manager,
            &replacer
        );
        CatalogManager catalog(&bpm);
        assert(catalog.GetInitializationStatus().ok());
        QueryExecutor executor(&catalog, &bpm);

        assert(catalog.HasIndex("users", "id"));
        assert(catalog.HasIndex("users", "email"));

        QueryStats index_stats;
        QueryResult index_result;
        assert(ExecuteSQL(
            executor,
            "SELECT id, email FROM users WHERE id = 7777;",
            &index_stats,
            &index_result
        ).ok());
        assert(index_stats.scan_type == ScanType::HASH_INDEX);
        assert(index_result.rows.size() == 1);
        assert(std::get<int32_t>(index_result.rows[0][0]) == TARGET_ID);
        assert(
            std::get<std::string>(index_result.rows[0][1]) ==
            "updated_7777@example.com"
        );

        QueryStats seq_stats;
        QueryResult seq_result;
        assert(ExecuteSQL(
            executor,
            "SELECT id FROM users WHERE name = 'User_7777';",
            &seq_stats,
            &seq_result
        ).ok());
        assert(seq_stats.scan_type == ScanType::SEQUENTIAL);
        AssertSingleIntResult(seq_result, TARGET_ID);

        assert(index_stats.records_examined < seq_stats.records_examined);
        assert(index_stats.disk_reads < seq_stats.disk_reads);

        QueryResult deleted_result;
        assert(ExecuteSQL(
            executor,
            "SELECT id FROM users WHERE id = 3333;",
            nullptr,
            &deleted_result
        ).ok());
        assert(deleted_result.rows.empty());

        QueryResult replacement_result;
        assert(ExecuteSQL(
            executor,
            "SELECT id FROM users WHERE id = 10001;",
            nullptr,
            &replacement_result
        ).ok());
        AssertSingleIntResult(replacement_result, 10001);

        QueryResult null_result;
        assert(ExecuteSQL(
            executor,
            "SELECT email FROM users WHERE id = 9000;",
            nullptr,
            &null_result
        ).ok());
        assert(null_result.rows.size() == 1);
        assert(std::holds_alternative<std::monostate>(
            null_result.rows[0][0]
        ));

        // La lista libre reconstruida también debe persistir.
        PageId reused_after_reopen = INVALID_PAGE_ID;
        Page* page = bpm.NewPage(&reused_after_reopen);
        assert(page != nullptr);
        assert(reused_after_reopen == reusable_page_id);
        assert(bpm.UnpinPage(reused_after_reopen, false));
        assert(bpm.DeletePage(reused_after_reopen));

        assert(bpm.GetPinnedPageCount() == 0);
        assert(disk_manager.GetFileSize() % PAGE_SIZE == 0);

        std::cout
            << "Sprint 6 final acceptance passed.\n"
            << "Records loaded: " << TOTAL_USERS << "\n"
            << "Buffer frames: " << BUFFER_FRAMES << "\n"
            << "Persistent catalog: OK\n"
            << "Persistent indexes: OK\n"
            << "Indexed update/delete maintenance: OK\n"
            << "NULL index transitions: OK\n"
            << "Slot reuse: OK\n"
            << "Persistent PageId reuse: OK\n"
            << "Pinned pages at end: "
            << bpm.GetPinnedPageCount() << "\n"
            << "Index records examined: "
            << index_stats.records_examined << "\n"
            << "SeqScan records examined: "
            << seq_stats.records_examined << "\n"
            << "Index disk reads: "
            << index_stats.disk_reads << "\n"
            << "SeqScan disk reads: "
            << seq_stats.disk_reads << "\n";
    }

    return 0;
}
