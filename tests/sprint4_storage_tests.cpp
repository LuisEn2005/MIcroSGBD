#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "catalog/catalog_manager.h"
#include "catalog/schema.h"
#include "index/hash_index.h"
#include "index/index_key.h"
#include "storage/disk_manager.h"
#include "storage/heap_file.h"
#include "storage/record_codec.h"
#include "storage/slotted_page.h"
#include "storage/table_storage.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace minidbms;

namespace {

Schema UsersSchema() {
    return Schema({
        {"id", TypeId::INTEGER, sizeof(int32_t)},
        {"email", TypeId::VARCHAR, 100},
        {"active", TypeId::BOOLEAN, 1}
    });
}

PageId CreateHeapFirstPage(BufferPoolManager* bpm) {
    PageId page_id = INVALID_PAGE_ID;
    Page* page = bpm->NewPage(&page_id);
    assert(page != nullptr);

    SlottedPage slotted_page(page);
    assert(slotted_page.Init().ok());
    assert(bpm->UnpinPage(page_id, true));

    return page_id;
}

std::string Encode(const FieldValue& value) {
    std::string key;
    assert(IndexKey::Encode(value, &key).ok());
    return key;
}

bool ContainsRid(
    const std::vector<RecordID>& values,
    RecordID expected
) {
    return std::find(
        values.begin(),
        values.end(),
        expected
    ) != values.end();
}

void ExpectIndexContains(
    HashIndex* index,
    const FieldValue& value,
    RecordID rid
) {
    assert(index != nullptr);

    std::vector<RecordID> results;
    assert(index->GetValue(Encode(value), &results).ok());
    assert(ContainsRid(results, rid));
}

void ExpectIndexDoesNotContain(
    HashIndex* index,
    const FieldValue& value,
    RecordID rid
) {
    assert(index != nullptr);

    std::vector<RecordID> results;
    assert(index->GetValue(Encode(value), &results).ok());
    assert(!ContainsRid(results, rid));
}

std::size_t CountRecords(
    BufferPoolManager* bpm,
    PageId first_page_id
) {
    HeapFile heap_file(bpm, first_page_id);

    Record record;
    RecordID rid;
    Status status = heap_file.GetFirstRecord(
        &record,
        &rid
    );

    if (status.code() == StatusCode::NOT_FOUND) {
        return 0;
    }

    assert(status.ok());
    std::size_t count = 1;

    while (true) {
        Record next_record;
        RecordID next_rid;

        status = heap_file.GetNextRecord(
            rid,
            &next_record,
            &next_rid
        );

        if (status.code() == StatusCode::NOT_FOUND) {
            break;
        }

        assert(status.ok());
        ++count;
        rid = next_rid;
    }

    return count;
}

std::vector<FieldValue> ReadValues(
    TableStorage* storage,
    const Schema& schema,
    const std::string& table_name,
    RecordID rid
) {
    Record record;
    assert(storage->GetRecord(
        table_name,
        rid,
        &record
    ).ok());

    std::vector<FieldValue> values;
    assert(RecordCodec::Deserialize(
        schema,
        record,
        &values
    ).ok());

    return values;
}

void CreateUsersIndexes(
    BufferPoolManager* bpm,
    CatalogManager* catalog
) {
    PageId id_header = INVALID_PAGE_ID;
    std::unique_ptr<HashIndex> id_index;

    assert(HashIndex::Create(
        bpm,
        8,
        &id_index,
        &id_header
    ).ok());

    assert(catalog->CreateIndex(
        "idx_users_id",
        "users",
        "id",
        std::move(id_index)
    ).ok());

    PageId email_header = INVALID_PAGE_ID;
    std::unique_ptr<HashIndex> email_index;

    assert(HashIndex::Create(
        bpm,
        8,
        &email_index,
        &email_header
    ).ok());

    assert(catalog->CreateIndex(
        "idx_users_email",
        "users",
        "email",
        std::move(email_index)
    ).ok());
}

} // namespace

