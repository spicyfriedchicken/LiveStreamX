//
// Created by Oscar Abreu on 7/29/25.
//

#include "Chunk.h";

class S3DownloadManager {
    const size_t chunk_size = 188 * 5000;
    const int max_workers = 5;

public:
    void run() {
        std::string url = pick_random_s3_url("objects.txt");
        curl_global_init(CURL_GLOBAL_ALL);
        ChunkQueue queue;
        std::atomic<size_t> current_offset{0};

        std::thread producer([&]() {
            while (!queue.is_shutdown()) {
                size_t offset = current_offset.fetch_add(chunk_size);
                queue.push(Chunk{offset, chunk_size});
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
};

