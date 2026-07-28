#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "storage/disk_manager.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>

using namespace minidbms;

int main() {
    const std::filesystem::path test_directory = "data";
    const std::filesystem::path test_file =
        test_directory / "buffer_test.db";

    std::filesystem::create_directories(test_directory);
    std::filesystem::remove(test_file);

    // Test 1: funcionamiento básico de ClockReplacer.
    {
        ClockReplacer replacer(5);

        assert(replacer.Size() == 0);

        replacer.Unpin(0);
        replacer.Unpin(2);
        replacer.Unpin(4);
        assert(replacer.Size() == 3);

        FrameId victim = INVALID_FRAME_ID;

        assert(replacer.Victim(&victim));
        assert(victim == 0);

        assert(replacer.Victim(&victim));
        assert(victim == 2);

        assert(replacer.Victim(&victim));
        assert(victim == 4);

        assert(replacer.Size() == 0);
    }

    PageId persisted_page_id = INVALID_PAGE_ID;
    const char persisted_data[] = "Hello, Buffer!";

    // Test 2: escribir, expulsar una página sucia y cerrar el primer manager.
    {
        DiskManager disk_manager(test_file.string());
        ClockReplacer replacer(3);
        BufferPoolManager buffer_pool(3, &disk_manager, &replacer);

        PageId page_id_1 = INVALID_PAGE_ID;
        PageId page_id_2 = INVALID_PAGE_ID;
        PageId page_id_3 = INVALID_PAGE_ID;
        PageId page_id_4 = INVALID_PAGE_ID;

        Page* page_1 = buffer_pool.NewPage(&page_id_1);
        Page* page_2 = buffer_pool.NewPage(&page_id_2);
        Page* page_3 = buffer_pool.NewPage(&page_id_3);

        assert(page_1 != nullptr);
        assert(page_2 != nullptr);
        assert(page_3 != nullptr);
        assert(page_id_1 == 1);
        assert(page_id_2 == 2);
        assert(page_id_3 == 3);

        persisted_page_id = page_id_1;

        std::memcpy(
            page_1->GetData(),
            persisted_data,
            sizeof(persisted_data)
        );

        assert(buffer_pool.UnpinPage(page_id_1, true));

        // page_id_2 y page_id_3 continúan fijadas.
        // La cuarta página debe expulsar únicamente page_id_1.
        Page* page_4 = buffer_pool.NewPage(&page_id_4);
        assert(page_4 != nullptr);
        assert(page_id_4 == 4);

        assert(buffer_pool.UnpinPage(page_id_2, false));
        assert(buffer_pool.UnpinPage(page_id_3, false));
        assert(buffer_pool.UnpinPage(page_id_4, false));

        buffer_pool.FlushAllPages();
    }

    // Test 3: reabrir únicamente después de destruir el primer DiskManager.
    {
        DiskManager disk_manager(test_file.string());
        ClockReplacer replacer(3);
        BufferPoolManager buffer_pool(3, &disk_manager, &replacer);

        Page* reloaded_page = buffer_pool.FetchPage(persisted_page_id);
        assert(reloaded_page != nullptr);

        assert(
            std::memcmp(
                reloaded_page->GetData(),
                persisted_data,
                sizeof(persisted_data)
            ) == 0
        );

        assert(buffer_pool.UnpinPage(persisted_page_id, false));
    }

    // Test 4: NewPage no debe asignar una página física si todo está fijado.
    {
        const std::filesystem::path pinned_file =
            test_directory / "buffer_all_pinned_test.db";
        std::filesystem::remove(pinned_file);

        DiskManager disk_manager(pinned_file.string());
        ClockReplacer replacer(1);
        BufferPoolManager buffer_pool(1, &disk_manager, &replacer);

        PageId first_page_id = INVALID_PAGE_ID;
        Page* first_page = buffer_pool.NewPage(&first_page_id);
        assert(first_page != nullptr);
        assert(disk_manager.GetNumPages() == 2);

        PageId rejected_page_id = INVALID_PAGE_ID;
        Page* rejected_page = buffer_pool.NewPage(&rejected_page_id);

        assert(rejected_page == nullptr);
        assert(rejected_page_id == INVALID_PAGE_ID);
        assert(disk_manager.GetNumPages() == 2);

        assert(buffer_pool.UnpinPage(first_page_id, false));
    }

    // Test 5: Prueba de Aceptación Sprint 2 — 3 frames y 10 páginas con reemplazo efectivo y métricas
    {
        const std::filesystem::path sprint2_file =
            test_directory / "sprint2_acceptance.db";
        std::filesystem::remove(sprint2_file);

        std::vector<PageId> page_ids;
        {
            DiskManager disk_manager(sprint2_file.string());
            ClockReplacer replacer(3);
            BufferPoolManager buffer_pool(3, &disk_manager, &replacer);

            for (int i = 0; i < 10; ++i) {
                PageId pid = INVALID_PAGE_ID;
                Page* p = buffer_pool.NewPage(&pid);
                assert(p != nullptr);
                page_ids.push_back(pid);

                std::string content = "Sprint2_Page_Data_" + std::to_string(pid);
                std::memcpy(p->GetData(), content.c_str(), content.size() + 1);

                assert(buffer_pool.UnpinPage(pid, true));
            }

            assert(buffer_pool.GetDiskWrites() > 0);
            buffer_pool.FlushAllPages();
        }

        {
            DiskManager disk_manager(sprint2_file.string());
            ClockReplacer replacer(3);
            BufferPoolManager buffer_pool(3, &disk_manager, &replacer);

            for (PageId pid : page_ids) {
                Page* p = buffer_pool.FetchPage(pid);
                assert(p != nullptr);

                std::string expected = "Sprint2_Page_Data_" + std::to_string(pid);
                std::string actual(p->GetData());
                assert(actual == expected);

                assert(buffer_pool.UnpinPage(pid, false));
            }

            assert(buffer_pool.GetBufferMisses() == 10);
            assert(buffer_pool.GetDiskReads() == 10);
        }
    }

    std::cout << "All buffer tests passed.\n";
    return 0;
}
