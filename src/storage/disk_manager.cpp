#include "storage/disk_manager.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <ios>
#include <stdexcept>

namespace minidbms {

namespace {

std::streamoff GetPageOffset(PageId page_id) {
    return static_cast<std::streamoff>(page_id) *
           static_cast<std::streamoff>(PAGE_SIZE);
}

}

DiskManager::DiskManager(const std::string& db_file)
    : db_filename_(db_file) {

    db_io_.open(
        db_filename_,
        std::ios::binary |
        std::ios::in |
        std::ios::out
    );

    if (!db_io_.is_open()) {
        std::ofstream creator(
            db_filename_,
            std::ios::binary |
            std::ios::out
        );

        if (!creator.is_open()) {
            throw std::runtime_error(
                "Could not create database file: " + db_filename_
            );
        }

        creator.close();

        db_io_.open(
            db_filename_,
            std::ios::binary |
            std::ios::in |
            std::ios::out
        );
    }

    if (!db_io_.is_open()) {
        throw std::runtime_error(
            "Could not open database file: " + db_filename_
        );
    }

    db_io_.seekg(0, std::ios::end);
    const std::streamoff file_size = db_io_.tellg();
    db_io_.clear();

    if (file_size < 0) {
        throw std::runtime_error(
            "Could not determine database file size"
        );
    }

    if (file_size % static_cast<std::streamoff>(PAGE_SIZE) != 0) {
        throw std::runtime_error(
            "Database file size is not a multiple of PAGE_SIZE"
        );
    }

    num_pages_ = static_cast<PageId>(
        file_size / static_cast<std::streamoff>(PAGE_SIZE)
    );

    if (num_pages_ == 0) {
        std::array<char, PAGE_SIZE> empty_page{};

        const Status status = WritePage(0, empty_page.data());

        if (!status.ok()) {
            throw std::runtime_error(
                "Could not create header page: " + status.message()
            );
        }
    }

    // Las páginas desasignadas se representan como páginas completamente
    // vacías. Al abrir el archivo se reconstruye la lista libre sin cambiar
    // el formato físico ni depender de metadatos volátiles.
    std::array<char, PAGE_SIZE> page_buffer{};

    for (PageId page_id = 1; page_id < num_pages_; ++page_id) {
        db_io_.clear();
        db_io_.seekg(GetPageOffset(page_id), std::ios::beg);
        db_io_.read(
            page_buffer.data(),
            static_cast<std::streamsize>(PAGE_SIZE)
        );

        if (db_io_.gcount() != static_cast<std::streamsize>(PAGE_SIZE)) {
            db_io_.clear();
            throw std::runtime_error(
                "Could not scan database free pages"
            );
        }

        const bool is_zero_page = std::all_of(
            page_buffer.begin(),
            page_buffer.end(),
            [](char byte) { return byte == 0; }
        );

        if (is_zero_page) {
            free_pages_.insert(page_id);
        }
    }

    db_io_.clear();
}

DiskManager::~DiskManager() {
    std::lock_guard<std::mutex> guard(latch_);

    if (db_io_.is_open()) {
        db_io_.flush();
        db_io_.close();
    }
}

Status DiskManager::WritePage(
    PageId page_id,
    const char* page_data
) {
    if (page_id < 0) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Page id cannot be negative"
        );
    }

    if (page_data == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Page data cannot be null"
        );
    }

    std::lock_guard<std::mutex> guard(latch_);

    if (page_id > num_pages_) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Cannot write beyond the next available page"
        );
    }

    db_io_.clear();
    db_io_.seekp(GetPageOffset(page_id), std::ios::beg);

    if (!db_io_) {
        db_io_.clear();

        return Status::IOError(
            "Could not seek to page position"
        );
    }

    db_io_.write(
        page_data,
        static_cast<std::streamsize>(PAGE_SIZE)
    );

    db_io_.flush();

    if (!db_io_) {
        db_io_.clear();

        return Status::IOError(
            "Could not write page to disk"
        );
    }

    if (page_id == num_pages_) {
        ++num_pages_;
    }

    return Status::OK();
}

Status DiskManager::ReadPage(
    PageId page_id,
    char* page_data
) {
    if (page_id < 0) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Page id cannot be negative"
        );
    }

    if (page_data == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Destination buffer cannot be null"
        );
    }

    std::lock_guard<std::mutex> guard(latch_);

    if (page_id >= num_pages_) {
        return Status::NotFound(
            "Page does not exist"
        );
    }

    std::memset(page_data, 0, PAGE_SIZE);

    db_io_.clear();
    db_io_.seekg(GetPageOffset(page_id), std::ios::beg);

    if (!db_io_) {
        db_io_.clear();

        return Status::IOError(
            "Could not seek to page position"
        );
    }

    db_io_.read(
        page_data,
        static_cast<std::streamsize>(PAGE_SIZE)
    );

    if (db_io_.gcount() != static_cast<std::streamsize>(PAGE_SIZE)) {
        db_io_.clear();

        return Status::IOError(
            "Could not read a complete page"
        );
    }

    return Status::OK();
}

PageId DiskManager::AllocatePage() {
    std::lock_guard<std::mutex> guard(latch_);

    PageId new_page_id = num_pages_;

    if (!free_pages_.empty()) {
        const auto iterator = free_pages_.begin();
        new_page_id = *iterator;
        free_pages_.erase(iterator);
    }

    std::array<char, PAGE_SIZE> empty_page{};

    db_io_.clear();
    db_io_.seekp(GetPageOffset(new_page_id), std::ios::beg);

    if (!db_io_) {
        db_io_.clear();
        if (new_page_id < num_pages_) {
            free_pages_.insert(new_page_id);
        }
        return INVALID_PAGE_ID;
    }

    db_io_.write(
        empty_page.data(),
        static_cast<std::streamsize>(PAGE_SIZE)
    );

    db_io_.flush();

    if (!db_io_) {
        db_io_.clear();
        if (new_page_id < num_pages_) {
            free_pages_.insert(new_page_id);
        }
        return INVALID_PAGE_ID;
    }

    if (new_page_id == num_pages_) {
        ++num_pages_;
    }

    return new_page_id;
}

void DiskManager::DeallocatePage(PageId page_id) {
    if (page_id <= 0) {
        return;
    }

    std::lock_guard<std::mutex> guard(latch_);

    if (page_id >= num_pages_ ||
        free_pages_.find(page_id) != free_pages_.end()) {
        return;
    }

    std::array<char, PAGE_SIZE> empty_page{};

    db_io_.clear();
    db_io_.seekp(GetPageOffset(page_id), std::ios::beg);
    db_io_.write(
        empty_page.data(),
        static_cast<std::streamsize>(PAGE_SIZE)
    );
    db_io_.flush();

    if (db_io_) {
        free_pages_.insert(page_id);
    } else {
        db_io_.clear();
    }
}

PageId DiskManager::GetNumPages() const {
    std::lock_guard<std::mutex> guard(latch_);
    return num_pages_;
}

std::size_t DiskManager::GetFileSize() const {
    std::lock_guard<std::mutex> guard(latch_);

    return static_cast<std::size_t>(num_pages_) *
           PAGE_SIZE;
}

}
