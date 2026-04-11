/// Read a video file and print frame info.
///
/// Usage: basic_read <video_file>

#include <cvffmpeg/VideoReader.h>

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <video_file>" << std::endl;
        return 1;
    }

    cvffmpeg::VideoReader reader;
    if (!reader.open(argv[1], /*force_bt709=*/true)) {
        std::cerr << "Failed to open video" << std::endl;
        return 1;
    }

    std::cout << "Resolution: " << reader.getWidth() << "x" << reader.getHeight() << std::endl;
    std::cout << "FPS: " << reader.getFPS() << std::endl;
    std::cout << "Frame count: " << reader.getFrameCount() << std::endl;

    cv::Mat frame;
    int count = 0;
    while (reader.read(frame)) {
        count++;
        if (count <= 3) {
            cv::Scalar mean = cv::mean(frame);
            std::cout << "Frame " << count << ": " << frame.cols << "x" << frame.rows
                      << " mean BGR=(" << mean[0] << ", " << mean[1] << ", " << mean[2] << ")"
                      << std::endl;
        }
    }

    std::cout << "Total frames read: " << count << std::endl;
    return 0;
}
