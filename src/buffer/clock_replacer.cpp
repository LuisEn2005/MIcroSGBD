#include "buffer/clock_replacer.h"

#include <algorithm>

namespace minidbms {

ClockReplacer::ClockReplacer(std::size_t num_pages)
    : ref_flags_(num_pages, false),
      in_replacer_(num_pages, false) {}

bool ClockReplacer::Victim(FrameId* frame_id) {
    if (frame_id == nullptr || Size() == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(latch_);

    const std::size_t size = ref_flags_.size();
    // Recorremos todos los frames; si todos tienen bit true, los pondremos a false
    // y en la siguiente vuelta elegiremos el primero disponible.
    for (std::size_t count = 0; count < size; ++count) {
        if (in_replacer_[clock_hand_]) {
            if (ref_flags_[clock_hand_]) {
                ref_flags_[clock_hand_] = false;   // segunda oportunidad
            } else {
                // Seleccionamos este frame
                in_replacer_[clock_hand_] = false;
                *frame_id = static_cast<FrameId>(clock_hand_);
                // AVANZAMOS LA MANO al siguiente
                clock_hand_ = (clock_hand_ + 1) % size;
                return true;
            }
        }
        clock_hand_ = (clock_hand_ + 1) % size;
    }

    // Si llegamos aquí, todos los frames en el reemplazador tenían bit true
    // y fueron puestos a false. Ahora hacemos una segunda pasada.
    for (std::size_t count = 0; count < size; ++count) {
        if (in_replacer_[clock_hand_]) {
            // Ahora debe tener bit false (porque acabamos de ponerlos a false)
            in_replacer_[clock_hand_] = false;
            *frame_id = static_cast<FrameId>(clock_hand_);
            clock_hand_ = (clock_hand_ + 1) % size;
            return true;
        }
        clock_hand_ = (clock_hand_ + 1) % size;
    }

    // Nunca debería llegar aquí (pero por si acaso)
    return false;
}

void ClockReplacer::Pin(FrameId frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if (frame_id >= 0 && static_cast<std::size_t>(frame_id) < in_replacer_.size()) {
        in_replacer_[frame_id] = false;
        ref_flags_[frame_id] = false;
    }
}

void ClockReplacer::Unpin(FrameId frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if (frame_id >= 0 && static_cast<std::size_t>(frame_id) < in_replacer_.size()) {
        in_replacer_[frame_id] = true;
        ref_flags_[frame_id] = true;
    }
}

std::size_t ClockReplacer::Size() {
    std::lock_guard<std::mutex> lock(latch_);
    return std::count(in_replacer_.begin(), in_replacer_.end(), true);
}

} // namespace minidbms


