// rate_limiter.h — proteksi brute force autentikasi. Clock di-inject agar
// perilaku dapat diuji deterministik. Lintas-platform (bagian ldcore).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace ldr::sec {

struct RateLimiterOptions {
    int maxFails = 5;                    // kegagalan dalam window sebelum lockout
    int64_t windowMs = 10 * 60 * 1000;   // jendela sliding kegagalan
    int64_t baseLockMs = 30 * 1000;      // lockout pertama 30 s
    int64_t maxLockMs = 15 * 60 * 1000;  // lockout maksimum 15 menit
    int challengeMax = 10;               // budget challenge per jendela per IP
    int64_t challengeWindowMs = 60 * 1000;
};

class RateLimiter {
public:
    // nowFn: waktu monotonic ms; default = util::NowMs().
    explicit RateLimiter(RateLimiterOptions opt = RateLimiterOptions{},
                         std::function<int64_t()> nowFn = {})
        : opt_(opt), now_(nowFn ? std::move(nowFn) : DefaultNow) {}

    // true = boleh melanjutkan autentikasi. Setelah lockout berakhir, SATU percobaan
    // berikutnya diizinkan; bila gagal lagi, episode lockout baru (durasi ×2) dimulai.
    bool Allowed(const std::string& key) {
        PruneWindow(key);
        auto it = buckets_.find(key);
        if (it == buckets_.end()) return true;
        return it->second.lockedUntil <= now_();
    }

    void Fail(const std::string& key) {
        auto& b = buckets_[key];
        int64_t now = now_();
        bool wasLocked = b.lockedUntil > now;
        b.lastFail = now;
        b.fails++;
        // Episode lockout baru hanya saat threshold tercapai atau lockout lama habis.
        if (b.fails >= opt_.maxFails && !wasLocked) {
            int64_t dur = opt_.baseLockMs;
            for (int i = 0; i < b.lockCount && dur < opt_.maxLockMs; ++i) dur *= 2;
            if (dur > opt_.maxLockMs) dur = opt_.maxLockMs;
            b.lockCount++;
            b.lockedUntil = now + dur;
        }
    }

    void Success(const std::string& key) {
        auto it = buckets_.find(key);
        if (it != buckets_.end()) buckets_.erase(it);
    }

    // Budget challenge: cegah flood challenge PBKDF2 dari IP yang sama.
    bool ChallengeAllowed(const std::string& key) {
        int64_t now = now_();
        auto& b = buckets_[key];
        if (b.chWindowStart + opt_.challengeWindowMs < now) {
            b.chWindowStart = now;
            b.chCount = 0;
        }
        if (b.chCount >= opt_.challengeMax) return false;
        b.chCount++;
        return true;
    }

    void Reset(const std::string& key) { buckets_.erase(key); }
    size_t Size() { return buckets_.size(); }

private:
    struct Bucket {
        int fails = 0;
        int lockCount = 0;
        int64_t lastFail = 0;
        int64_t lockedUntil = 0;
        int64_t chWindowStart = 0;
        int chCount = 0;
    };
    void PruneWindow(const std::string& key) {
        auto it = buckets_.find(key);
        if (it == buckets_.end()) return;
        if (it->second.lastFail + opt_.windowMs < now_() &&
            it->second.lockedUntil <= now_()) {
            buckets_.erase(it);
        }
    }
    static int64_t DefaultNow();

    RateLimiterOptions opt_;
    std::function<int64_t()> now_;
    std::map<std::string, Bucket> buckets_;
};

} // namespace ldr::sec
