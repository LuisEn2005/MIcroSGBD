#ifndef MINI_DBMS_REPLACER_H
#define MINI_DBMS_REPLACER_H

#include "../common/types.h"

namespace minidbms {
  class Replacer {
    public:
      Replacer() = default;
      virtual ~Replacer() = default;

      virtual bool Victim(FrameId* frame_id) = 0;
      virtual void Pin(FrameId frame_id) = 0;
      virtual void Unpin(FrameId frame_id) = 0;
      virtual std::size_t Size() = 0;
  };

}

#endif // MINI_DBMS_REPLACER_H
