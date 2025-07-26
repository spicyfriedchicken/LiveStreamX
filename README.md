
# LiveStreamX (Real-Time Cat Videos)

A typical streaming service like Twitch or Zoom may have the following architecture:

A capture device on the emitters end (Client OBS) outputs raw video frames and audio (YUV/RGB and PCM). The client (OBS) encodes/compresses this large video using FFmpeg with libx264 and the H.264/AVC codec, using a set of encoding settings like resolution, framerate, group of picture size (how many frames we’re sending), bitrate (data/sec), etc. We upload this compressed/encoded video via a streaming protocol like WebRTC (Zoom) or RTMP (Twitch) into a backend “ingest” server (owned by Twitch). The server accepts this video and now ‘modulates’ this video to various resolutions (i.e., 720p, 480p, 360p, etc.) with GPU acceleration (probably). Then we segment these chunks of video using HLS to allow adaptive bitrate streaming and easy distribution. Here, Twitch might use a company like Amazon to route these video chunks to the closest server farm to the user (if you’re in NYC, there are multiple dedicated ones), and the router-hopping sequence starts from there to your ISP to your router, and finally to your computer. This happens multiple times every few seconds.


# LiveStreamX: Real-Time AI-Enhanced Cat Streaming

LiveStreamX is a real-time, AI-powered streaming pipeline that ingests pre-recorded `.mp4` videos (H.264), performs GPU-accelerated super-resolution using a distilled Dense Residual Connection Transformer (DRCT), and delivers enhanced live streams to viewers via HLS or WebRTC.

---

# Architecture Overview

