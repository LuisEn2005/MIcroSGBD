#include "storage/page.h"

namespace minidbms {

void Page::WLatch() {
    latch_.lock();
}

void Page::WUnlatch() {
    latch_.unlock();
}

void Page::RLatch() {
    latch_.lock_shared();
}

void Page::RUnlatch() {
    latch_.unlock_shared();
}

} // namespace minidbms
