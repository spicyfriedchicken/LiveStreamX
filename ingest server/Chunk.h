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
private:
    struct TSPacket {
        uint8_t sync_byte;
        uint16_t pid;
        bool payload_unit_start;
        bool adaptation_field_exists;
        uint8_t adaptation_field_length;
        size_t payload_offset;
        size_t payload_size;
    };

    static bool parse_ts_packet(const std::vector<uint8_t>& data, size_t offset, TSPacket& packet) {
        if (offset + 188 > data.size() || data[offset] != 0x47) {
            return false;
        }

        packet.sync_byte = data[offset];
        packet.payload_unit_start = (data[offset + 1] & 0x40) != 0;
        packet.pid = ((data[offset + 1] & 0x1F) << 8) | data[offset + 2];
        packet.adaptation_field_exists = (data[offset + 3] & 0x20) != 0;

        packet.payload_offset = offset + 4;
        if (packet.adaptation_field_exists) {
            packet.adaptation_field_length = data[offset + 4];
            packet.payload_offset += 1 + packet.adaptation_field_length;
        }

        if (packet.payload_offset >= offset + 188) {
            packet.payload_size = 0;
        } else {
            packet.payload_size = (offset + 188) - packet.payload_offset;
        }

        return true;
    }

    static std::optional<uint16_t> find_video_pid(const std::vector<uint8_t>& data) {
        // Look for PAT (PID 0) and then PMT to find video PID
        for (size_t i = 0; i + 188 <= data.size(); i += 188) {
            TSPacket packet;
            if (!parse_ts_packet(data, i, packet) || packet.pid != 0) continue;

            // Parse PAT to find PMT PID, then parse PMT to find video PID
            // For simplicity, assume common video PIDs: 0x100, 0x101, etc.
            // In production, you'd parse the actual PAT/PMT tables
        }

        // Common video PIDs to try
        std::vector<uint16_t> common_video_pids = {0x100, 0x101, 0x11, 0x20};
        for (uint16_t pid : common_video_pids) {
            // Check if this PID contains video data
            for (size_t i = 0; i + 188 <= data.size(); i += 188) {
                TSPacket packet;
                if (parse_ts_packet(data, i, packet) && packet.pid == pid && packet.payload_size > 0) {
                    return pid;
                }
            }
        }
        return std::nullopt;
    }

public:
    static bool has_idr_frame(const std::vector<uint8_t>& data) {
        // Step 1: Find video PID
        auto video_pid = find_video_pid(data);
        if (!video_pid) return false;

        // Step 2: Reconstruct PES packets from TS packets
        std::vector<uint8_t> pes_buffer;
        bool in_pes_packet = false;

        for (size_t i = 0; i + 188 <= data.size(); i += 188) {
            TSPacket packet;
            if (!parse_ts_packet(data, i, packet) || packet.pid != *video_pid) {
                continue;
            }

            if (packet.payload_unit_start) {
                // New PES packet starting
                if (in_pes_packet && !pes_buffer.empty()) {
                    // Process previous PES packet
                    if (contains_idr_frame(pes_buffer)) {
                        return true;
                    }
                }
                pes_buffer.clear();
                in_pes_packet = true;
            }

            if (in_pes_packet && packet.payload_size > 0) {
                // Add payload to PES buffer
                pes_buffer.insert(pes_buffer.end(),
                                data.begin() + packet.payload_offset,
                                data.begin() + packet.payload_offset + packet.payload_size);
            }
        }

        // Check final PES packet
        if (in_pes_packet && !pes_buffer.empty()) {
            return contains_idr_frame(pes_buffer);
        }

        return false;
    }

private:
    static bool contains_idr_frame(const std::vector<uint8_t>& pes_data) {
        // Skip PES header (usually 6-9 bytes + optional fields)
        size_t start = 0;
        if (pes_data.size() < 6) return false;

        // Basic PES header parsing
        if (pes_data[0] == 0x00 && pes_data[1] == 0x00 && pes_data[2] == 0x01) {
            uint8_t pes_header_length = pes_data[8];
            start = 9 + pes_header_length;
        }

        // Look for NAL units in the elementary stream
        bool found_sps = false, found_pps = false, found_aud = false;

        for (size_t i = start; i + 4 < pes_data.size(); ++i) {
            if (pes_data[i] == 0x00 && pes_data[i+1] == 0x00 &&
                pes_data[i+2] == 0x01) {

                uint8_t nal_type = pes_data[i+3] & 0x1F;

                switch (nal_type) {
                    case 7: found_sps = true; break;  // SPS
                    case 8: found_pps = true; break;  // PPS
                    case 9: found_aud = true; break;  // AUD
                    case 5: // IDR frame
                        // For robustness, ensure we have proper sequence
                        if (found_sps && found_pps) {
                            return true;
                        }
                        break;
                }
            }
        }

        return false;
    }
};

#endif