Unlike traditional streaming services (e.g. Twitch or Zoom) which ingest raw streams from live devices, LiveStreamX pulls compressed `.mp4` videos from YouTube (via niiyan1216's cat videos), enhances them using an AI model, and streams them in real time as if they were live.

### Streaming Flow

```text
[YouTube Scraper (Go)]
     ↓
[.mp4/H.264 stored in AWS S3]
     ↓
[NVDEC (GPU decode) → YUV420]
     ↓
[YUV420 → RGB → Distilled DRCT → RGB → YUV420]  ← all fused in CUDA
     ↓
[NVENC (GPU encode) → H.265/H.264]
     ↓
[Fan-out to HLS/WebRTC edge servers]
```

---

## [1] Input: YouTube Video Scraping

We use a Go-based scraper to ingest videos from the YouTube channel [niiyan1216](https://www.youtube.com/user/niiyan1216), a user known for over 9 years of stray cat content.

- Format: `.mp4`
- Codec: `H.264/AVC`
- Storage: `AWS S3`

---

## [2] Inference Pipeline

### Model: **Distilled Dense Residual Connection Transformer (DRCT)**

- A fast, compact transformer-based upscaling model
- Designed for real-time inference under GPU constraints
- Converts low-res RGB frames into enhanced RGB frames

### Inference Stack:

| Step           | Technology          | Details                             |
| -------------- | ------------------- | ----------------------------------- |
| Decode         | **NVDEC**           | H.264 → YUV420                      |
| Preprocessing  | **CUDA Kernel**     | YUV420 → RGB                        |
| Inference      | **TensorRT (FP16)** | RGB → RGB (super-res / enhancement) |
| Postprocessing | **CUDA Kernel**     | RGB → YUV420                        |
| Encode         | **NVENC**           | YUV420 → H.265/H.264                |

> All operations after decode are fully GPU-resident to avoid PCIe transfer bottlenecks.

---

## [3] Fan-Out (Streaming Distribution)

Once encoded, enhanced video is streamed to end-users via multiple formats:

| Format | Use Case        | Latency | Notes                                 |
| ------ | --------------- | ------- | ------------------------------------- |
| HLS    | Web playback    | \~3–6s  | Adaptive bitrate supported            |
| LL-HLS | Faster playback | \~1–2s  | Lower latency for modern browsers     |
| WebRTC | Real-time demo  | <1s     | Requires SFU server                   |
| RTMP   | Legacy support  | \~2–4s  | Optionally restream to YouTube/Twitch |

Fan-out is horizontally scalable — edge servers pull from a central output queue and stream to clients.

---

## [4] Encoding Settings (for Output)

- Resolution: 1080p (Input), enhanced to 1080p or 1440p
- GOP Size: 60 frames (1s @ 60fps)
- Bitrate: 3–6 Mbps (CBR or constrained VBR)
- Format: `.ts` (HLS), `.m4s` (DASH/CMAF)
- Audio: Passthrough (AAC or Opus)

---

## [5] Edge Distribution and CDN

Enhanced streams are segmented and routed to users via:

- **NGINX + LL-HLS** for local testing
- **CloudFront + S3** for scalable global delivery
- **MediaMTX or GStreamer** for hybrid ingest/distribute setups

CDNs enable adaptive bitrate playback, caching, and user load balancing.

---

## [6] Playback and Monitoring

Use **libVLC** or **web-based players** to validate performance:

- Track latency, FPS, frame drops
- Validate resolution switching (ABR)
- Compare pre/post-inference video quality

---

## Dev Notes

- All AI inference runs in a fused CUDA pipeline with pre/post color conversion and TensorRT inference
- NVDEC/NVENC are used for decode/encode to maintain end-to-end GPU residency
- Streams are routed to least-congested fan-out servers after enhancement

---

## TODO

-

---

## Special Thanks

To [niiyan1216](https://www.youtube.com/user/niiyan1216) for his cute contributions.


Options
--------------------------------------------------------------------------------------------------------------------------------------------

### [1] Capture (Youtube Video). 
    A Japanese man by the username ["niiyan1216"](https://www.youtube.com/user/niiyan1216) on YouTube has been posting daily videos of himself feeding stray cats for over 9 years. This dataset will represent the streamed content. We must      implement both a scraper and an S3 Asset Manager to load each randomized video sequentially.
    
    Store/Input: AWS S3, Format: mp4/H.264

### [2] Encoding (Compression)

    Codecs:

        Video: H.264/AVC, H.265/HEVC, AV1

        Audio: AAC, Opus

 Encoding settings matter:

    Bitrate (CBR/VBR)

    GOP (Group of Pictures) size

    Frame rate (30/60fps)

    Resolution (1080p, 720p)

FFmpeg + libx264 (Software, CPU): Not chosen, but (in my opinion) its the best alternative for a CPU-based pipeline. <br>
QuickSync (Hardware, Intel iGPU): Not chosen because it’s tied to Intel hardware and not available on cloud GPU servers.<br>
VideoToolbox (Hardware, Apple): Not chosen because it's macOS-only and not suitable for server or GPU infrastructure.<br>
libaom AV1 (Software, CPU): Not chosen because it's extremely slow and only suitable for offline VoD encoding.<br>
SVT-AV1 (Software, CPU): Not chosen because it’s still too slow for real-time applications despite better performance than libaom.<br>
x265 (Software, CPU): Ideal for VoD but not fast enough for real-time GPU inference pipelines.<br>
✅ NVENC (Hardware, GPU): Chosen for its real-time encoding speed, compatibility with GPU workflows, and low CPU overhead.

### [3] Upload (Streaming Protocol)

SRT (UDP): Not chosen because it's better suited for unstable network ingest and adds unnecessary complexity.<br>
RIST (UDP): Not chosen because it's overkill for consumer-grade streaming and targeted at professional broadcast backhauls.<br>
RTSP (TCP/UDP): Not chosen due to its legacy design, weak reliability, and limited modern support for web and adaptive delivery.<br>
WebRTC (UDP): Not chosen due to high implementation complexity. However, a possible avenue if I need a _faster_ ingest stream over RTMP.<br>
✅ RTMP (TCP): Chosen for its simplicity, wide ecosystem support, and sufficient reliability for streaming pre-recorded video. A "safe option" for now.

[4] Transcoding & Replication

    Server accepts 1080p input and transcodes into:

        720p, 480p, 360p, etc.

    Can run on:

        Kubernetes (scales pods per stream)

        FFmpeg on bare-metal for cost efficiency


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

MPEG-DASH: Not chosen due to higher latency and weaker native browser support compared to LL-HLS.<br>
HLS: Not chosen because its default 2–6s latency is too high for near real-time playback, but likely will add this as fallback later (some browsers do not support LL-HLS).<br>
LL-HLS: Chosen for delivering low-latency (~1–2s) playback in modern browsers. The other choices aren't bad, but this one makes the most sense!

[6] Delivery via CDN

    CDNs like Akamai, Cloudflare, Fastly distribute segments globally.

    HTTP-based delivery allows:

        Browser caching

        Regional edge servers

        Load balancing & rate limiting

    Users request segments like:

    https://cdn.twitch.tv/live/stream123/1080p/segment152.ts

First, we will use MediaMTX for low-latency LL-HLS streaming during local development and demos; later, we will transition to [Cloudflare](https://screencasting.com/articles/cheap-video-hosting) R2 for scalable, production-grade global delivery (Cloudfront is just too expensive!).

