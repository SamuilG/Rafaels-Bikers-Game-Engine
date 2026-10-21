#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace engine {

// Reuse vacant ranges without moving any live mesh/material/texture index.
class ModelAssetSlots {
public:
    uint32_t Allocate(std::size_t count, std::size_t size) {
        if (!count) return 0;
        if (count > UINT32_MAX || size > UINT32_MAX - count)
            throw std::overflow_error("Model asset index space exhausted");
        for (auto it = mFree.begin(); it != mFree.end(); ++it) {
            if (it->count < count) continue;
            const auto first = it->first;
            it->first += static_cast<uint32_t>(count);
            it->count -= static_cast<uint32_t>(count);
            if (!it->count) mFree.erase(it);
            return first;
        }
        return static_cast<uint32_t>(size);
    }

    void Release(uint32_t first, std::size_t count) {
        if (!count) return;
        mFree.push_back({first, static_cast<uint32_t>(count)});
        std::sort(mFree.begin(), mFree.end(), [](auto a, auto b) { return a.first < b.first; });
        for (std::size_t i = 1; i < mFree.size();) {
            if (mFree[i - 1].first + mFree[i - 1].count == mFree[i].first) {
                mFree[i - 1].count += mFree[i].count;
                mFree.erase(mFree.begin() + i);
            } else ++i;
        }
    }

private:
    struct Range { uint32_t first, count; };
    std::vector<Range> mFree;
};

} // namespace engine
