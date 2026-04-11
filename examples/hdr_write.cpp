/// Write an HDR10 HEVC video with BT.2020 + PQ metadata.
///
/// Usage: hdr_write <output.mp4>
///
/// Requires libx265. The output plays with correct HDR tone mapping
/// on macOS (QuickTime) and iOS.

#include <cvffmpeg/VideoWriter.h>

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <output.mp4>" << std::endl;
        return 1;
    }

    const int width = 1920;
    const int height = 1080;
    const int num_frames = 60;  // 2 seconds at 30fps

    cvffmpeg::VideoWriterOptions opts;
    opts.pix_fmt = AV_PIX_FMT_YUV420P10LE;
    opts.is_10bit = true;

    cvffmpeg::VideoWriter writer;
    if (!writer.open(argv[1], AV_CODEC_ID_HEVC, width, height,
                     {30, 1}, opts)) {
        std::cerr << "Failed to open output (is libx265 installed?)" << std::endl;
        return 1;
    }

    for (int i = 0; i < num_frames; i++) {
        // CV_16UC3: 16-bit BGR, values 0-65535
        // PQ (SMPTE 2084) maps these to HDR luminance
        cv::Mat frame(height, width, CV_16UC3);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                // Bright gradient with HDR peak highlights
                uint16_t b = static_cast<uint16_t>(40000 * x / width);
                uint16_t g = static_cast<uint16_t>(40000 * y / height);
                uint16_t r = static_cast<uint16_t>(20000 + 45000 * i / num_frames);
                frame.at<cv::Vec3w>(y, x) = {b, g, r};
            }
        }

        if (!writer.write(frame)) {
            std::cerr << "Failed to write frame " << i << std::endl;
            return 1;
        }
    }

    writer.release();
    std::cout << "Wrote " << num_frames << " HDR10 frames to " << argv[1] << std::endl;
    return 0;
}
