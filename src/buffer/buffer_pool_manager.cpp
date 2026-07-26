#include "buffer/buffer_pool_manager.h"

#include <cstring>
#include <utility>

namespace minidbms {

BufferPoolManager::BufferPoolManager(std::size_t pool_size,
                                     DiskManager* disk_manager,
                                     Replacer* replacer)
    : pool_size_(pool_size),
      disk_manager_(disk_manager),
      replacer_(replacer) {
    pages_ = new Page[pool_size_];
    for (FrameId i = 0; i < static_cast<FrameId>(pool_size); ++i) {
        free_list_.push_back(i);
    }
}

BufferPoolManager::~BufferPoolManager() {
    FlushAllPages();
    delete[] pages_;
}

Page* BufferPoolManager::FetchPage(PageId page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        FrameId frame_id = it->second;
        pages_[frame_id].pin_count_++;
        replacer_->Pin(frame_id);
        return &pages_[frame_id];
    }

    FrameId frame_id = INVALID_FRAME_ID;
    if (!free_list_.empty()) {
        frame_id = free_list_.front();
        free_list_.pop_front();
    } else {
        if (!replacer_->Victim(&frame_id)) {
            return nullptr;
        }
        Page& victim_page = pages_[frame_id];
        if (victim_page.IsDirty()) {
            Status s = disk_manager_->WritePage(victim_page.GetPageId(),
                                                victim_page.GetData());
            if (!s.ok()) {
                return nullptr;
            }
            victim_page.SetDirty(false);
        }
        page_table_.erase(victim_page.GetPageId());
    }

    Page& new_page = pages_[frame_id];
    new_page.ResetMemory();
    Status s = disk_manager_->ReadPage(page_id, new_page.GetData());
    if (!s.ok()) {
        free_list_.push_back(frame_id);
        return nullptr;
    }
    new_page.SetPageId(page_id);
    new_page.pin_count_ = 1;
    new_page.is_dirty_ = false;

    page_table_[page_id] = frame_id;
    replacer_->Pin(frame_id);

    return &new_page;
}

bool BufferPoolManager::UnpinPage(PageId page_id, bool is_dirty) {
    std::lock_guard<std::mutex> lock(latch_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return false;
    }
    FrameId frame_id = it->second;
    Page& page = pages_[frame_id];
    if (page.pin_count_ <= 0) {
        return false;
    }
    page.pin_count_--;
    if (is_dirty) {
        page.is_dirty_ = true;
    }
    if (page.pin_count_ == 0) {
        replacer_->Unpin(frame_id);
    }
    return true;
}

bool BufferPoolManager::FlushPage(PageId page_id) {
    std::lock_guard<std::mutex> lock(latch_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return false;
    }
    FrameId frame_id = it->second;
    Page& page = pages_[frame_id];
    if (page.IsDirty()) {
        Status s = disk_manager_->WritePage(page_id, page.GetData());
        if (!s.ok()) {
            return false;
        }
        page.SetDirty(false);
    }
    return true;
}

Page* BufferPoolManager::NewPage(PageId* page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    PageId new_page_id = disk_manager_->AllocatePage();
    if (new_page_id == INVALID_PAGE_ID) {
        return nullptr;
    }

    FrameId frame_id = INVALID_FRAME_ID;
    if (!free_list_.empty()) {
        frame_id = free_list_.front();
        free_list_.pop_front();
    } else {
        if (!replacer_->Victim(&frame_id)) {
            return nullptr;
        }
        Page& victim_page = pages_[frame_id];
        if (victim_page.IsDirty()) {
            Status s = disk_manager_->WritePage(victim_page.GetPageId(),
                                                victim_page.GetData());
            if (!s.ok()) {
                return nullptr;
            }
            victim_page.SetDirty(false);
        }
        page_table_.erase(victim_page.GetPageId());
    }

    Page& new_page = pages_[frame_id];
    new_page.ResetMemory();
    new_page.SetPageId(new_page_id);
    new_page.pin_count_ = 1;
    new_page.is_dirty_ = false;

    page_table_[new_page_id] = frame_id;
    replacer_->Pin(frame_id);

    *page_id = new_page_id;
    return &new_page;
}

bool BufferPoolManager::DeletePage(PageId page_id) {
    std::lock_guard<std::mutex> lock(latch_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return true;
    }
    FrameId frame_id = it->second;
    Page& page = pages_[frame_id];
    if (page.pin_count_ > 0) {
        return false;
    }
    page.ResetMemory();
    page_table_.erase(it);
    replacer_->Pin(frame_id);
    free_list_.push_back(frame_id);
    disk_manager_->DeallocatePage(page_id);
    return true;
}

void BufferPoolManager::FlushAllPages() {
    std::lock_guard<std::mutex> lock(latch_);
    for (const auto& entry : page_table_) {
        PageId page_id = entry.first;
        FrameId frame_id = entry.second;
        Page& page = pages_[frame_id];
        if (page.IsDirty()) {
            disk_manager_->WritePage(page_id, page.GetData());
            page.SetDirty(false);
        }
    }
}

} // namespace minidbms
