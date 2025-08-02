// gosh this is so complicated, lets just have ffmpeg do it all

#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <chrono>
#include <random>
#include <fstream>
class SimpleGOPProcessor {
public:
    void process_stream_realtime(const std::string& s3_url) {
        std::string cmd = "ffmpeg -i \"" + s3_url + "\" "
                         "-c copy "
                         "-f segment "
                         "-segment_format mpegts "
                         "-break_non_keyframes 0 "
                         "-reset_timestamps 1 "
                         "gop_%03d.ts &";

        std::cout << "Starting FFmpeg segmentation..." << std::endl;
        system(cmd.c_str());
        process_segments_as_ready();
    }

private:
    void process_segments_as_ready() {
        int segment_number = 1;

        while (true) {
            std::string segment_file = "gop_" + pad_number(segment_number, 3) + ".ts";

            while (!std::filesystem::exists(segment_file)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            std::cout << "Processing " << segment_file << std::endl;

            // gpu_decoder.process_segment(segment_file);

            test_segment(segment_file);
            segment_number++;
            if (segment_number > 100) break;
        }
    }

    void test_segment(const std::string& filename) {
        std::string cmd = "ffprobe -v quiet \"" + filename + "\" 2>&1";
        int result = system(cmd.c_str());

        if (result == 0) {
            std::cout << filename << " is valid" << std::endl;
        } else {
            std::cout << filename << " is NOT ivalid" << std::endl;
        }
    }

    std::string pad_number(int num, int width) {
        std::string str = std::to_string(num);
        return std::string(width - str.length(), '0') + str;
    }
};
