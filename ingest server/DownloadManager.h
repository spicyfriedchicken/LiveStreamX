#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include "Chunk.h"
#include "FetchPool.h"
#include <random>
#include <thread>
#include <fstream>

class S3DownloadManager {
    const size_t TS_PACKET_SIZE = 188;
    const size_t PACKETS_PER_CHUNK = 1000;  // Larger chunks for better efficiency
    const size_t CHUNK_SIZE = TS_PACKET_SIZE * PACKETS_PER_CHUNK;

public:
    void run() {
        std::string url = pick_random_s3_url("objects.txt");
        curl_global_init(CURL_GLOBAL_ALL);
        ChunkQueue queue;
        std::atomic<size_t> current_offset{0};

        auto stream_info = analyze_stream_structure(url);
        if (!stream_info) {
            std::cerr << "Failed to analyze stream structure" << std::endl;
            return;
        }

        std::thread producer([&]() {
            while (!queue.is_shutdown()) {
                size_t offset = current_offset.fetch_add(CHUNK_SIZE);

                // Align to TS packet boundary
                offset = (offset / TS_PACKET_SIZE) * TS_PACKET_SIZE;

                auto keyframe_offset = find_keyframe_offset(url, offset, CHUNK_SIZE * 3);
                if (keyframe_offset) {
                    queue.push(Chunk{*keyframe_offset, CHUNK_SIZE});
                } else {
                    std::cerr << "No IDR frame found near offset " << offset << std::endl;
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });

        FetcherPool pool(5, queue, url);
        pool.join();
        producer.join();
        curl_global_cleanup();
    }

private:
    struct StreamInfo {
        uint16_t video_pid;
        size_t typical_gop_size_bytes;
        double frame_rate;
    };
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

    std::optional<StreamInfo> analyze_stream_structure(const std::string& url) {
        // fetch first 1MB to analyze structure
        Chunk probe{0, 1024 * 1024};
        if (!ChunkFetcher::fetch(url, probe)) return std::nullopt;

        StreamInfo info;
        auto video_pid = ChunkFetcher::find_video_pid(probe.data_);
        if (!video_pid) return std::nullopt;

        info.video_pid = *video_pid;
        return info;
    }

    std::optional<size_t> find_keyframe_offset(const std::string& url,
                                                       size_t start_offset,
                                                       size_t scan_window) {

        start_offset = (start_offset / TS_PACKET_SIZE) * TS_PACKET_SIZE;

        Chunk probe_chunk{start_offset, scan_window};
        if (!ChunkFetcher::fetch(url, probe_chunk)) return std::nullopt;

        for (size_t i = 0; i + CHUNK_SIZE <= probe_chunk.data_.size(); i += TS_PACKET_SIZE) {
            std::vector<uint8_t> view(probe_chunk.data_.begin() + i,
                                    probe_chunk.data_.begin() + i + CHUNK_SIZE);

            if (ChunkFetcher::has_idr_frame(view)) {
                return start_offset + i;
            }
        }
        return std::nullopt;
    }
};

#endif
