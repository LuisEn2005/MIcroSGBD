#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "storage/disk_manager.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

using namespace minidbms;

int main() {
    // Configurar archivo temporal
    const std::filesystem::path test_dir = "data";
    const std::filesystem::path test_file = test_dir / "buffer_test.db";
    std::filesystem::create_directories(test_dir);
    std::filesystem::remove(test_file);

    // -------- Test 1: ClockReplacer básico --------
    {
        ClockReplacer replacer(5);

        // Inicialmente vacío
        assert(replacer.Size() == 0);

        // Unpin los frames 0, 2, 4
        replacer.Unpin(0);
        replacer.Unpin(2);
        replacer.Unpin(4);
        assert(replacer.Size() == 3);

        // Primera víctima: debe ser el frame 0 (mano comienza en 0)
        FrameId victim;
        bool ok = replacer.Victim(&victim);
        assert(ok);
        assert(victim == 0);
        assert(replacer.Size() == 2);

        // Segunda víctima: la mano ahora está en 1, pero el frame 1 no está en el reemplazador.
        // El siguiente en el reemplazador es 2, así que la víctima debe ser 2.
        ok = replacer.Victim(&victim);
        assert(ok);
        assert(victim == 2);   // Este comportamiento es el esperado según el algoritmo
        assert(replacer.Size() == 1);

        // Tercera víctima: debe ser 4 (el único que queda)
        ok = replacer.Victim(&victim);
        assert(ok);
        assert(victim == 4);
        assert(replacer.Size() == 0);
    }

    // -------- Test 2: BufferPoolManager con persistencia --------
    {
        DiskManager disk_manager(test_file.string());
        ClockReplacer replacer(3);
        BufferPoolManager bpm(3, &disk_manager, &replacer);

        PageId p1, p2, p3, p4;
        Page* page1 = bpm.NewPage(&p1);
        Page* page2 = bpm.NewPage(&p2);
        Page* page3 = bpm.NewPage(&p3);
        assert(page1 != nullptr && page2 != nullptr && page3 != nullptr);
        assert(p1 == 1 && p2 == 2 && p3 == 3);

        // Escribir datos en la página 1 y marcarla sucia
        const char test_data[] = "Hello, Buffer!";
        std::memcpy(page1->GetData(), test_data, sizeof(test_data));
        bpm.UnpinPage(p1, true);   // sucia y liberada

        // No unpin p2 ni p3 (permanecen fijadas)

        // Crear una cuarta página: debe reemplazar a p1 (la única no fijada y sucia)
        Page* page4 = bpm.NewPage(&p4);
        assert(page4 != nullptr);
        assert(p4 == 4);

        // Flush y cerrar
        bpm.FlushAllPages();

        // Reabrir el archivo y verificar que p1 se guardó correctamente
        DiskManager disk_manager2(test_file.string());
        ClockReplacer replacer2(3);
        BufferPoolManager bpm2(3, &disk_manager2, &replacer2);

        Page* page1_reloaded = bpm2.FetchPage(p1);
        assert(page1_reloaded != nullptr);
        assert(std::memcmp(page1_reloaded->GetData(), test_data, sizeof(test_data)) == 0);
        bpm2.UnpinPage(p1, false);
    }

    std::cout << "All buffer tests passed.\n";
    return 0;
}
