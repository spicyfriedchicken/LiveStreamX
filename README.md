
# LiveStreamX (Real-Time Cat Videos)

A typical streaming service like Twitch or Zoom may have the following architecture:

A capture device on the emitters end (Client OBS) outputs raw video frames and audio (YUV/RGB and PCM). The client (OBS) encodes/compresses this large video using FFmpeg with libx264 and the H.264/AVC codec, using a set of encoding settings like resolution, framerate, group of picture size (how many frames we’re sending), bitrate (data/sec), etc. We upload this compressed/encoded video via a streaming protocol like WebRTC (Zoom) or RTMP (Twitch) into a backend “ingest” server (owned by Twitch). The server accepts this video and now ‘modulates’ this video to various resolutions (i.e., 720p, 480p, 360p, etc.) with GPU acceleration (probably). Then we segment these chunks of video using HLS to allow adaptive bitrate streaming and easy distribution. Here, Twitch might use a company like Amazon to route these video chunks to the closest server farm to the user (if you’re in NYC, there are multiple dedicated ones), and the router-hopping sequence starts from there to your ISP to your router, and the webcam model you were watching in 4K gets to your computer. This happens multiple times every few seconds.


--------------------------------------------------------------------------------------------------------------------------------------------

### [1] Capture (Youtube Video). 
    A Japanese man by the username ["niiyan1216"](https://www.youtube.com/user/niiyan1216) on YouTube has been posting daily videos of himself feeding stray cats for over 9 years. This dataset will represent the streamed content. We must      implement both a scraper and an S3 Asset Manager to load each randomized video sequentially.
    
    Store/Input: AWS S3
    Convert to: Raw video frames (YUV or RGB), and raw audio (PCM)

| API / Lib            | Platform       | Language      | Latency     | Ease of Use | Notes                                             |
| -------------------- | -------------- | ------------- | ----------- | ----------- | ------------------------------------------------- |
| **AVFoundation**     | macOS, iOS     | Obj-C / Swift | ✅ Low       | 😐 Medium   | Hardware-accelerated; native Apple stack          |
| **DirectShow**       | Windows        | C++           | ✅ Low       | 😐 Medium   | Old but powerful; replaced by Media Foundation    |
| **Media Foundation** | Windows        | C++           | ✅ Low       | 😖 Complex  | Successor to DirectShow; better for modern codecs |
| **V4L2**             | Linux          | C/C++         | ✅ Low       | 😖 Hard     | `/dev/video0` camera devices                      |
| **getUserMedia()**   | Web (browser)  | JS            | ✅ Low       | ✅ Easy      | WebRTC-based; works for webcam/mic                |
| **OpenCV**           | Cross-platform | C++/Python    | ⚠️ High     | ✅ Easy      | Good for prototyping; not low-latency             |
| **GStreamer**        | Cross-platform | C / CLI       | ✅ Low       | 😖 Hard     | Used in pro AV pipelines (can also encode)        |
| **WebRTC API**       | Cross-platform | C++ / JS      | ✅ Ultra-Low | 😐 Medium   | Built-in support for capture + encoding           |


### [2] Encoding (Compression)

    Codecs:

        Video: H.264/AVC, H.265/HEVC, AV1

        Audio: AAC, Opus

🔧 Encoding settings matter:

    Bitrate (CBR/VBR)

    GOP (Group of Pictures) size

    Frame rate (30/60fps)

    Resolution (1080p, 720p)

| Tool                     | Encoding Type  | Speed       | Quality      | Use Case                           |
| ------------------------ | -------------- | ----------- | ------------ | ---------------------------------- |
| **FFmpeg + libx264**     | Software (CPU) | ⚠️ Slower   | ✅ Good       | Widely used, flexible              |
| **NVENC (NVIDIA)**       | Hardware (GPU) | ✅ Fast      | 😐 OK        | Real-time streaming, less CPU load |
| **QuickSync (Intel)**    | Hardware       | ✅ Fast      | 😐 OK        | Used in OBS                        |
| **VideoToolbox (Apple)** | Hardware       | ✅ Fast      | 😐 OK        | Built into macOS/iOS               |
| **libaom AV1**           | Software (CPU) | ❌ Very Slow | ✅✅ Excellent | Archive/VoD, not real-time         |
| **SVT-AV1**              | Software (CPU) | ⚠️ Medium   | ✅✅ Very Good | Slightly faster than libaom        |
| **x265**                 | Software (CPU) | ⚠️ Medium   | ✅✅ Very Good | Best quality in HEVC               |

### [3] Upload (Streaming Protocol)

| Protocol   | Transport | Latency   | Reliability      | Complexity | Use Case                           |
| ---------- | --------- | --------- | ---------------- | ---------- | ---------------------------------- |
| **RTMP**   | TCP       | ⚠️ \~2–5s | ✅ High           | ✅ Easy     | Twitch, YouTube Live ingest        |
| **SRT**    | UDP       | ✅ \~1–2s  | ✅ Strong (FEC)   | ⚠️ Medium  | Low-latency ingest across networks |
| **WebRTC** | UDP       | ✅✅ <1s    | ⚠️ Less reliable | ❌ Complex  | Zoom, Discord, FaceTime            |
| **RIST**   | UDP       | ✅ \~1s    | ✅ Like SRT       | ❌ Complex  | Broadcast-grade low-latency links  |
| **RTSP**   | TCP/UDP   | ⚠️ Legacy | ⚠️ Weak          | ⚠️ Legacy  | Camera feeds, NVR systems          |


[4] Transcoding & Replication

    Server accepts 1080p input and transcodes into:

        720p, 480p, 360p, etc.

    Can run on:

        Kubernetes (scales pods per stream)

        FFmpeg on bare-metal for cost efficiency

| Method                             | Hardware | Speed    | Quality |  Scalability  | Notes                    |
| ---------------------------------- | -------- | -------- | ------- | ------------- | ------------------------ |
| **FFmpeg Software**                | CPU      | ⚠️ Slow  | ✅ Good  | ⚠️ Costly    | Flexible but heavy      |
| **FFmpeg + NVENC/QuickSync**       | GPU/iGPU | ✅ Fast   | 😐 OK   | ✅ Better    | Real-time capable      |
| **MediaMTX Built-in Transcode**    | CPU      | ⚠️ Basic | 😐 OK   | Limited       | Works but not tuned      |
| **GStreamer GPU Pipelines**        | GPU      | ✅ Fast   | ✅✅ High | ✅✅ Good  | Used in pro-grade stacks |
| **Cloud Services (AWS MediaLive)** | Cloud    | ✅✅ Fast  | ✅✅ Good | ✅✅✅    | Expensive but scalable   |

[5] Segmenting & Packaging

    HLS (HTTP Live Streaming):

        Break stream into .ts or .fmp4 segments (2–6s chunks)

        Generates an .m3u8 playlist index

    DASH (Dynamic Adaptive Streaming over HTTP):

        Similar but XML/MPD-based

    These formats allow:

        Adaptive bitrate streaming (switching resolutions on the fly)

        Easy caching & distribution over HTTP

        Near real-time playback

| Protocol      | File Types                 | Latency  | Segment Duration   | Notes                              |
| ------------- | -------------------------- | -------- | ------------------ | ---------------------------------- |
| **HLS**       | `.m3u8` + `.ts/.m4s`       | ⚠️ 2–6s+ | 2–6 sec            | Default for Twitch/YouTube         |
| **LL-HLS**    | `.m3u8` + `.m4s` + `.part` | ✅ 1–2s   | 1–2s + 200ms parts| Lower latency, browser supported   |
| **MPEG-DASH** | `.mpd` + `.m4s`            | ⚠️ 4–10s | 2–4 sec            | Used by YouTube, Netflix           |
| **CMAF**      | `.m3u8/.mpd` + `.cmf`      | ✅ ✅      | 1–2s             | Common media format (Apple + DASH) |

[6] Delivery via CDN

    CDNs like Akamai, Cloudflare, Fastly distribute segments globally.

    HTTP-based delivery allows:

        Browser caching

        Regional edge servers

        Load balancing & rate limiting

    Users request segments like:

    https://cdn.twitch.tv/live/stream123/1080p/segment152.ts

| Option          | Latency     | Cost    | Scalability   | Setup Time | Best For              |
| --------------- | ----------- | ------- | ------------- | ---------- | --------------------- |
| NGINX Local     | ✅ Low       | ✅ Free  | ⚠️ Limited    | ✅ Fast    | Dev/prototyping   |
| CloudFront+S3   | ⚠️ Higher   | ⚠️ Some | ✅✅ Scalable   | ⚠️ Medium  | Production use     |
| MediaMTX direct | ✅✅ Very Low | ✅ Free  | ⚠️ Local only | ✅ Easy   | LL-HLS local demo|

[7]
