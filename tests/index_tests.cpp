#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "index/bucket_page.h"
#include "index/hash_index.h"
#include "index/hash_index_header_page.h"
#include "index/index_key.h"
#include "storage/disk_manager.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace minidbms;

namespace {

std::string KeyFor(int index) {
    return "key-" + std::to_string(index);
}

bool Contains(
    const std::vector<RecordID>& values,
    RecordID expected
) {
    return std::find(values.begin(), values.end(), expected) !=
           values.end();
}

std::size_t CountBucketChain(
    BufferPoolManager& bpm,
    PageId header_page_id
) {
    Page* header_page = bpm.FetchPage(header_page_id);
    assert(header_page != nullptr);

    HashIndexHeaderPage header(header_page);
    assert(header.IsInitialized());
    assert(header.GetBucketCount() == 1);

    PageId bucket_page_id = INVALID_PAGE_ID;
    Status status = header.GetBucketPageId(0, &bucket_page_id);
    assert(status.ok());
    assert(bucket_page_id != INVALID_PAGE_ID);
    assert(bpm.UnpinPage(header_page_id, false));

    std::size_t count = 0;
    PageId current_page_id = bucket_page_id;

    while (current_page_id != INVALID_PAGE_ID) {
        Page* page = bpm.FetchPage(current_page_id);
        assert(page != nullptr);

        HashIndexBucketPage bucket(page);
        assert(bucket.IsInitialized());

        const PageId next_page_id = bucket.GetOverflowPageId();
        assert(bpm.UnpinPage(current_page_id, false));

        ++count;
        current_page_id = next_page_id;
        assert(count < 10);
    }

    return count;
}

} // namespace

