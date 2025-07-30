//
// Created by Oscar Abreu on 7/29/25.
//

#ifndef CHUNK_H
#define CHUNK_H

#include <deque>
#include <curl/curl.h>
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <atomic>
#include <fstream>
#include <iostream>

struct Chunk {
    size_t offset_;
    size_t size_;
    std::vector<uint8_t> data_;
    bool downloaded_ = false;

    std::string filename() const {
        return "chunk_" + std::to_string(offset_) + ".ts";
    }

    void save_to_disk() const {
        std::ofstream out(filename(), std::ios::binary);
        out.write(reinterpret_cast<const char*>(data_.data()), data_.size());
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
        if (dq_.empty()) return std::nullopt;
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
        CURLcode res;
        for (int attempt = 0; attempt < 3; ++attempt) {
            res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                std::cerr << "Attempt # " << attempt << " CURL error at offset " << chunk.offset_ << ": "
                          << curl_easy_strerror(res) <<  std::endl;
            }
        }

            curl_easy_cleanup(curl);
            chunk.downloaded_ = (res == CURLE_OK);

            if (chunk.downloaded_ && has_idr_frame(chunk.data_)) {
                chunk.save_to_disk();
                return true;
            }

            return false;
        }
public:
    static bool has_idr_frame(const std::vector<uint8_t>& data) {
        constexpr size_t TS_PACKET_SIZE = 188;
        bool seen_aud = false;
        bool seen_sps = false;
        bool seen_pps = false;

        for (size_t i = 0; i + TS_PACKET_SIZE <= data.size(); i += TS_PACKET_SIZE) {
            if (data[i] != 0x47) continue; // Sync byte
            bool pusi = data[i + 1] & 0x40;
            if (!pusi) continue;

            size_t payload_start = i + 4;
            if (data[i + 3] & 0x20) {
                const uint8_t af_len = data[i + 4];
                payload_start += (1 + af_len);
            }

            for (size_t j = payload_start; j + 4 < i + TS_PACKET_SIZE; ++j) {
                if (data[j] == 0x00 && data[j + 1] == 0x00 && data[j + 2] == 0x01) {
                    const uint8_t nal_header = data[j + 3];
                    const uint8_t nal_type = nal_header & 0x1F;

                    switch (nal_type) {
                        case 9: seen_aud = true; break;
                        case 7: seen_sps = true; break;
                        case 8: seen_pps = true; break;
                        case 5:
                            if (seen_aud && seen_sps && seen_pps) return true;
                            break;
                        default: break;
                    }
                }
            }
        }

        return false;
    }
private:
    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* myData) {
        Chunk* chunk = static_cast<Chunk*>(myData);
        size_t total = size * nmemb;
        chunk->data_.insert(chunk->data_.end(), ptr, ptr + total);
        return total;
    }
};


#endif
