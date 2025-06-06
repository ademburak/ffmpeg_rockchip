extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_buffer.h>
}

#include <opencv2/opencv.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>

// Function to get CPU usage
double get_cpu_usage() {
    static unsigned long long lastTotalUser, lastTotalUserLow, lastTotalSys, lastTotalIdle;
    unsigned long long totalUser, totalUserLow, totalSys, totalIdle, total;
    
    std::ifstream statFile("/proc/stat");
    std::string line;
    std::getline(statFile, line);
    std::istringstream iss(line);
    std::string cpu;
    iss >> cpu >> totalUser >> totalUserLow >> totalSys >> totalIdle;
    
    if (lastTotalUser == 0) {
        lastTotalUser = totalUser;
        lastTotalUserLow = totalUserLow;
        lastTotalSys = totalSys;
        lastTotalIdle = totalIdle;
        return 0.0;
    }
    
    unsigned long long totalUserDiff = totalUser - lastTotalUser;
    unsigned long long totalUserLowDiff = totalUserLow - lastTotalUserLow;
    unsigned long long totalSysDiff = totalSys - lastTotalSys;
    unsigned long long totalIdleDiff = totalIdle - lastTotalIdle;
    
    lastTotalUser = totalUser;
    lastTotalUserLow = totalUserLow;
    lastTotalSys = totalSys;
    lastTotalIdle = totalIdle;
    
    total = totalUserDiff + totalUserLowDiff + totalSysDiff + totalIdleDiff;
    return total == 0 ? 0.0 : 100.0 * (totalUserDiff + totalUserLowDiff + totalSysDiff) / total;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./rtsp_player <rtsp_url> [--no-record] [--no-resize] [--color-format=bgr|yuv|nv12] [--use-mpp] [output_file.mp4]" << std::endl;
        return -1;
    }

    const char* rtsp_url = argv[1];
    bool no_record = false;
    bool no_resize = false;
    bool use_bgr = false;  // Default to YUV format
    bool use_nv12 = false;
    bool use_mpp = false;  // Default to OpenCV for conversion
    const char* output_file = "output.mp4";

    // Parse arguments
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        std::cout << "Processing argument: " << arg << std::endl;  // Debug output
        if (arg == "--no-record") {
            no_record = true;
        } else if (arg == "--no-resize") {
            no_resize = true;
        } else if (arg == "--use-mpp") {
            use_mpp = true;
            std::cout << "Using MPP for color conversion" << std::endl;
        } else if (arg.find("--color-format=") == 0) {
            std::string format = arg.substr(15);  // Length of "--color-format=" is 15
            std::cout << "Parsing color format: " << format << std::endl;  // Debug output
            if (format == "yuv") {
                use_bgr = false;
                use_nv12 = false;
                std::cout << "Setting color format to YUV" << std::endl;
            } else if (format == "nv12") {
                use_bgr = false;
                use_nv12 = true;
                std::cout << "Setting color format to NV12" << std::endl;
            } else if (format == "bgr") {
                use_bgr = true;
                use_nv12 = false;
                std::cout << "Setting color format to BGR" << std::endl;
            } else {
                std::cerr << "Invalid color format. Use 'bgr', 'yuv', or 'nv12'" << std::endl;
                return -1;
            }
        } else if (arg[0] != '-') {  // Only treat non-option arguments as output file
            output_file = argv[i];
        }
    }

    std::cout << "Connecting to RTSP URL: " << rtsp_url << std::endl;
    if (!no_record) {
        std::cout << "Output file: " << output_file << std::endl;
    } else {
        std::cout << "Running in no-record mode" << std::endl;
    }
    if (no_resize) {
        std::cout << "Running in no-resize mode" << std::endl;
    } else {
        std::cout << "Running with frame resizing (800x600)" << std::endl;
    }
    std::cout << "Color format: " << (use_bgr ? "BGR" : (use_nv12 ? "NV12" : "YUV")) << std::endl;

    avformat_network_init();

    // Input setup with additional options for HEVC
    AVFormatContext* fmt_ctx = nullptr;
    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    av_dict_set(&options, "stimeout", "5000000", 0);
    av_dict_set(&options, "analyzeduration", "5000000", 0);
    av_dict_set(&options, "probesize", "5000000", 0);
    av_dict_set(&options, "buffer_size", "1024000", 0);
    av_dict_set(&options, "rtsp_flags", "prefer_tcp", 0);
    av_dict_set(&options, "reorder_queue_size", "0", 0);
    av_dict_set(&options, "max_delay", "500000", 0);

    if (avformat_open_input(&fmt_ctx, rtsp_url, nullptr, &options) < 0) {
        std::cerr << "Could not open input stream" << std::endl;
        av_dict_free(&options);
        return -1;
    }
    av_dict_free(&options);

    // Set additional options after opening
    fmt_ctx->flags |= AVFMT_FLAG_NOBUFFER;
    fmt_ctx->flags |= AVFMT_FLAG_FLUSH_PACKETS;

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream information" << std::endl;
        return -1;
    }

    // Find video stream
    int video_stream_index = -1;
    AVCodec* decoder = nullptr;
    AVCodecContext* dec_ctx = nullptr;  // Declare dec_ctx at the top level
    
    // First find the video stream
    video_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_index < 0) {
        std::cerr << "Could not find video stream" << std::endl;
        return -1;
    }

    // Get the codec ID from the stream
    AVCodecID codec_id = fmt_ctx->streams[video_stream_index]->codecpar->codec_id;
    std::cout << "Stream codec ID: " << avcodec_get_name(codec_id) << std::endl;

    // Define hardware decoders based on codec type
    const char* hw_decoders = nullptr;
    if (codec_id == AV_CODEC_ID_H264) {
        hw_decoders = "h264_rkmpp";  // Try Rockchip hardware decoder for H.264
    } else if (codec_id == AV_CODEC_ID_HEVC) {
        hw_decoders = "hevc_rkmpp";  // Try Rockchip hardware decoder for HEVC
    }

    // Try hardware decoder if available for this codec
    if (hw_decoders) {
        decoder = avcodec_find_decoder_by_name(hw_decoders);
        if (decoder) {
            std::cout << "Trying hardware decoder: " << hw_decoders << std::endl;
            
            // Setup decoder with additional options
            dec_ctx = avcodec_alloc_context3(decoder);
            if (dec_ctx) {
                // Copy parameters from software context
                if (avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[video_stream_index]->codecpar) >= 0) {
                    // Set thread count for decoding
                    dec_ctx->thread_count = 4;
                    dec_ctx->thread_type = FF_THREAD_FRAME;
                    
                    // Additional decoder options
                    AVDictionary* decoder_opts = nullptr;
                    av_dict_set(&decoder_opts, "threads", "4", 0);
                    av_dict_set(&decoder_opts, "zerocopy", "1", 0);
                    av_dict_set(&decoder_opts, "refcounted_frames", "1", 0);
                    av_dict_set(&decoder_opts, "skip_loop_filter", "48", 0);
                    av_dict_set(&decoder_opts, "skip_frame", "0", 0);
                    av_dict_set(&decoder_opts, "strict", "experimental", 0);

                    // Add hardware-specific options
                    if (strstr(decoder->name, "rkmpp")) {
                        av_dict_set(&decoder_opts, "zerocopy", "1", 0);
                        // Add H.264 specific options
                        if (codec_id == AV_CODEC_ID_H264) {
                            av_dict_set(&decoder_opts, "flags2", "+export_mvs", 0);
                            av_dict_set(&decoder_opts, "flags", "+low_delay", 0);
                            av_dict_set(&decoder_opts, "flags2", "+fast", 0);
                        }
                    }

                    // Try to open hardware decoder
                    if (avcodec_open2(dec_ctx, decoder, &decoder_opts) >= 0) {
                        std::cout << "Successfully opened hardware decoder" << std::endl;
                        
                        // Create hardware device context
                        AVBufferRef* hw_device_ref = nullptr;
                        int ret = av_hwdevice_ctx_create(&hw_device_ref, AV_HWDEVICE_TYPE_DRM, "/dev/dri/renderD128", nullptr, 0);
                        if (ret < 0) {
                            char err_buf[AV_ERROR_MAX_STRING_SIZE];
                            av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
                            std::cout << "Failed to create hardware device context: " << err_buf << std::endl;
                        } else {
                            // Create hardware frames context
                            AVBufferRef* hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ref);
                            if (hw_frames_ref) {
                                AVHWFramesContext* hw_frames_ctx = (AVHWFramesContext*)hw_frames_ref->data;
                                hw_frames_ctx->format = AV_PIX_FMT_DRM_PRIME;
                                hw_frames_ctx->sw_format = AV_PIX_FMT_YUV420P;
                                hw_frames_ctx->width = dec_ctx->width;
                                hw_frames_ctx->height = dec_ctx->height;
                                hw_frames_ctx->initial_pool_size = 20;
                                
                                if (av_hwframe_ctx_init(hw_frames_ref) >= 0) {
                                    dec_ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
                                    std::cout << "Hardware frames context initialized" << std::endl;
                                    std::cout << "Hardware frames format: " << av_get_pix_fmt_name(hw_frames_ctx->sw_format) << std::endl;
                                    std::cout << "Hardware frames width: " << hw_frames_ctx->width << std::endl;
                                    std::cout << "Hardware frames height: " << hw_frames_ctx->height << std::endl;
                                } else {
                                    std::cout << "Failed to initialize hardware frames context" << std::endl;
                                }
                                av_buffer_unref(&hw_frames_ref);
                            } else {
                                std::cout << "Failed to allocate hardware frames context" << std::endl;
                            }
                            av_buffer_unref(&hw_device_ref);
                        }
                    } else {
                        std::cout << "Failed to open hardware decoder, falling back to software" << std::endl;
                        avcodec_free_context(&dec_ctx);
                        decoder = nullptr;
                    }
                    av_dict_free(&decoder_opts);
                } else {
                    std::cout << "Failed to copy parameters to hardware context, falling back to software" << std::endl;
                    avcodec_free_context(&dec_ctx);
                    decoder = nullptr;
                }
            }
        }
    }

    // If no hardware decoder found or not available, use the default software decoder
    if (!decoder) {
        decoder = avcodec_find_decoder(codec_id);
        if (!decoder) {
            std::cerr << "Could not find decoder for codec: " << avcodec_get_name(codec_id) << std::endl;
            return -1;
        }
        std::cout << "Using software decoder: " << decoder->name << std::endl;

        // Setup decoder with additional options
        dec_ctx = avcodec_alloc_context3(decoder);
        if (!dec_ctx) {
            std::cerr << "Could not allocate decoder context" << std::endl;
            return -1;
        }

        avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[video_stream_index]->codecpar);
        
        // Set thread count for decoding
        dec_ctx->thread_count = 4;
        dec_ctx->thread_type = FF_THREAD_FRAME;
        
        // Additional decoder options
        AVDictionary* decoder_opts = nullptr;
        av_dict_set(&decoder_opts, "threads", "4", 0);
        av_dict_set(&decoder_opts, "refcounted_frames", "1", 0);
        av_dict_set(&decoder_opts, "skip_loop_filter", "48", 0);
        av_dict_set(&decoder_opts, "skip_frame", "0", 0);
        av_dict_set(&decoder_opts, "strict", "normal", 0);

        if (avcodec_open2(dec_ctx, decoder, &decoder_opts) < 0) {
            std::cerr << "Could not open decoder" << std::endl;
            av_dict_free(&decoder_opts);
            return -1;
        }
        av_dict_free(&decoder_opts);
    }

    std::cout << "Successfully opened decoder: " << decoder->name << std::endl;
    std::cout << "Video dimensions: " << dec_ctx->width << "x" << dec_ctx->height << std::endl;
    std::cout << "Pixel format: " << av_get_pix_fmt_name(dec_ctx->pix_fmt) << std::endl;

    // Setup output format and stream if recording
    AVFormatContext* out_ctx = nullptr;
    AVStream* out_stream = nullptr;
    AVCodecContext* enc_ctx = nullptr;
    if (!no_record) {
        avformat_alloc_output_context2(&out_ctx, nullptr, nullptr, output_file);
        if (!out_ctx) {
            std::cerr << "Could not create output context" << std::endl;
            return -1;
        }

        out_stream = avformat_new_stream(out_ctx, nullptr);
        if (!out_stream) {
            std::cerr << "Could not create output stream" << std::endl;
            return -1;
        }

        // Find the encoder
        const AVCodec* encoder = nullptr;
        if (codec_id == AV_CODEC_ID_H264) {
            encoder = avcodec_find_encoder_by_name("libx264");
        } else if (codec_id == AV_CODEC_ID_HEVC) {
            encoder = avcodec_find_encoder_by_name("libx265");
        }
        
        if (!encoder) {
            // Fallback to default encoder for the codec
            encoder = avcodec_find_encoder(fmt_ctx->streams[video_stream_index]->codecpar->codec_id);
        }
        
        if (!encoder) {
            std::cerr << "Could not find encoder" << std::endl;
            return -1;
        }

        std::cout << "Using encoder: " << encoder->name << std::endl;

        // Allocate encoder context
        enc_ctx = avcodec_alloc_context3(encoder);
        if (!enc_ctx) {
            std::cerr << "Could not allocate encoder context" << std::endl;
            return -1;
        }

        // Set encoder parameters
        enc_ctx->width = dec_ctx->width;
        enc_ctx->height = dec_ctx->height;
        enc_ctx->time_base = (AVRational){1, 30};  // 30 fps
        enc_ctx->framerate = (AVRational){30, 1};
        enc_ctx->pix_fmt = dec_ctx->pix_fmt;
        enc_ctx->bit_rate = 4000000;  // 4 Mbps
        enc_ctx->gop_size = 30;
        enc_ctx->max_b_frames = 0;  // Disable B-frames for real-time encoding

        // Set additional encoder options
        AVDictionary* encoder_opts = nullptr;
        if (strcmp(encoder->name, "libx264") == 0) {
            av_dict_set(&encoder_opts, "preset", "ultrafast", 0);
            av_dict_set(&encoder_opts, "tune", "zerolatency", 0);
            av_dict_set(&encoder_opts, "profile", "baseline", 0);
        } else if (strcmp(encoder->name, "libx265") == 0) {
            av_dict_set(&encoder_opts, "preset", "ultrafast", 0);
            av_dict_set(&encoder_opts, "tune", "zerolatency", 0);
            av_dict_set(&encoder_opts, "rc-lookahead", "0", 0);  // Disable lookahead
            av_dict_set(&encoder_opts, "b-adapt", "0", 0);       // Disable B-frame adaptation
            av_dict_set(&encoder_opts, "bframes", "0", 0);       // Disable B-frames
            av_dict_set(&encoder_opts, "scenecut", "0", 0);      // Disable scene cut detection
        }
        av_dict_set(&encoder_opts, "threads", "4", 0);

        // Open the encoder
        if (avcodec_open2(enc_ctx, encoder, &encoder_opts) < 0) {
            std::cerr << "Could not open encoder" << std::endl;
            av_dict_free(&encoder_opts);
            return -1;
        }
        av_dict_free(&encoder_opts);

        // Set the codec parameters for the output stream
        if (avcodec_parameters_from_context(out_stream->codecpar, enc_ctx) < 0) {
            std::cerr << "Could not copy encoder parameters" << std::endl;
            return -1;
        }

        // Set the time base
        out_stream->time_base = enc_ctx->time_base;

        if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&out_ctx->pb, output_file, AVIO_FLAG_WRITE) < 0) {
                std::cerr << "Could not open output file" << std::endl;
                return -1;
            }
        }

        // Write header
        if (avformat_write_header(out_ctx, nullptr) < 0) {
            std::cerr << "Could not write header" << std::endl;
            return -1;
        }
    }

    // Setup frame processing
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgb_frame = av_frame_alloc();
    if (!frame || !rgb_frame) {
        std::cerr << "Could not allocate frames" << std::endl;
        return -1;
    }

    const int target_width = 800;
    const int target_height = 600;
    SwsContext* sws_ctx = nullptr;
    std::vector<uint8_t> buffer;

    // Timing variables
    double total_conversion_time = 0.0;
    int conversion_count = 0;
    struct timespec start_time, end_time;

    AVPixelFormat target_format;
    if (use_bgr) {
        target_format = AV_PIX_FMT_BGR24;
    } else if (use_nv12) {
        target_format = AV_PIX_FMT_NV12;
    } else {
        target_format = dec_ctx->pix_fmt;  // Keep original YUV format
    }

    if (!no_resize) {
        sws_ctx = sws_getContext(
            dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
            target_width, target_height, target_format,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        if (!sws_ctx) {
            std::cerr << "Could not initialize SwsContext" << std::endl;
            return -1;
        }

        int num_bytes = av_image_get_buffer_size(target_format, 
                                               target_width, target_height, 1);
        buffer.resize(num_bytes);
        av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, buffer.data(),
                            target_format, 
                            target_width, target_height, 1);
    } else {
        // Use original dimensions and format
        int num_bytes = av_image_get_buffer_size(target_format, 
                                               dec_ctx->width, dec_ctx->height, 1);
        buffer.resize(num_bytes);
        
        // Properly initialize the frame
        rgb_frame->format = target_format;
        rgb_frame->width = dec_ctx->width;
        rgb_frame->height = dec_ctx->height;
        
        // Allocate the frame buffer
        int ret = av_frame_get_buffer(rgb_frame, 32);  // 32-byte alignment
        if (ret < 0) {
            std::cerr << "Could not allocate frame buffer" << std::endl;
            return -1;
        }
        
        // Make sure the frame is writable
        ret = av_frame_make_writable(rgb_frame);
        if (ret < 0) {
            std::cerr << "Could not make frame writable" << std::endl;
            return -1;
        }

        // Only create SwsContext if we need format conversion
        if (target_format != dec_ctx->pix_fmt) {
            sws_ctx = sws_getContext(
                dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
                dec_ctx->width, dec_ctx->height, target_format,
                SWS_BILINEAR, nullptr, nullptr, nullptr
            );
            if (!sws_ctx) {
                std::cerr << "Could not initialize SwsContext" << std::endl;
                return -1;
            }
        }
    }

    std::cout << "Starting video processing..." << std::endl;
    std::cout << "Using frame size: " << (no_resize ? 
        std::to_string(dec_ctx->width) + "x" + std::to_string(dec_ctx->height) :
        std::to_string(target_width) + "x" + std::to_string(target_height)) << std::endl;

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        std::cerr << "Could not allocate packet" << std::endl;
        return -1;
    }

    int64_t start_time_total = av_gettime();
    int64_t max_duration = 10 * 1000000; // 10 seconds in microseconds
    int frame_count = 0;
    double total_cpu_usage = 0.0;
    int cpu_samples = 0;
    int error_count = 0;
    const int max_errors = 10;
    int64_t last_cpu_check = 0;
    const int64_t cpu_check_interval = 100000; // Check CPU every 100ms
    int64_t last_fps_time = start_time_total;
    int fps_frame_count = 0;
    double current_fps = 0.0;

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        // Check if we've exceeded the time limit
        int64_t current_time = av_gettime();
        if (current_time - start_time_total > max_duration) {
            std::cout << "\nReached maximum duration (10 seconds)" << std::endl;
            break;
        }

        // Calculate FPS
        if (current_time - last_fps_time >= 1000000) { // Every second
            current_fps = (double)fps_frame_count * 1000000.0 / (current_time - last_fps_time);
            fps_frame_count = 0;
            last_fps_time = current_time;
        }

        if (pkt->stream_index == video_stream_index) {
            int ret = avcodec_send_packet(dec_ctx, pkt);
            if (ret < 0) {
                std::cerr << "Error sending packet to decoder: " << ret << std::endl;
                error_count++;
                if (error_count >= max_errors) {
                    std::cerr << "Too many consecutive errors, stopping" << std::endl;
                    break;
                }
                av_packet_unref(pkt);
                continue;
            }
            error_count = 0;

            while (ret >= 0) {
                ret = avcodec_receive_frame(dec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    std::cerr << "Error receiving frame from decoder: " << ret << std::endl;
                    break;
                }

                // Make sure frame is valid
                if (!frame->data[0] || !frame->linesize[0]) {
                    std::cerr << "Invalid frame data" << std::endl;
                    break;
                }

                // Start timing the conversion
                clock_gettime(CLOCK_MONOTONIC, &start_time);

                if (!no_resize) {
                    // Resize frame
                    sws_scale(sws_ctx,
                            frame->data, frame->linesize, 0, dec_ctx->height,
                            rgb_frame->data, rgb_frame->linesize);
                } else {
                    // For no-resize mode, choose between MPP and OpenCV for YUV to BGR conversion
                    if (use_bgr) {
                        if (use_mpp) {
                            // Get MPP buffer from AVFrame
                            MppBuffer mpp_buffer = nullptr;
                            
                            // First try to get from hw_frames_ctx
                            if (frame->hw_frames_ctx) {
                                AVHWFramesContext* hw_frames_ctx = (AVHWFramesContext*)frame->hw_frames_ctx->data;
                                if (hw_frames_ctx && hw_frames_ctx->hwctx) {
                                    mpp_buffer = (MppBuffer)hw_frames_ctx->hwctx;
                                }
                            }
                            
                            // If not found in hw_frames_ctx, try data[3]
                            if (!mpp_buffer && frame->data[3]) {
                                mpp_buffer = (MppBuffer)frame->data[3];
                            }
                            
                            // If still not found, try opaque
                            if (!mpp_buffer && frame->opaque) {
                                mpp_buffer = (MppBuffer)frame->opaque;
                            }
                            
                            if (mpp_buffer) {
                                std::cout << "Using MPP buffer for conversion" << std::endl;
                                // Create MPP frame for output
                                MppFrame mpp_frame = NULL;
                                mpp_frame_init(&mpp_frame);
                                
                                // Set up MPP frame parameters
                                mpp_frame_set_width(mpp_frame, dec_ctx->width);
                                mpp_frame_set_height(mpp_frame, dec_ctx->height);
                                mpp_frame_set_fmt(mpp_frame, MPP_FMT_BGR888);
                                
                                // Get MPP buffer for output
                                MppBuffer out_buffer = NULL;
                                size_t size = dec_ctx->width * dec_ctx->height * 3;  // BGR format
                                mpp_buffer_get(NULL, &out_buffer, size);
                                
                                if (out_buffer) {
                                    // Set output buffer to MPP frame
                                    mpp_frame_set_buffer(mpp_frame, out_buffer);
                                    
                                    // Convert using MPP
                                    MPP_RET ret = mpp_frame_init(&mpp_frame);
                                    if (ret == MPP_OK) {
                                        // Copy converted data to output frame
                                        void* data = mpp_buffer_get_ptr(out_buffer);
                                        memcpy(rgb_frame->data[0], data, size);
                                    }
                                    
                                    // Release MPP buffer
                                    mpp_buffer_put(out_buffer);
                                }
                                
                                // Release MPP frame
                                mpp_frame_deinit(&mpp_frame);
                            } else {
                                std::cout << "MPP buffer not available, falling back to OpenCV" << std::endl;
                                // Fallback to OpenCV
                                cv::Mat yuv(dec_ctx->height * 3/2, dec_ctx->width, CV_8UC1, frame->data[0]);
                                cv::Mat bgr(dec_ctx->height, dec_ctx->width, CV_8UC3, rgb_frame->data[0]);
                                cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_I420);
                            }
                        } else {
                            // Use OpenCV for conversion
                            cv::Mat yuv(dec_ctx->height * 3/2, dec_ctx->width, CV_8UC1, frame->data[0]);
                            cv::Mat bgr(dec_ctx->height, dec_ctx->width, CV_8UC3, rgb_frame->data[0]);
                            cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_I420);
                        }
                    } else {
                        // If formats match, just copy the frame
                        if (av_frame_copy(rgb_frame, frame) < 0) {
                            std::cerr << "Error copying frame" << std::endl;
                            break;
                        }
                    }
                }

                // End timing the conversion
                clock_gettime(CLOCK_MONOTONIC, &end_time);
                double conversion_time = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                                      (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;  // Convert to milliseconds
                total_conversion_time += conversion_time;
                conversion_count++;

                if (!no_record) {
                    // Set frame timestamp
                    frame->pts = frame_count;

                    // Send frame to encoder
                    ret = avcodec_send_frame(enc_ctx, frame);
                    if (ret < 0) {
                        std::cerr << "Error sending frame to encoder" << std::endl;
                        break;
                    }

                    // Receive encoded packets
                    while (ret >= 0) {
                        AVPacket* out_pkt = av_packet_alloc();
                        ret = avcodec_receive_packet(enc_ctx, out_pkt);
                        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                            av_packet_free(&out_pkt);
                            break;
                        } else if (ret < 0) {
                            std::cerr << "Error receiving packet from encoder" << std::endl;
                            av_packet_free(&out_pkt);
                            break;
                        }

                        // Set packet timestamp
                        out_pkt->pts = av_rescale_q_rnd(out_pkt->pts,
                            enc_ctx->time_base,
                            out_stream->time_base,
                            (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                        out_pkt->dts = av_rescale_q_rnd(out_pkt->dts,
                            enc_ctx->time_base,
                            out_stream->time_base,
                            (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                        out_pkt->duration = av_rescale_q(out_pkt->duration,
                            enc_ctx->time_base,
                            out_stream->time_base);
                        out_pkt->stream_index = 0;

                        // Write the packet
                        if (av_interleaved_write_frame(out_ctx, out_pkt) < 0) {
                            std::cerr << "Error writing frame" << std::endl;
                        }
                        av_packet_free(&out_pkt);
                    }
                }

                frame_count++;
                fps_frame_count++;
                
                // Check CPU usage based on time interval
                current_time = av_gettime();
                if (current_time - last_cpu_check >= cpu_check_interval) {
                    double cpu_usage = get_cpu_usage();
                    total_cpu_usage += cpu_usage;
                    cpu_samples++;
                    double avg_conversion_time = conversion_count > 0 ? total_conversion_time / conversion_count : 0.0;
                    std::cout << "\rFrames processed: " << frame_count 
                             << " CPU Usage: " << cpu_usage << "%"
                             << " FPS: " << std::fixed << std::setprecision(1) << current_fps
                             << " Avg conversion time: " << std::fixed << std::setprecision(3) << avg_conversion_time << "ms" << std::flush;
                    last_cpu_check = current_time;
                }
            }
        }
        av_packet_unref(pkt);
    }

    // Flush encoder
    if (!no_record) {
        avcodec_send_frame(enc_ctx, nullptr);
        while (true) {
            AVPacket* out_pkt = av_packet_alloc();
            int ret = avcodec_receive_packet(enc_ctx, out_pkt);
            if (ret == AVERROR_EOF || ret < 0) {
                av_packet_free(&out_pkt);
                break;
            }

            out_pkt->pts = av_rescale_q_rnd(out_pkt->pts,
                enc_ctx->time_base,
                out_stream->time_base,
                (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            out_pkt->dts = av_rescale_q_rnd(out_pkt->dts,
                enc_ctx->time_base,
                out_stream->time_base,
                (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            out_pkt->duration = av_rescale_q(out_pkt->duration,
                enc_ctx->time_base,
                out_stream->time_base);
            out_pkt->stream_index = 0;

            if (av_interleaved_write_frame(out_ctx, out_pkt) < 0) {
                std::cerr << "Error writing frame" << std::endl;
            }
            av_packet_free(&out_pkt);
        }
    }

    // Write trailer if recording
    if (!no_record) {
        av_write_trailer(out_ctx);
    }

    // Calculate and display average CPU usage and FPS
    double avg_cpu_usage = cpu_samples > 0 ? total_cpu_usage / cpu_samples : 0.0;
    double avg_fps = (double)frame_count * 1000000.0 / (av_gettime() - start_time_total);
    double avg_conversion_time = conversion_count > 0 ? total_conversion_time / conversion_count : 0.0;
    std::cout << "\nProcessing completed:" << std::endl;
    std::cout << "Total frames processed: " << frame_count << std::endl;
    std::cout << "Average CPU usage: " << avg_cpu_usage << "%" << std::endl;
    std::cout << "Average FPS: " << std::fixed << std::setprecision(1) << avg_fps << std::endl;
    std::cout << "Average conversion time: " << std::fixed << std::setprecision(3) << avg_conversion_time << "ms" << std::endl;
    std::cout << "Mode: " << (no_resize ? "No resize" : "With resize") 
              << ", " << (no_record ? "No record" : "With record")
              << ", Color format: " << (use_bgr ? "BGR" : (use_nv12 ? "NV12" : "YUV")) << std::endl;
    std::cout << "Total conversion time: " << std::fixed << std::setprecision(3) << total_conversion_time << "ms" << std::endl;
    std::cout << "Conversion overhead: " << std::fixed << std::setprecision(1) 
              << (total_conversion_time / (av_gettime() - start_time_total) * 100.0) << "%" << std::endl;

    // Cleanup
    if (!no_record) {
        if (out_ctx && !(out_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&out_ctx->pb);
        }
        avformat_free_context(out_ctx);
        avcodec_free_context(&enc_ctx);
    }
    if (!no_resize) {
        sws_freeContext(sws_ctx);
    }
    av_frame_free(&rgb_frame);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);
    avformat_network_deinit();

    return 0;
}