int main() {
    const std::filesystem::path database_path =
        "data/sprint4_storage_test.db";

    std::filesystem::create_directories("data");
    std::filesystem::remove(database_path);

    constexpr int USER_COUNT = 180;
    const Schema schema = UsersSchema();

    PageId users_first_page = INVALID_PAGE_ID;
    PageId audit_first_page = INVALID_PAGE_ID;
    RecordID updated_rid;
    RecordID deleted_rid;
    RecordID reused_rid;

    // ============================================================
    // Fase 1: crear tablas, índices y mutaciones con 3 frames.
    // ============================================================
    {
        DiskManager disk_manager(database_path.string());
        ClockReplacer replacer(3);
        BufferPoolManager bpm(3, &disk_manager, &replacer);
        CatalogManager catalog(&bpm);

        assert(catalog.GetInitializationStatus().ok());

        users_first_page = CreateHeapFirstPage(&bpm);
        audit_first_page = CreateHeapFirstPage(&bpm);

        assert(catalog.CreateTable(
            "users",
            schema,
            users_first_page
        ).ok());

        assert(catalog.CreateTable(
            "audit",
            schema,
            audit_first_page
        ).ok());

        CreateUsersIndexes(&bpm, &catalog);

        TableStorage storage(&bpm, &catalog);
        std::vector<RecordID> user_rids;
        user_rids.reserve(USER_COUNT);

        for (int index = 0;
             index < USER_COUNT;
             ++index) {
            RecordID rid;
            const Status status = storage.InsertRecord(
                "users",
                {
                    static_cast<int32_t>(index),
                    std::string("user") +
                        std::to_string(index) +
                        "@example.com",
                    index % 2 == 0
                },
                &rid
            );

            assert(status.ok());
            user_rids.push_back(rid);
        }

        assert(CountRecords(
            &bpm,
            users_first_page
        ) == USER_COUNT);

        HashIndex* id_index = catalog.GetIndex(
            "users",
            "id"
        );
        HashIndex* email_index = catalog.GetIndex(
            "users",
            "email"
        );

        assert(id_index != nullptr);
        assert(email_index != nullptr);

        for (int index : {0, 42, 179}) {
            ExpectIndexContains(
                id_index,
                static_cast<int32_t>(index),
                user_rids[index]
            );

            ExpectIndexContains(
                email_index,
                std::string("user") +
                    std::to_string(index) +
                    "@example.com",
                user_rids[index]
            );
        }

        // Clave indexada demasiado larga: se rechaza antes de insertar.
        const std::size_t count_before_invalid =
            CountRecords(&bpm, users_first_page);

        RecordID invalid_rid;
        Status status = storage.InsertRecord(
            "users",
            {
                int32_t{9000},
                std::string(70, 'x'),
                true
            },
            &invalid_rid
        );

        assert(status.code() == StatusCode::INVALID_ARGUMENT);
        assert(invalid_rid.page_id == INVALID_PAGE_ID);
        assert(CountRecords(
            &bpm,
            users_first_page
        ) == count_before_invalid);

        // Actualizar dos columnas indexadas conservando el RID.
        updated_rid = user_rids[42];
        status = storage.UpdateRecord(
            "users",
            updated_rid,
            {
                int32_t{4200},
                std::string{"updated@example.com"},
                false
            }
        );
        assert(status.ok());

        ExpectIndexDoesNotContain(
            id_index,
            int32_t{42},
            updated_rid
        );
        ExpectIndexContains(
            id_index,
            int32_t{4200},
            updated_rid
        );
        ExpectIndexDoesNotContain(
            email_index,
            std::string{"user42@example.com"},
            updated_rid
        );
        ExpectIndexContains(
            email_index,
            std::string{"updated@example.com"},
            updated_rid
        );

        // Una actualización inválida no cambia ni HeapFile ni índice.
        status = storage.UpdateRecord(
            "users",
            updated_rid,
            {
                int32_t{9999},
                std::string(70, 'y'),
                true
            }
        );
        assert(status.code() == StatusCode::INVALID_ARGUMENT);

        const std::vector<FieldValue> unchanged = ReadValues(
            &storage,
            schema,
            "users",
            updated_rid
        );
        assert(std::get<int32_t>(unchanged[0]) == 4200);
        assert(std::get<std::string>(unchanged[1]) ==
               "updated@example.com");

        ExpectIndexContains(
            id_index,
            int32_t{4200},
            updated_rid
        );

        // Transición de VARCHAR a NULL: elimina la entrada del índice.
        status = storage.UpdateRecord(
            "users",
            updated_rid,
            {
                int32_t{4200},
                std::monostate{},
                true
            }
        );
        assert(status.ok());

        ExpectIndexDoesNotContain(
            email_index,
            std::string{"updated@example.com"},
            updated_rid
        );
        ExpectIndexContains(
            id_index,
            int32_t{4200},
            updated_rid
        );

        // Eliminar: primero se limpian los índices y luego el HeapFile.
        deleted_rid = user_rids[20];
        status = storage.DeleteRecord(
            "users",
            deleted_rid
        );
        assert(status.ok());

        Record deleted_record;
        status = storage.GetRecord(
            "users",
            deleted_rid,
            &deleted_record
        );
        assert(status.code() == StatusCode::NOT_FOUND);

        ExpectIndexDoesNotContain(
            id_index,
            int32_t{20},
            deleted_rid
        );
        ExpectIndexDoesNotContain(
            email_index,
            std::string{"user20@example.com"},
            deleted_rid
        );

        // La siguiente inserción debe reutilizar el slot liberado.
        status = storage.InsertRecord(
            "users",
            {
                int32_t{2020},
                std::string{"reused@example.com"},
                true
            },
            &reused_rid
        );
        assert(status.ok());
        assert(reused_rid == deleted_rid);

        ExpectIndexContains(
            id_index,
            int32_t{2020},
            reused_rid
        );

        // Un RID de otra tabla no puede modificar users.
        RecordID audit_rid;
        assert(storage.InsertRecord(
            "audit",
            {
                int32_t{1},
                std::string{"audit@example.com"},
                true
            },
            &audit_rid
        ).ok());

        status = storage.UpdateRecord(
            "users",
            audit_rid,
            {
                int32_t{2},
                std::string{"wrong-table@example.com"},
                false
            }
        );
        assert(status.code() == StatusCode::INVALID_ARGUMENT);

        status = storage.DeleteRecord(
            "users",
            audit_rid
        );
        assert(status.code() == StatusCode::INVALID_ARGUMENT);

        assert(catalog.Flush().ok());
        bpm.FlushAllPages();
    }

    // ============================================================
    // Fase 2: reabrir y verificar HeapFile, catálogo e índices.
    // ============================================================
    {
        DiskManager disk_manager(database_path.string());
        ClockReplacer replacer(3);
        BufferPoolManager bpm(3, &disk_manager, &replacer);
        CatalogManager catalog(&bpm);

        assert(catalog.GetInitializationStatus().ok());
        assert(catalog.HasIndex("users", "id"));
        assert(catalog.HasIndex("users", "email"));

        TableStorage storage(&bpm, &catalog);

        const std::vector<FieldValue> updated = ReadValues(
            &storage,
            schema,
            "users",
            updated_rid
        );

        assert(std::get<int32_t>(updated[0]) == 4200);
        assert(std::holds_alternative<std::monostate>(updated[1]));
        assert(std::get<bool>(updated[2]));

        HashIndex* id_index = catalog.GetIndex(
            "users",
            "id"
        );
        HashIndex* email_index = catalog.GetIndex(
            "users",
            "email"
        );

        ExpectIndexContains(
            id_index,
            int32_t{4200},
            updated_rid
        );
        ExpectIndexDoesNotContain(
            email_index,
            std::string{"updated@example.com"},
            updated_rid
        );
        ExpectIndexContains(
            id_index,
            int32_t{2020},
            reused_rid
        );
        ExpectIndexContains(
            email_index,
            std::string{"reused@example.com"},
            reused_rid
        );

        assert(CountRecords(
            &bpm,
            users_first_page
        ) == USER_COUNT);

        assert(bpm.GetPoolSize() == 3);
        assert(bpm.GetDiskReads() > 0);
    }

    std::cout
        << "Sprint 4 Integrante 1 tests passed.\n"
        << "Indexed inserts: " << USER_COUNT + 1 << '\n'
        << "Indexed update: OK\n"
        << "Indexed delete: OK\n"
        << "NULL index transition: OK\n"
        << "Slot reuse: OK\n"
        << "Cross-table RID protection: OK\n"
        << "Persistent reopen: OK\n"
        << "Buffer frames: 3\n";

    return 0;
}
