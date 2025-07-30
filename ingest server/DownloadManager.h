#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include "Chunk.h"
#include "FetchPool.h"
#include <random>
#include <thread>
#include <fstream>



class S3DownloadManager {
    const size_t TS_PACKET_SIZE = 188;
    const size_t PACKETS_PER_CHUNK = 500;
    const size_t CHUNK_SIZE = TS_PACKET_SIZE * PACKETS_PER_CHUNK;
    const size_t max_workers = 5;

public:
    void run() {
        std::string url = pick_random_s3_url("objects.txt");
        curl_global_init(CURL_GLOBAL_ALL);
        ChunkQueue queue;
        std::atomic<size_t> current_offset{0};

        std::thread producer([&]() {
            while (!queue.is_shutdown()) {
                size_t tentative_offset = current_offset.fetch_add(CHUNK_SIZE);
                auto keyframe_offset = find_keyframe_offset(url, tentative_offset, CHUNK_SIZE * 2);
                if (keyframe_offset.has_value()) {
                    queue.push(Chunk{keyframe_offset.value(), CHUNK_SIZE});
                } else {
                    std::cerr << "No IDR frame found near offset " << tentative_offset << std::endl;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });


        FetcherPool pool(max_workers, queue, url);
        pool.join();
        producer.join();
        curl_global_cleanup();
    }

private:
    std::string pick_random_s3_url(const std::string& filename) {
        std::ifstream file(filename);
        std::vector<std::string> lines;
        for (std::string line; std::getline(file, line);)
            if (!line.empty()) lines.push_back(line);

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(0, lines.size() - 1);
        std::string chosen = lines[dist(gen)];
        return "https://livestreamx-cats.s3.amazonaws.com/" + chosen;
    }
    std::optional<size_t> find_keyframe_offset(const std::string& url, size_t start_offset, size_t scan_window) {
        Chunk probe_chunk{start_offset, scan_window};
        if (!ChunkFetcher::fetch(url, probe_chunk)) return std::nullopt;

        for (size_t i = 0; i + CHUNK_SIZE <= probe_chunk.data_.size(); i += TS_PACKET_SIZE) {
            std::vector<uint8_t> view(probe_chunk.data_.begin() + i, probe_chunk.data_.begin() + i + CHUNK_SIZE);
            if (ChunkFetcher::has_idr_frame(view)) {
                return start_offset + i;
            }
        }
        return std::nullopt;
    }
};

#endif