#ifndef MINI_DBMS_TYPES_H
#define MINI_DBMS_TYPES_H

#include <cstdint>
#include <cstddef>

namespace minidbms {
  using PageId = int32_t;
  using FrameId = int32_t;
  using SlotId = uint16_t;

  constexpr std::size_t PAGE_SIZE = 4096;

  constexpr PageId INVALID_PAGE_ID = -1;
  constexpr FrameId INVALID_FRAME_ID = -1;

  struct RecordID {
    PageId page_id{INVALID_PAGE_ID};
    SlotId slot_id{0};

    bool operator==(const RecordID& other) const {
      return page_id == other.page_id && slot_id == other.slot_id;
    }

    bool operator!=(const RecordID& other) const {
      return !(*this == other);
    }
  };

}

#endif // MINI_DBMS_TYPES_H
