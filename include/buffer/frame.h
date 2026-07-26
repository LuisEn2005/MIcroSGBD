#ifndef MINI_DBMS_FRAME_H
#define MINI_DBMS_FRAME_H

#include "../common/types.h"

namespace minidbms {
  struct Frame {
    FrameId frame_id{INVALID_FRAME_ID};
    PageId page_id{INVALID_PAGE_ID};
    int pin_count{0};
    bool is_dirty{false};
  };

}

#endif // MINI_DBMS_FRAME_H
