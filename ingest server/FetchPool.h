#ifndef FETCHPOOL_H
#define FETCHPOOL_H
#include <fstream>
#include <thread>
#include "Chunk.h"

class FetcherPool {
    friend class ChunkFetcher;
    std::vector<std::thread> workers_;
    ChunkQueue& queue_;
    std::string url_;

public:
    FetcherPool(int num_workers, ChunkQueue& queue, const std::string& url)
        : queue_(queue), url_(url) {
        for (int i = 0; i < num_workers; ++i) {
            workers_.emplace_back(&FetcherPool::worker, this);
        }
    }

    void join() {
        for (auto& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
    }

private:
    void worker() {
        while (!queue_.is_shutdown()) {
            auto maybe_chunk = queue_.pop();
            if (!maybe_chunk) break;

            Chunk& chunk = *maybe_chunk;
            if (ChunkFetcher::fetch(url_, chunk)) {
                std::cout << "Downloaded and saved " << chunk.filename() << std::endl;
            } else {
                std::cerr << "Failed to fetch chunk at offset " << chunk.offset_ << std::endl;
            }

            // Shutdown if the last chunk was undersized
            if (chunk.data_.size() < chunk.size_) {
                queue_.shutdown();
            }
        }
    }
};


#endif //FETCHPOOL_H
