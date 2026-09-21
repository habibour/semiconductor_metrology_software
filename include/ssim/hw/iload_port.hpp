#pragma once

// FR-HW-1: interface only today. FR-HW-6 (a simulated cassette load port
// with 1-25 slots) is not in Day 1's scope — no implementation exists yet.
//
// Thread-safety: to be documented by whichever implementation lands with
// FR-HW-6.

namespace ssim::hw {

class ILoadPort {
public:
    virtual ~ILoadPort() = default;

    virtual int slot_count() const = 0;
    virtual bool slot_occupied(int slot) const = 0;
    virtual const char* wafer_id_at(int slot) const = 0;
};

}  // namespace ssim::hw
