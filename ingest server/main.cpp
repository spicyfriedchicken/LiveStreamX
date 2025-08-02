
#include <iostream>
#include <fstream>
#include <random>
#include "gop_processor.h"

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

int main() {
    std::string url = pick_random_s3_url("test_object.txt");

    SimpleGOPProcessor processor;
    processor.process_stream_realtime(url);
    return 0;
}
