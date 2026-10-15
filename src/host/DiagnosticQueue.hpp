#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

namespace hksr::framework::detail {
struct DiagnosticRecord {
    std::uint64_t time=0;
    unsigned long thread=0;
    std::string text;
};
// Producers only copy values. The single consumer owns the output sink and
// performs IO without the queue lock. No callback can block on a file flush.
// Accepted entries are not discarded under congestion; allocation failures
// and sink failures are counted explicitly for the consumer to report.
class DiagnosticQueue {
    std::mutex mutex_;
    std::deque<DiagnosticRecord> queue_;
    std::atomic<std::uint64_t> lost_{0};
public:
    void Push(std::uint64_t time,unsigned long thread,const std::string& text) noexcept {
        try {std::lock_guard<std::mutex> lock(mutex_);queue_.push_back({time,thread,text});}
        catch(...){++lost_;}
    }
    template<class Sink> void Pump(Sink&& sink) noexcept {
        std::deque<DiagnosticRecord> batch;
        {std::lock_guard<std::mutex> lock(mutex_);batch.swap(queue_);}
        if(batch.empty())return;
        try{sink(batch);}catch(...){lost_+=static_cast<std::uint64_t>(batch.size());}
    }
    std::uint64_t Lost() const {return lost_.load();}
};
}
