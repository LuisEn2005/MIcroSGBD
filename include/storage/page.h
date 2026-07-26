#ifndef MINI_DBMS_PAGE_H
#define MINI_DBMS_PAGE_H

#include "../common/types.h"

#include <cstring>
#include <shared_mutex>

namespace minidbms {

class Page {
public:
    Page() {
        ResetMemory();
    }

    ~Page() = default;

    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;

    char* GetData() {
        return data_;
    }

    const char* GetData() const {
        return data_;
    }

    PageId GetPageId() const {
        return page_id_;
    }

    int GetPinCount() const {
        return pin_count_;
    }

    bool IsDirty() const {
        return is_dirty_;
    }

    void SetPageId(PageId page_id) {
        page_id_ = page_id;
    }

    void SetDirty(bool is_dirty) {
        is_dirty_ = is_dirty;
    }

    void WLatch();
    void WUnlatch();

    void RLatch();
    void RUnlatch();

private:
    friend class BufferPoolManager;

    void ResetMemory() {
        std::memset(data_, 0, PAGE_SIZE);
        page_id_ = INVALID_PAGE_ID;
        pin_count_ = 0;
        is_dirty_ = false;
    }

    char data_[PAGE_SIZE];

    PageId page_id_{INVALID_PAGE_ID};
    int pin_count_{0};
    bool is_dirty_{false};

    mutable std::shared_mutex latch_;
};

} 

#endif // MINI_DBMS_PAGE_H
