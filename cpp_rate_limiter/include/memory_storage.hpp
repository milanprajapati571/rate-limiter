#pragma once

#include <string>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <cmath>

class MemoryStorage {
public:
    static MemoryStorage& instance() {
        static MemoryStorage inst;
        return inst;
    }

    // --- 1. Fixed Window ---
    struct FixedWindowEntry {
        long long window_id{0};
        int count{0};
    };
    std::pair<int, bool> check_fixed_window(const std::string& key, long long window_id, int limit) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto& entry = fw_store_[key];
        if (entry.window_id != window_id) {
            entry.window_id = window_id;
            entry.count = 0;
        }
        entry.count++;
        return {entry.count, entry.count <= limit};
    }

    // --- 2. Sliding Window Log ---
    std::pair<int, long long> check_sliding_log(const std::string& key, long long now_ms, long long window_ms, int limit) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto& log = swlog_store_[key];
        long long cutoff = now_ms - window_ms;

        while (!log.empty() && log.front() <= cutoff) {
            log.pop_front();
        }

        if (static_cast<int>(log.size()) < limit) {
            log.push_back(now_ms);
            return {static_cast<int>(log.size()), 0};
        } else {
            long long oldest = log.empty() ? now_ms : log.front();
            long long retry_after_ms = std::max(1LL, (oldest + window_ms) - now_ms);
            return {static_cast<int>(log.size()), retry_after_ms};
        }
    }

    // --- 3. Sliding Window Counter ---
    struct SlidingCounterEntry {
        long long current_window_id{0};
        int current_count{0};
        int previous_count{0};
    };
    std::pair<int, bool> check_sliding_counter(const std::string& key, long long window_id, double overlap_ratio, int limit) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto& entry = swcnt_store_[key];
        if (entry.current_window_id != window_id) {
            if (entry.current_window_id == window_id - 1) {
                entry.previous_count = entry.current_count;
            } else {
                entry.previous_count = 0;
            }
            entry.current_count = 0;
            entry.current_window_id = window_id;
        }

        int estimated = static_cast<int>(std::floor(entry.previous_count * (1.0 - overlap_ratio) + entry.current_count));
        if (estimated < limit) {
            entry.current_count++;
            return {estimated + 1, true};
        } else {
            return {estimated, false};
        }
    }

    // --- 4. Token Bucket ---
    struct TokenBucketEntry {
        double tokens{0.0};
        double last_refill_sec{0.0};
    };
    std::pair<int, int> check_token_bucket(const std::string& key, double now_sec, double capacity, double refill_rate) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = tb_store_.find(key);
        if (it == tb_store_.end()) {
            tb_store_[key] = {capacity - 1.0, now_sec};
            return {static_cast<int>(std::floor(capacity - 1.0)), 0}; // 0 retry_after means allowed
        }

        auto& entry = it->second;
        double delta = std::max(0.0, now_sec - entry.last_refill_sec);
        entry.tokens = std::min(capacity, entry.tokens + delta * refill_rate);
        entry.last_refill_sec = now_sec;

        if (entry.tokens >= 1.0) {
            entry.tokens -= 1.0;
            return {static_cast<int>(std::floor(entry.tokens)), 0};
        } else {
            int retry_after = static_cast<int>(std::ceil((1.0 - entry.tokens) / refill_rate));
            return {0, std::max(1, retry_after)};
        }
    }

    // --- 5. Leaky Bucket ---
    struct LeakyBucketEntry {
        double water{0.0};
        double last_leak_sec{0.0};
    };
    std::pair<int, int> check_leaky_bucket(const std::string& key, double now_sec, double capacity, double leak_rate) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = lb_store_.find(key);
        if (it == lb_store_.end()) {
            lb_store_[key] = {1.0, now_sec};
            return {static_cast<int>(std::floor(capacity - 1.0)), 0};
        }

        auto& entry = it->second;
        double delta = std::max(0.0, now_sec - entry.last_leak_sec);
        entry.water = std::max(0.0, entry.water - delta * leak_rate);
        entry.last_leak_sec = now_sec;

        if (entry.water + 1.0 <= capacity) {
            entry.water += 1.0;
            return {static_cast<int>(std::floor(capacity - entry.water)), 0};
        } else {
            int retry_after = static_cast<int>(std::ceil((entry.water + 1.0 - capacity) / leak_rate));
            return {0, std::max(1, retry_after)};
        }
    }

private:
    MemoryStorage() = default;
    std::mutex mtx_;
    std::unordered_map<std::string, FixedWindowEntry> fw_store_;
    std::unordered_map<std::string, std::deque<long long>> swlog_store_;
    std::unordered_map<std::string, SlidingCounterEntry> swcnt_store_;
    std::unordered_map<std::string, TokenBucketEntry> tb_store_;
    std::unordered_map<std::string, LeakyBucketEntry> lb_store_;
};
