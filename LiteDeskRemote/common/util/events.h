#pragma once
// events.h — antrian peristiwa thread-safe untuk komunikasi thread pekerja → thread UI.
// UI meng-drain antrian pada WM_TIMER (lihat common/ui/window.cpp), tanpa pointer mentah
// lintas thread.
#include <mutex>
#include <utility>
#include <vector>

namespace ldr::util {

template <typename T>
class EventQueue {
public:
    void Push(T e) {
        std::lock_guard<std::mutex> lk(mu_);
        items_.push_back(std::move(e));
    }
    std::vector<T> Drain() {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<T> out;
        out.swap(items_);
        return out;
    }
    size_t Size() {
        std::lock_guard<std::mutex> lk(mu_);
        return items_.size();
    }
private:
    std::mutex mu_;
    std::vector<T> items_;
};

} // namespace ldr::util
