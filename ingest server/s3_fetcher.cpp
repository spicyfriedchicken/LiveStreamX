//
// Created by Oscar Abreu on 7/28/25.
//

// #include <iostream>
// #include <fstream>
// #include <thread>
// #include <mutex>
// #include <vector>
// #include <cmath>
// #include <atomic>
// #include <condition_variable>
// #include <random>
// #include <curl/curl.h>
//
// struct Chunk {
//     size_t offset;
//     size_t size;
//     std::vector<uint8_t> data;
//     bool downloaded = false;
// };
// constexpr uint32_t NUM_CHUNKS = 16;
// constexpr size_t CHUNK_SIZE = 188 * 5000;
// constexpr uint8_t MAX_WORKERS = 5;
// const std::string BUCKET_NAME = "livestreamx-cats/";
// std::mutex queue_mutex;
// std::condition_variable queue_condition;
// std::vector<Chunk> fetch_queue;
// std::atomic<bool> shutdown_{false};
//
// // stores prefetched bytes into chunk->data
// size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
//     Chunk* chunk = static_cast<Chunk*>(userdata);
//     size_t total = size * nmemb;
//     chunk->data.insert(chunk->data.end(), ptr, ptr + total);
//     return total;
// }
//
// void store_chunk(const Chunk& chunk) {
//      std::string filename = "chunk_" + std::to_string(chunk.offset) + ".bin";
//      std::ofstream out(filename, std::ios::binary);
//      if (!out) {
//          std::cerr << "failed to write chunk at offset " << chunk.offset << std::endl;
//      }
//      out.write(reinterpret_cast<const char*>(chunk.data.data()), chunk.data.size());
//     out.flush();
//
//      out.close();
//      std::cout<< "Stored chunk to " << filename << std::endl;
// }
//
// void fetch_chunk(const std::string& url, Chunk& chunk) {
//     CURL* curl = curl_easy_init();
//     if (!curl) return;
//
//     std::string range_header = std::to_string(chunk.offset) + "-" + std::to_string(chunk.offset + chunk.size - 1);
//
//     curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
//     curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
//     curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
//     curl_easy_setopt(curl, CURLOPT_RANGE, range_header.c_str());
//     curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
//     curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
//     curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
//
//     CURLcode res = curl_easy_perform(curl);
//     if (res != CURLE_OK) {
//         std::cerr << "error! Fetch failed. " << curl_easy_strerror(res) << std::endl;
//     } else {
//         chunk.downloaded = true;
//     }
//
//     curl_easy_cleanup(curl);
// }
//
// void worker_thread(const std::string& url) {
//     while (true) {
//         Chunk chunk;
//         {
//             std::unique_lock<std::mutex> lock(queue_mutex);
//             queue_condition.wait(lock, [] {
//                 return !fetch_queue.empty() || shutdown_.load();
//             });
//
//             if (fetch_queue.empty()) {
//                 if (shutdown_.load()) break; // nothing to do
//                 else continue; // spurious wakeup
//             }
//
//             chunk = fetch_queue.back();
//             fetch_queue.pop_back();
//         }
//
//         fetch_chunk(url, chunk);
//
//         if (chunk.downloaded) {
//             store_chunk(chunk);
//             std::cout << "finished fetching chunk at offset=" << chunk.offset
//                       << ", size=" << chunk.data.size() << std::endl;
//
//             if (chunk.data.size() < CHUNK_SIZE) {
//                 std::cout << "End of stream detected at offset=" << chunk.offset << "\n";
//                 shutdown_.store(true);
//                 queue_condition.notify_all();  // Wake any sleeping workers
//             }
//         }
//
//     }
// }
//
//
// int main(int argc, char* argv[]) {
//     if (argc != 1) {
//         std::cerr << "usage: ./s3_fetcher" << std::endl;
//         return 1;
//     }
//
//     // HARDCODED VALUES
//     int totalLines = 100;
//     const std::string filename = "objects.txt";
//     std::random_device rd;
//     std::mt19937 gen(rd());
//     std::uniform_int_distribution<> dist(0, totalLines - 1);
//     int targetLine = dist(gen);
//
//     // Read just that line
//     std::ifstream file(filename);
//     std::string line;
//     for (int i = 0; i <= targetLine && std::getline(file, line); ++i);
//
//     std::string total_line;
//     for (char c : line) {
//         if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_') {
//             total_line += c;
//         }
//     }
//
//     std::string s3_url = "https://livestreamx-cats.s3.amazonaws.com/" + line;
//
//     std::atomic<size_t> current_offset{0};
//
//     curl_global_init(CURL_GLOBAL_ALL);  // must be called before any curl usage
//     // confusing, but if you think of this like a waitress writing an order on the wall
//     // each chef (worker) will pull a chunk from the pre-filled queue and populate it and mark it as done
//     std::thread producer([&]() {
//         while (!shutdown_.load()) {
//             size_t offset = current_offset.fetch_add(CHUNK_SIZE);
//
//             Chunk chunk;
//             chunk.offset = offset;
//             chunk.size = CHUNK_SIZE;
//
//             {
//                 std::lock_guard<std::mutex> lock(queue_mutex);
//                 fetch_queue.push_back(chunk);
//             }
//
//             queue_condition.notify_one();
//             std::this_thread::sleep_for(std::chrono::milliseconds(10));
//         }
//     });
//
//
//
//     queue_condition.notify_all();
//
//     // Launch worker threads
//     std::vector<std::thread> workers;
//     for (int i = 0; i < MAX_WORKERS; ++i) {
//         workers.emplace_back(worker_thread, s3_url);
//     }
//
//     for (auto& t : workers) {
//         t.join();
//     }
//
//     curl_global_cleanup(); // must be calle after curl
//     producer.join();
//
//     return 0;
// }