int main() {
    constexpr int UNIQUE_KEY_COUNT = 140;

    const std::filesystem::path data_directory = "data";
    const std::filesystem::path database_path =
        data_directory / "hash_index_test.db";

    std::filesystem::create_directories(data_directory);
    std::filesystem::remove(database_path);

    PageId index_header_page_id = INVALID_PAGE_ID;

    const RecordID duplicate_rid_1{5000, 1};
    const RecordID duplicate_rid_2{5001, 2};
    const RecordID duplicate_rid_3{5002, 3};

    // ============================================================
    // Fase 1: crear índice, forzar colisiones y desbordamiento
    // ============================================================
    {
        DiskManager disk_manager(database_path.string());
        ClockReplacer replacer(3);
        BufferPoolManager bpm(3, &disk_manager, &replacer);

        std::unique_ptr<HashIndex> index;
        Status status = HashIndex::Create(
            &bpm,
            1,
            &index,
            &index_header_page_id
        );

        assert(status.ok());
        assert(index != nullptr);
        assert(index_header_page_id == 1);

        uint16_t bucket_count = 0;
        status = index->GetBucketCount(&bucket_count);
        assert(status.ok());
        assert(bucket_count == 1);

        for (int value = 0; value < UNIQUE_KEY_COUNT; ++value) {
            status = index->Insert(
                KeyFor(value),
                RecordID{
                    static_cast<PageId>(1000 + value),
                    static_cast<SlotId>(value)
                }
            );
            assert(status.ok());
        }

        status = index->Insert("duplicate", duplicate_rid_1);
        assert(status.ok());
        status = index->Insert("duplicate", duplicate_rid_2);
        assert(status.ok());
        status = index->Insert("duplicate", duplicate_rid_3);
        assert(status.ok());

        // Insertar exactamente el mismo par no debe duplicarlo.
        status = index->Insert("duplicate", duplicate_rid_1);
        assert(status.ok());

        std::vector<RecordID> result;

        status = index->GetValue("duplicate", &result);
        assert(status.ok());
        assert(result.size() == 3);
        assert(Contains(result, duplicate_rid_1));
        assert(Contains(result, duplicate_rid_2));
        assert(Contains(result, duplicate_rid_3));

        status = index->GetValue(KeyFor(139), &result);
        assert(status.ok());
        assert(result.size() == 1);
        assert(result[0] == RecordID({1139, 139}));

        // Un bucket físico soporta varias decenas de entradas;
        // 143 pares con un solo bucket deben crear overflow.
        assert(CountBucketChain(bpm, index_header_page_id) >= 3);

        status = index->Remove("duplicate", duplicate_rid_2);
        assert(status.ok());

        status = index->GetValue("duplicate", &result);
        assert(status.ok());
        assert(result.size() == 2);
        assert(Contains(result, duplicate_rid_1));
        assert(!Contains(result, duplicate_rid_2));
        assert(Contains(result, duplicate_rid_3));

        status = index->Remove(KeyFor(50));
        assert(status.ok());

        status = index->GetValue(KeyFor(50), &result);
        assert(status.ok());
        assert(result.empty());

        const std::string oversized_key(
            HashIndexBucketPage::MAX_KEY_LENGTH + 1,
            'x'
        );
        status = index->Insert(oversized_key, {7000, 0});
        assert(status.code() == StatusCode::INVALID_ARGUMENT);

        // Contrato de codificación estable para el futuro IndexScan.
        std::string integer_key;
        status = IndexKey::Encode(int32_t{42}, &integer_key);
        assert(status.ok());
        assert(!integer_key.empty());

        std::string integer_key_again;
        status = IndexKey::Encode(int32_t{42}, &integer_key_again);
        assert(status.ok());
        assert(integer_key == integer_key_again);

        bpm.FlushAllPages();
    }

    assert(index_header_page_id == 1);
    assert(std::filesystem::file_size(database_path) >= 5 * PAGE_SIZE);
    assert(
        std::filesystem::file_size(database_path) % PAGE_SIZE == 0
    );

    // ============================================================
    // Fase 2: reabrir y comprobar persistencia física
    // ============================================================
    {
        DiskManager disk_manager(database_path.string());
        ClockReplacer replacer(3);
        BufferPoolManager bpm(3, &disk_manager, &replacer);

        std::unique_ptr<HashIndex> index;
        Status status = HashIndex::Open(
            &bpm,
            index_header_page_id,
            &index
        );

        assert(status.ok());
        assert(index != nullptr);

        std::vector<RecordID> result;

        status = index->GetValue(KeyFor(139), &result);
        assert(status.ok());
        assert(result.size() == 1);
        assert(result[0] == RecordID({1139, 139}));

        status = index->GetValue(KeyFor(50), &result);
        assert(status.ok());
        assert(result.empty());

        status = index->GetValue("duplicate", &result);
        assert(status.ok());
        assert(result.size() == 2);
        assert(Contains(result, duplicate_rid_1));
        assert(!Contains(result, duplicate_rid_2));
        assert(Contains(result, duplicate_rid_3));

        assert(CountBucketChain(bpm, index_header_page_id) >= 3);
        assert(bpm.GetDiskReads() > 0);
    }

    std::cout
        << "Hash index Sprint 3 tests passed.\n"
        << "Unique keys inserted: " << UNIQUE_KEY_COUNT << '\n'
        << "Duplicate RIDs persisted: 2\n"
        << "Buffer frames: 3\n"
        << "Index header page: " << index_header_page_id << '\n'
        << "Database size: "
        << std::filesystem::file_size(database_path)
        << " bytes\n";

    // ============================================================
    // Fase 3 (Sprint 3 - Integrante 2): Métricas I/O del Buffer Pool y Flush del Índice
    // ============================================================
    {
        const std::filesystem::path metrics_db =
            data_directory / "hash_index_metrics_test.db";
        std::filesystem::remove(metrics_db);

        DiskManager disk_manager(metrics_db.string());
        ClockReplacer replacer(4);
        BufferPoolManager bpm(4, &disk_manager, &replacer);

        PageId header_pid = INVALID_PAGE_ID;
        std::unique_ptr<HashIndex> index;

        Status status = HashIndex::Create(&bpm, 4, &index, &header_pid);
        assert(status.ok());

        bpm.ResetStats();

        for (int i = 0; i < 50; ++i) {
            status = index->Insert(
                KeyFor(i),
                RecordID{static_cast<PageId>(10 + i), static_cast<SlotId>(i)}
            );
            assert(status.ok());
        }

        uint64_t initial_writes = bpm.GetDiskWrites();
        uint64_t initial_misses = bpm.GetBufferMisses();
        assert(initial_misses > 0);

        uint64_t hits_before = bpm.GetBufferHits();
        std::vector<RecordID> search_res;
        status = index->GetValue(KeyFor(0), &search_res);
        assert(status.ok());
        assert(!search_res.empty());
        uint64_t hits_after = bpm.GetBufferHits();
        assert(hits_after >= hits_before);

        bpm.FlushAllPages();
        assert(bpm.GetDiskWrites() >= initial_writes);

        std::cout << "Sprint 3 Integrante 2: Index Buffer Pool I/O Metrics & Flush tests PASSED.\n";
    }

    return 0;
}
