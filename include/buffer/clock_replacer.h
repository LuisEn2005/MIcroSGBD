#ifndef MINI_DBMS_CLOCK_REPLACER_H
#define MINI_DBMS_CLOCK_REPLACER_H

#include "../buffer/replacer.h"
#include <vector>
#include <mutex>

namespace minidbms {
  class ClockReplacer : public Replacer {
    public:
      explicit ClockReplacer(std::size_t num_pages);
      ~ClockReplacer() override = default;

      bool Victim(FrameId* frame_id) override;
      void Pin(FrameId frame_id) override;
      void Unpin(FrameId frame_id) override;
      std::size_t Size() override;

    private:
      std::size_t clock_hand_{0};
      std::vector<bool> ref_flags_;
      std::vector<bool> in_replacer_;
      std::mutex latch_;
  };

}

#endif // MINI_DBMS_CLOCK_REPLACER_H
