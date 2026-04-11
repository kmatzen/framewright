/// Compare framewright::VideoReader against cv::VideoCapture on the same file.
///
/// Usage: compare_readers <video_file>
///
/// Writes PNG snapshots so you can diff pixel values and see
/// exactly where OpenCV's silent color matrix choice diverges.

#include <framewright/VideoReader.h>

#include <iostream>
#include <opencv2/opencv.hpp>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <video_file>" << std::endl;
        return 1;
    }

    std::string videoPath = argv[1];

    std::cout << "\n=== framewright::VideoReader (auto-detect) ===" << std::endl;
    {
        framewright::VideoReader reader;
        if (!reader.open(videoPath)) {
            std::cerr << "Failed to open video" << std::endl;
            return 1;
        }

        cv::Mat frame;
        if (!reader.read(frame)) {
            std::cerr << "Failed to read first frame" << std::endl;
            return 1;
        }

        cv::Scalar mean = cv::mean(frame);
        std::cout << "  Mean BGR: (" << mean[0] << ", " << mean[1] << ", " << mean[2] << ")"
                  << std::endl;
        cv::imwrite("/tmp/framewright_default.png", frame);
        std::cout << "  Saved to /tmp/framewright_default.png" << std::endl;
    }

    std::cout << "\n=== framewright::VideoReader (force BT.709) ===" << std::endl;
    {
        framewright::VideoReader reader;
        if (!reader.open(videoPath, true, false)) {
            std::cerr << "Failed to open video" << std::endl;
            return 1;
        }

        cv::Mat frame;
        if (!reader.read(frame)) {
            std::cerr << "Failed to read first frame" << std::endl;
            return 1;
        }

        cv::Scalar mean = cv::mean(frame);
        std::cout << "  Mean BGR: (" << mean[0] << ", " << mean[1] << ", " << mean[2] << ")"
                  << std::endl;
        cv::imwrite("/tmp/framewright_bt709.png", frame);
        std::cout << "  Saved to /tmp/framewright_bt709.png" << std::endl;
    }

    std::cout << "\n=== framewright::VideoReader (force BT.709 + full range) ===" << std::endl;
    {
        framewright::VideoReader reader;
        if (!reader.open(videoPath, true, true)) {
            std::cerr << "Failed to open video" << std::endl;
            return 1;
        }

        cv::Mat frame;
        if (!reader.read(frame)) {
            std::cerr << "Failed to read first frame" << std::endl;
            return 1;
        }

        cv::Scalar mean = cv::mean(frame);
        std::cout << "  Mean BGR: (" << mean[0] << ", " << mean[1] << ", " << mean[2] << ")"
                  << std::endl;
        cv::imwrite("/tmp/framewright_bt709_full.png", frame);
        std::cout << "  Saved to /tmp/framewright_bt709_full.png" << std::endl;
    }

    std::cout << "\n=== cv::VideoCapture (for comparison) ===" << std::endl;
    {
        cv::VideoCapture cap(videoPath);
        if (!cap.isOpened()) {
            std::cerr << "Failed to open video with OpenCV" << std::endl;
            return 1;
        }

        cv::Mat frame;
        if (!cap.read(frame)) {
            std::cerr << "Failed to read first frame" << std::endl;
            return 1;
        }

        cv::Scalar mean = cv::mean(frame);
        std::cout << "  Mean BGR: (" << mean[0] << ", " << mean[1] << ", " << mean[2] << ")"
                  << std::endl;
        cv::imwrite("/tmp/opencv.png", frame);
        std::cout << "  Saved to /tmp/opencv.png" << std::endl;
    }

    std::cout << "\n=== Compare with ImageMagick ===" << std::endl;
    std::cout << "  compare -metric RMSE /tmp/opencv.png /tmp/framewright_default.png /dev/null 2>&1"
              << std::endl;
    std::cout << "  compare -metric RMSE /tmp/opencv.png /tmp/framewright_bt709.png /dev/null 2>&1"
              << std::endl;

    return 0;
}
