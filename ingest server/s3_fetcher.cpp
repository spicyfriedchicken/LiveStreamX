//
// Created by Oscar Abreu on 7/28/25.
//

#include <iostream>
#include <fstream>
#include <stream>
#include <thread>
#include <mutex>
#include <vector>
#include <cmath>
#include <atomic>
#include <condition_variable>
#include <curl/curl.h>

struct chunk {
    size_t offset;
    size_t size;
    std::vector<uint8_t> data;
    bool downloaded = false;
};
constexpr uint32_t NUM_CHUNKS = 2;
constexpr size_t CHUNK_SIZE = 1024 * 1024;
constexpr uint8_t MAX_WORKERS = 5;

std::mutex queue_mutex;
std::condition_variable queue_condition;
std::vector<Chunk> fetch_queue;
std::atomic<bool> shutdown{false};

// stores prefetched bytes into chunk->data
size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    Chunk* chunk = static_cast<Chunk*>(userdata);
    size_t total = size * nmemb;
    chunk->data.insert(chunk->data.end(), ptr, ptr + total);
    return total;
}

void store_chunk(const Chunk& chunk) {
     std::string filename = "chunk_" + std::to_string(chunk.offset) + ".bin";
     std::ofstream out(filename, std::ios::binary);
     if (!out) {
         std::cerr << "failed to write chunk at offset " << chunk.offset << std::endl;
     }
     out.write(reinterpret_cast<const char*>(chunk.data.data()), chunk.data.size());
     out.close();
     std::cout<< "Stored chunk to " << filename << std::endl;
}

void fetch_chunk(const std::string& url, Chunk& chunk) {
    CURL* curl = curl_easy_init();
    if (!curl) return;

    std::string range_header = std::to_string(chunk.offset) + "-" + std::to_string(chunk.offset + chunk.size - 1);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
    curl_easy_setopt(curl, CURLOPT_RANGE, range_header.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "error! Fetch failed. " << curl_easy_strerror(res) << std::endl;
    } else {
        chunk.downloaded = true;
    }

    curl_easy_cleanup(curl);
}

void worker_thread(const std::string& url) {
    while (!shutdown.load()) {
        Chunk chunk;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_condition.wait(lock, [] { return !fetch_queue.empty() || shutdown.load(); });
            if (shutdown.load()) break;
            chunk = fetch_queue.back();
            fetch_queue.pop_back();
        }

        fetch_chunk(url, chunk);

        if (chunk.downloaded) {
            store_chunk(chunk);
            std::cout << "finished fetching chunk at offset=" << chunk.offset << ", size=" << chunk.size << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "invalid number of arguments.\nusage: ./s3_fetcher <S3_URL>" << std::endl;
        return 1;
    }

    std::string s3_url = argv[1];
    std::size_t expected_fetch_size = CHUNK_SIZE * NUM_CHUNKS;

    curl_global_init(CURL_GLOBAL_ALL);  // must be called before any curl usage
    // confusing, but if you think of this like a waittress writing an order on the wall
    // each chef (worker) will pull a chunk from the pre-filled queue and populate it and mark it as done
    for (std::size_t offset = 0; offset < expected_fetch_size;) {
        Chunk chunk;
        chunk.offset = offset;
        chunk.size = std::min(CHUNK_SIZE, expected_fetch_size - offset);

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            fetch_queue.push_back(chunk);
        }

        offset += chunk.size;  // don't assume chunk.size == CHUNK_SIZE for final chunk
    }

    queue_condition.notify_all();

    // Launch worker threads
    std::vector<std::thread> workers;
    for (int i = 0; i < MAX_WORKERS; ++i) {
        workers.emplace_back(worker_thread, s3_url);
    }

    for (auto& t : workers) {
        t.join();
    }

    curl_global_cleanup(); // must be calle after curl
    return 0;
}
