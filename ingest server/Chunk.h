//
// Created by Oscar Abreu on 7/29/25.
//

#include <deque>
#include <curl/curl.h>
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <atomic>
#include <iostream>

struct Chunk {
    size_t offset_;
    size_t size_;
    std::vector<uint8_t> data_;
    bool downloaded_ = false;

    std::string filename() const {
        return "chunk_" + std::to_string(offset_) + ".ts";
    }
};

class ChunkQueue {
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Chunk> dq_;

    std::atomic<bool> shutdown_{false};

public:
    void push(Chunk&& chunk) {
        std::lock_guard lock(mutex_);
        dq_.push_back(std::move(chunk));
        cv_.notify_one();
    }

    std::optional<Chunk> pop() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [&] { return !dq_.empty() || shutdown_; });
        if (dq_.empty()) return std::nullopt; // avoid popping on empty after shutdown
        Chunk chunk = std::move(dq_.front());
        dq_.pop_front();
        return chunk;
    }

    void shutdown() {
        shutdown_.store(true);
        cv_.notify_all();
    }

    bool is_shutdown() const { return shutdown_; }
};

class ChunkFetcher {
public:
    static bool fetch(const std::string& url, Chunk& chunk) {
        CURL* curl = curl_easy_init();
        if (!curl) return false;

        std::string range = std::to_string(chunk.offset_) + "-" + std::to_string(chunk.offset_ + chunk.size_ - 1);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        chunk.downloaded_ = (res == CURLE_OK);
        return chunk.downloaded_;
    }

private:
    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* myData) {
        Chunk* chunk = static_cast<Chunk*>(myData);
        size_t total = size * nmemb;
        chunk->data_.insert(chunk->data_.end(), ptr, ptr + total);
        return total;
    }
};
