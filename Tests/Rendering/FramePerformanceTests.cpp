#include "Runtime/UserState/FramePerformance.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Near(double actual, double expected) {
    Require(std::abs(actual - expected) < 1e-9, "unexpected timing average");
}
engine::CpuFrameTimings Sample(double multiplier) {
    return {16.0 * multiplier, 6.0 * multiplier, 1.0 * multiplier,
        7.0 * multiplier, 2.0 * multiplier};
}
void CheckAverage(const engine::CpuFrameTimings& sample, double multiplier) {
    const auto expected = Sample(multiplier);
    Near(sample.totalMs, expected.totalMs);
    Near(sample.cpuWorkMs, expected.cpuWorkMs);
    Near(sample.fenceWaitMs, expected.fenceWaitMs);
    Near(sample.acquireWaitMs, expected.acquireWaitMs);
    Near(sample.presentCallMs, expected.presentCallMs);
    Near(sample.totalMs, sample.cpuWorkMs + sample.fenceWaitMs + sample.acquireWaitMs + sample.presentCallMs);
}
void EmptyAndWarmup() {
    engine::FramePerformanceStatistics statistics;
    Require(statistics.CpuSampleCount() == 0 && statistics.GpuSampleCount() == 0, "missing samples must stay absent");
    CheckAverage(statistics.CpuAverage(), 0.0);
    Near(statistics.GpuAverageMs(), 0.0);
    statistics.AddCpuSample(Sample(1.0));
    statistics.AddCpuSample(Sample(3.0));
    CheckAverage(statistics.CpuAverage(), 2.0);
    Require(statistics.CpuSampleCount() == 2, "warmup must not average with unused slots");
    Require(statistics.GpuSampleCount() == 0, "CPU samples must not create GPU samples");
}
void IndependentCompletedGpuFrames() {
    engine::FramePerformanceStatistics statistics;
    for (int i = 0; i < 10; ++i) statistics.AddCpuSample(Sample(1.0));
    statistics.AddGpuSample(2.0);
    statistics.AddGpuSample(6.0);
    Require(statistics.GpuSampleCount() == 2 && statistics.CpuSampleCount() == 10, "histories must be independent");
    CheckAverage(statistics.CpuAverage(), 1.0);
    Near(statistics.GpuAverageMs(), 4.0);
}
void RollingWindow() {
    engine::FramePerformanceStatistics statistics;
    const auto capacity = engine::FramePerformanceStatistics::HistoryCapacity;
    Require(capacity == 120, "UI promises a maximum of 120 samples");
    for (std::size_t i = 1; i <= capacity * 3 + 7; ++i) {
        statistics.AddCpuSample(Sample(static_cast<double>(i)));
        statistics.AddGpuSample(static_cast<double>(i) * 0.25);
    }
    Require(statistics.CpuSampleCount() == capacity && statistics.GpuSampleCount() == capacity, "history must stay bounded");
    // Most recent 120 frames: 248 through 367.
    const double expected = (static_cast<double>(capacity * 2 + 8) + static_cast<double>(capacity * 3 + 7)) / 2.0;
    CheckAverage(statistics.CpuAverage(), expected);
    Near(statistics.GpuAverageMs(), expected * 0.25);
}
void InvalidSamples() {
    engine::FramePerformanceStatistics statistics;
    statistics.AddCpuSample(Sample(1.0));
    statistics.AddGpuSample(4.0);
    const double invalid[] = {-1.0, std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()};
    double engine::CpuFrameTimings::* fields[] = {
        &engine::CpuFrameTimings::totalMs, &engine::CpuFrameTimings::cpuWorkMs,
        &engine::CpuFrameTimings::fenceWaitMs, &engine::CpuFrameTimings::acquireWaitMs,
        &engine::CpuFrameTimings::presentCallMs};
    for (double value : invalid) {
        statistics.AddGpuSample(value);
        for (auto field : fields) {
            auto sample = Sample(10.0);
            sample.*field = value;
            statistics.AddCpuSample(sample);
        }
    }
    Require(statistics.CpuSampleCount() == 1 && statistics.GpuSampleCount() == 1, "invalid measurements must not replace valid samples");
    CheckAverage(statistics.CpuAverage(), 1.0);
    Near(statistics.GpuAverageMs(), 4.0);
    statistics.AddCpuSample({});
    statistics.AddGpuSample(0.0);
    Require(statistics.CpuSampleCount() == 2 && statistics.GpuSampleCount() == 2, "valid zero measurements must be accepted");
    CheckAverage(statistics.CpuAverage(), 0.5);
    Near(statistics.GpuAverageMs(), 2.0);
}
void ResetAndReuse() {
    engine::FramePerformanceStatistics statistics;
    for (int i = 0; i < 150; ++i) {
        statistics.AddCpuSample(Sample(2.0));
        statistics.AddGpuSample(10.0);
    }
    statistics.Reset();
    Require(statistics.CpuSampleCount() == 0 && statistics.GpuSampleCount() == 0, "reset must discard old samples");
    CheckAverage(statistics.CpuAverage(), 0.0);
    Near(statistics.GpuAverageMs(), 0.0);
    statistics.AddCpuSample(Sample(5.0));
    statistics.AddGpuSample(3.0);
    CheckAverage(statistics.CpuAverage(), 5.0);
    Near(statistics.GpuAverageMs(), 3.0);
}
}

int main() {
    try {
        EmptyAndWarmup();
        IndependentCompletedGpuFrames();
        RollingWindow();
        InvalidSamples();
        ResetAndReuse();
        std::cout << "PASS: 5 frame performance scenarios (warmup, asynchronous GPU, rollover, invalid data, reset)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
