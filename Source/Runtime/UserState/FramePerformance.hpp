#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace engine {

struct CpuFrameTimings {
    double totalMs = 0.0;
    double cpuWorkMs = 0.0;
    double fenceWaitMs = 0.0;
    double acquireWaitMs = 0.0;
    double presentCallMs = 0.0;
};

// CPU frames and asynchronously completed GPU frames have independent histories.
// Missing/invalid measurements do not become zero-duration samples.
class FramePerformanceStatistics {
public:
    static constexpr std::size_t HistoryCapacity = 120;

    void AddCpuSample(const CpuFrameTimings& sample) {
        if (!Valid(sample.totalMs) || !Valid(sample.cpuWorkMs) ||
            !Valid(sample.fenceWaitMs) || !Valid(sample.acquireWaitMs) ||
            !Valid(sample.presentCallMs)) return;
        mCpu[mCpuNext] = sample;
        mCpuNext = (mCpuNext + 1) % HistoryCapacity;
        if (mCpuCount < HistoryCapacity) ++mCpuCount;
    }

    void AddGpuSample(double milliseconds) {
        if (!Valid(milliseconds)) return;
        mGpu[mGpuNext] = milliseconds;
        mGpuNext = (mGpuNext + 1) % HistoryCapacity;
        if (mGpuCount < HistoryCapacity) ++mGpuCount;
    }

    void Reset() { *this = FramePerformanceStatistics{}; }

    CpuFrameTimings CpuAverage() const {
        CpuFrameTimings result;
        if (mCpuCount == 0) return result;
        const double count = static_cast<double>(mCpuCount);
        for (std::size_t i = 0; i < mCpuCount; ++i) {
            result.totalMs += mCpu[i].totalMs / count;
            result.cpuWorkMs += mCpu[i].cpuWorkMs / count;
            result.fenceWaitMs += mCpu[i].fenceWaitMs / count;
            result.acquireWaitMs += mCpu[i].acquireWaitMs / count;
            result.presentCallMs += mCpu[i].presentCallMs / count;
        }
        return result;
    }

    double GpuAverageMs() const {
        if (mGpuCount == 0) return 0.0;
        double result = 0.0;
        const double count = static_cast<double>(mGpuCount);
        for (std::size_t i = 0; i < mGpuCount; ++i) result += mGpu[i] / count;
        return result;
    }

    std::size_t CpuSampleCount() const { return mCpuCount; }
    std::size_t GpuSampleCount() const { return mGpuCount; }

private:
    static bool Valid(double value) { return std::isfinite(value) && value >= 0.0; }
    std::array<CpuFrameTimings, HistoryCapacity> mCpu{};
    std::array<double, HistoryCapacity> mGpu{};
    std::size_t mCpuNext = 0;
    std::size_t mCpuCount = 0;
    std::size_t mGpuNext = 0;
    std::size_t mGpuCount = 0;
};

} // namespace engine
