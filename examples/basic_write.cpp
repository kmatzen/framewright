/// Write a short H.264 video with a color gradient.
///
/// Usage: basic_write <output.mp4>

#include <framewright/VideoWriter.h>

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <output.mp4>" << std::endl;
        return 1;
    }

    const int width = 1280;
    const int height = 720;
    const int num_frames = 90;  // 3 seconds at 30fps

    framewright::VideoWriter writer;
    if (!writer.open(argv[1], AV_CODEC_ID_H264, width, height, {30, 1})) {
        std::cerr << "Failed to open output" << std::endl;
        return 1;
    }

    for (int i = 0; i < num_frames; i++) {
        cv::Mat frame(height, width, CV_8UC3);

        // Animated gradient: hue shifts over time
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint8_t b = static_cast<uint8_t>(255 * x / width);
                uint8_t g = static_cast<uint8_t>(255 * y / height);
                uint8_t r = static_cast<uint8_t>(255 * i / num_frames);
                frame.at<cv::Vec3b>(y, x) = {b, g, r};
            }
        }

        if (!writer.write(frame)) {
            std::cerr << "Failed to write frame " << i << std::endl;
            return 1;
        }
    }

    writer.release();
    std::cout << "Wrote " << num_frames << " frames to " << argv[1] << std::endl;
    return 0;
}
