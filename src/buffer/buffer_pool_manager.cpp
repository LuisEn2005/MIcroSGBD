#include "buffer/buffer_pool_manager.h"

#include <array>
#include <cstring>
#include <stdexcept>

namespace minidbms {

BufferPoolManager::BufferPoolManager(
    std::size_t pool_size,
    DiskManager* disk_manager,
    Replacer* replacer
)
    : pool_size_(pool_size),
      disk_manager_(disk_manager),
      replacer_(replacer) {
    if (pool_size_ == 0) {
        throw std::invalid_argument(
            "Buffer pool size must be greater than zero"
        );
    }

    if (disk_manager_ == nullptr || replacer_ == nullptr) {
        throw std::invalid_argument(
            "DiskManager and Replacer cannot be null"
        );
    }

    pages_ = new Page[pool_size_];

    for (FrameId frame_id = 0;
         frame_id < static_cast<FrameId>(pool_size_);
         ++frame_id) {
        free_list_.push_back(frame_id);
    }
}

std::size_t BufferPoolManager::GetPinnedPageCount() const {
    std::lock_guard<std::mutex> lock(latch_);

    std::size_t pinned_pages = 0;
    for (const auto& entry : page_table_) {
        const FrameId frame_id = entry.second;
        if (pages_[frame_id].pin_count_ > 0) {
            ++pinned_pages;
        }
    }

    return pinned_pages;
}

std::size_t BufferPoolManager::GetResidentPageCount() const {
    std::lock_guard<std::mutex> lock(latch_);
    return page_table_.size();
}

BufferPoolManager::~BufferPoolManager() {
    FlushAllPages();
    delete[] pages_;
}

Page* BufferPoolManager::FetchPage(PageId page_id) {
    if (page_id < 0) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(latch_);

    const auto table_entry = page_table_.find(page_id);

    if (table_entry != page_table_.end()) {
        const FrameId frame_id = table_entry->second;
        Page& page = pages_[frame_id];

        ++page.pin_count_;
        replacer_->Pin(frame_id);

        ++buffer_hits_;
        return &page;
    }

    ++buffer_misses_;

    FrameId frame_id = INVALID_FRAME_ID;
    bool came_from_free_list = false;

    if (!free_list_.empty()) {
        frame_id = free_list_.front();
        free_list_.pop_front();
        came_from_free_list = true;
    } else if (!replacer_->Victim(&frame_id)) {
        return nullptr;
    }

    Page& frame_page = pages_[frame_id];
    std::array<char, PAGE_SIZE> page_data{};

    const Status read_status = disk_manager_->ReadPage(
        page_id,
        page_data.data()
    );

    if (!read_status.ok()) {
        if (came_from_free_list) {
            free_list_.push_front(frame_id);
        } else {
            replacer_->Unpin(frame_id);
        }

        return nullptr;
    }

    ++disk_reads_;

    if (!came_from_free_list) {
        if (frame_page.IsDirty()) {
            const Status write_status = disk_manager_->WritePage(
                frame_page.GetPageId(),
                frame_page.GetData()
            );

            if (!write_status.ok()) {
                replacer_->Unpin(frame_id);
                return nullptr;
            }

            ++disk_writes_;
        }

        page_table_.erase(frame_page.GetPageId());
    }

    frame_page.ResetMemory();
    std::memcpy(
        frame_page.GetData(),
        page_data.data(),
        PAGE_SIZE
    );

    frame_page.SetPageId(page_id);
    frame_page.pin_count_ = 1;
    frame_page.is_dirty_ = false;

    page_table_[page_id] = frame_id;
    replacer_->Pin(frame_id);

    return &frame_page;
}

bool BufferPoolManager::UnpinPage(PageId page_id, bool is_dirty) {
    std::lock_guard<std::mutex> lock(latch_);

    const auto table_entry = page_table_.find(page_id);

    if (table_entry == page_table_.end()) {
        return false;
    }

    const FrameId frame_id = table_entry->second;
    Page& page = pages_[frame_id];

    if (page.pin_count_ <= 0) {
        return false;
    }

    --page.pin_count_;
    page.is_dirty_ = page.is_dirty_ || is_dirty;

    if (page.pin_count_ == 0) {
        replacer_->Unpin(frame_id);
    }

    return true;
}

bool BufferPoolManager::FlushPage(PageId page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    const auto table_entry = page_table_.find(page_id);

    if (table_entry == page_table_.end()) {
        return false;
    }

    Page& page = pages_[table_entry->second];

    if (!page.IsDirty()) {
        return true;
    }

    const Status write_status = disk_manager_->WritePage(
        page_id,
        page.GetData()
    );

    if (!write_status.ok()) {
        return false;
    }

    ++disk_writes_;
    page.SetDirty(false);
    return true;
}

Page* BufferPoolManager::NewPage(PageId* page_id) {
    if (page_id == nullptr) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(latch_);

    FrameId frame_id = INVALID_FRAME_ID;
    bool came_from_free_list = false;

    if (!free_list_.empty()) {
        frame_id = free_list_.front();
        free_list_.pop_front();
        came_from_free_list = true;
    } else if (!replacer_->Victim(&frame_id)) {
        return nullptr;
    }

    Page& frame_page = pages_[frame_id];

    if (!came_from_free_list && frame_page.IsDirty()) {
        const Status write_status = disk_manager_->WritePage(
            frame_page.GetPageId(),
            frame_page.GetData()
        );

        if (!write_status.ok()) {
            replacer_->Unpin(frame_id);
            return nullptr;
        }

        ++disk_writes_;
    }

    // La página física se asigna recién después de confirmar que existe frame.
    // Así no se filtran páginas en disco cuando todo el pool está fijado.
    const PageId new_page_id = disk_manager_->AllocatePage();

    if (new_page_id == INVALID_PAGE_ID) {
        if (came_from_free_list) {
            free_list_.push_front(frame_id);
        } else {
            replacer_->Unpin(frame_id);
        }

        return nullptr;
    }

    // AllocatePage escribe físicamente una página vacía.
    ++disk_writes_;

    if (!came_from_free_list) {
        page_table_.erase(frame_page.GetPageId());
    }

    frame_page.ResetMemory();
    frame_page.SetPageId(new_page_id);
    frame_page.pin_count_ = 1;
    frame_page.is_dirty_ = false;

    page_table_[new_page_id] = frame_id;
    replacer_->Pin(frame_id);

    *page_id = new_page_id;
    return &frame_page;
}

bool BufferPoolManager::DeletePage(PageId page_id) {
    if (page_id <= HEADER_PAGE_ID) {
        return false;
    }

    std::lock_guard<std::mutex> lock(latch_);

    const auto table_entry = page_table_.find(page_id);

    if (table_entry != page_table_.end()) {
        const FrameId frame_id = table_entry->second;
        Page& page = pages_[frame_id];

        if (page.pin_count_ > 0) {
            return false;
        }

        replacer_->Pin(frame_id);
        page_table_.erase(table_entry);
        page.ResetMemory();
        free_list_.push_back(frame_id);
    }

    // También debe desasignarse si la página no estaba en el buffer.
    disk_manager_->DeallocatePage(page_id);
    return true;
}

void BufferPoolManager::FlushAllPages() {
    std::lock_guard<std::mutex> lock(latch_);

    for (const auto& table_entry : page_table_) {
        const PageId page_id = table_entry.first;
        const FrameId frame_id = table_entry.second;
        Page& page = pages_[frame_id];

        if (!page.IsDirty()) {
            continue;
        }

        const Status write_status = disk_manager_->WritePage(
            page_id,
            page.GetData()
        );

        if (write_status.ok()) {
            ++disk_writes_;
            page.SetDirty(false);
        }
    }
}

} // namespace minidbms
