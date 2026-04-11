#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <framewright/LogLevel.h>
#include <framewright/VideoReader.h>
#include <framewright/VideoWriter.h>

namespace py = pybind11;

// Convert codec name string to AVCodecID
static AVCodecID codec_name_to_id(const std::string& name) {
    if (name == "h264" || name == "x264" || name == "avc") return AV_CODEC_ID_H264;
    if (name == "h265" || name == "x265" || name == "hevc") return AV_CODEC_ID_HEVC;
    if (name == "ffv1") return AV_CODEC_ID_FFV1;
    throw py::value_error("Unknown codec: '" + name + "'. Use 'h264', 'hevc', or 'ffv1'.");
}

// Convert pixel format name string to AVPixelFormat
static AVPixelFormat pix_fmt_name_to_id(const std::string& name) {
    if (name == "yuv420p") return AV_PIX_FMT_YUV420P;
    if (name == "yuv444p") return AV_PIX_FMT_YUV444P;
    if (name == "yuv420p10le") return AV_PIX_FMT_YUV420P10LE;
    if (name == "yuv422p10le") return AV_PIX_FMT_YUV422P10LE;
    if (name == "yuv444p10le") return AV_PIX_FMT_YUV444P10LE;
    throw py::value_error("Unknown pixel format: '" + name + "'.");
}

// cv::Mat -> numpy array with ownership via capsule
static py::object mat_to_numpy_owned(const cv::Mat& mat, py::capsule base) {
    if (mat.empty()) {
        return py::none();
    }

    std::vector<py::ssize_t> shape;
    std::vector<py::ssize_t> strides;

    if (mat.channels() == 1) {
        shape = {mat.rows, mat.cols};
        strides = {static_cast<py::ssize_t>(mat.step[0]),
                   static_cast<py::ssize_t>(mat.elemSize())};
    } else {
        shape = {mat.rows, mat.cols, mat.channels()};
        strides = {static_cast<py::ssize_t>(mat.step[0]),
                   static_cast<py::ssize_t>(mat.elemSize()),
                   static_cast<py::ssize_t>(mat.elemSize1())};
    }

    if (mat.depth() == CV_8U) {
        return py::array_t<uint8_t>(shape, strides, mat.data, base);
    } else if (mat.depth() == CV_16U) {
        return py::array_t<uint16_t>(shape, strides,
                                     reinterpret_cast<const uint16_t*>(mat.data), base);
    }

    throw py::type_error("Unsupported Mat depth");
}

// numpy array -> cv::Mat (zero-copy)
static cv::Mat numpy_to_mat(py::array arr) {
    py::buffer_info buf = arr.request();

    if (buf.ndim != 3 || buf.shape[2] != 3) {
        throw py::value_error("Expected array with shape (H, W, 3)");
    }

    int rows = static_cast<int>(buf.shape[0]);
    int cols = static_cast<int>(buf.shape[1]);

    if (buf.format == py::format_descriptor<uint8_t>::format()) {
        return cv::Mat(rows, cols, CV_8UC3, buf.ptr,
                       static_cast<size_t>(buf.strides[0]));
    } else if (buf.format == py::format_descriptor<uint16_t>::format()) {
        return cv::Mat(rows, cols, CV_16UC3, buf.ptr,
                       static_cast<size_t>(buf.strides[0]));
    }

    throw py::type_error("Expected uint8 or uint16 array");
}

PYBIND11_MODULE(_framewright, m) {
    m.doc() = "Color-correct video I/O — Python bindings for framewright";

    // LogLevel
    py::enum_<framewright::LogLevel>(m, "LogLevel")
        .value("Quiet", framewright::LogLevel::Quiet)
        .value("Error", framewright::LogLevel::Error)
        .value("Warning", framewright::LogLevel::Warning)
        .value("Info", framewright::LogLevel::Info);

    m.def("set_log_level", &framewright::setLogLevel, py::arg("level"),
          "Set the library-wide log level. Default is Error.");
    m.def("get_log_level", &framewright::getLogLevel,
          "Get the current log level.");

    // HDR10Metadata
    py::class_<framewright::HDR10Metadata>(m, "HDR10Metadata")
        .def(py::init<>())
        .def_readwrite("red_x", &framewright::HDR10Metadata::red_x)
        .def_readwrite("red_y", &framewright::HDR10Metadata::red_y)
        .def_readwrite("green_x", &framewright::HDR10Metadata::green_x)
        .def_readwrite("green_y", &framewright::HDR10Metadata::green_y)
        .def_readwrite("blue_x", &framewright::HDR10Metadata::blue_x)
        .def_readwrite("blue_y", &framewright::HDR10Metadata::blue_y)
        .def_readwrite("white_x", &framewright::HDR10Metadata::white_x)
        .def_readwrite("white_y", &framewright::HDR10Metadata::white_y)
        .def_readwrite("max_luminance", &framewright::HDR10Metadata::max_luminance)
        .def_readwrite("min_luminance", &framewright::HDR10Metadata::min_luminance)
        .def_readwrite("max_cll", &framewright::HDR10Metadata::max_cll)
        .def_readwrite("max_fall", &framewright::HDR10Metadata::max_fall);

    // VideoReader
    py::class_<framewright::VideoReader>(m, "VideoReader",
        "Color-correct video reader. Drop-in replacement for cv2.VideoCapture.")
        .def(py::init<>())
        .def("open", &framewright::VideoReader::open,
             py::arg("filename"),
             py::arg("force_bt709") = false,
             py::arg("force_full_range") = false,
             "Open a video file.")
        .def("read", [](framewright::VideoReader& self) -> py::object {
                cv::Mat frame;
                if (!self.read(frame)) {
                    return py::none();
                }
                // Mat from read() is already cloned. Transfer ownership to numpy.
                auto* mat_ptr = new cv::Mat(std::move(frame));
                auto capsule = py::capsule(mat_ptr,
                    [](void* p) { delete static_cast<cv::Mat*>(p); });
                return mat_to_numpy_owned(*mat_ptr, capsule);
             },
             "Read the next frame as a numpy array (H, W, 3) BGR uint8.\n"
             "Returns None at end of file.")
        .def("seek", &framewright::VideoReader::seek,
             py::arg("frame_number"),
             "Seek to a specific frame number (forward and backward).")
        .def("close", &framewright::VideoReader::close)
        .def_property_readonly("width", &framewright::VideoReader::getWidth)
        .def_property_readonly("height", &framewright::VideoReader::getHeight)
        .def_property_readonly("fps", &framewright::VideoReader::getFPS)
        .def_property_readonly("frame_count", &framewright::VideoReader::getFrameCount)
        .def_property_readonly("frame_number", &framewright::VideoReader::getCurrentFrameNumber)
        .def_property_readonly("timestamp", &framewright::VideoReader::getCurrentTimestamp)
        .def_property_readonly("pixel_format", [](const framewright::VideoReader& self) -> std::string {
            AVPixelFormat fmt = self.getPixelFormat();
            const char* name = av_get_pix_fmt_name(fmt);
            return name ? name : "none";
        })
        .def_property_readonly("codec", [](const framewright::VideoReader& self) -> std::string {
            AVCodecID id = self.getCodecID();
            const AVCodec* codec = avcodec_find_decoder(id);
            return codec ? codec->name : "none";
        })
        .def("__enter__", [](framewright::VideoReader& self) -> framewright::VideoReader& {
            return self;
        })
        .def("__exit__", [](framewright::VideoReader& self, py::object, py::object, py::object) {
            self.close();
        })
        .def("__iter__", [](framewright::VideoReader& self) -> framewright::VideoReader& {
            return self;
        })
        .def("__next__", [](framewright::VideoReader& self) -> py::object {
            cv::Mat frame;
            if (!self.read(frame)) {
                throw py::stop_iteration();
            }
            auto* mat_ptr = new cv::Mat(std::move(frame));
            auto capsule = py::capsule(mat_ptr,
                [](void* p) { delete static_cast<cv::Mat*>(p); });
            return mat_to_numpy_owned(*mat_ptr, capsule);
        });

    // VideoWriter
    py::class_<framewright::VideoWriter>(m, "VideoWriter",
        "Color-correct video writer with HDR10 support.")
        .def(py::init<>())
        .def("open", [](framewright::VideoWriter& self,
                        const std::string& filename,
                        const std::string& codec,
                        int width, int height,
                        double fps,
                        int bitrate,
                        const std::string& pix_fmt,
                        bool is_10bit, bool full_range,
                        bool use_444, bool lossless) {
                // Convert fps double to AVRational
                AVRational framerate;
                // Handle common frame rates precisely
                if (std::abs(fps - 23.976) < 0.001) {
                    framerate = {24000, 1001};
                } else if (std::abs(fps - 29.97) < 0.001) {
                    framerate = {30000, 1001};
                } else if (std::abs(fps - 59.94) < 0.001) {
                    framerate = {60000, 1001};
                } else {
                    int fps_int = static_cast<int>(fps);
                    if (std::abs(fps - fps_int) < 0.0001) {
                        framerate = {fps_int, 1};
                    } else {
                        framerate = {static_cast<int>(fps * 1000), 1000};
                    }
                }

                framewright::VideoWriterOptions opts;
                opts.bitrate = bitrate;
                opts.pix_fmt = pix_fmt_name_to_id(pix_fmt);
                opts.is_10bit = is_10bit;
                opts.full_range = full_range;
                opts.use_444 = use_444;
                opts.lossless = lossless;

                return self.open(filename, codec_name_to_id(codec),
                                 width, height, framerate, opts);
             },
             py::arg("filename"),
             py::arg("codec") = "h264",
             py::arg("width") = 0,
             py::arg("height") = 0,
             py::arg("fps") = 30.0,
             py::arg("bitrate") = 25000000,
             py::arg("pix_fmt") = "yuv420p",
             py::arg("is_10bit") = false,
             py::arg("full_range") = false,
             py::arg("use_444") = false,
             py::arg("lossless") = false,
             "Open an output file for writing.")
        .def("write", [](framewright::VideoWriter& self, py::array frame) {
                cv::Mat mat = numpy_to_mat(frame);
                return self.write(mat);
             },
             py::arg("frame"),
             "Write a BGR frame (uint8 or uint16 numpy array with shape (H, W, 3)).")
        .def("release", &framewright::VideoWriter::release,
             "Flush and finalize the file.")
        .def("set_hdr10_metadata", &framewright::VideoWriter::setHDR10Metadata,
             py::arg("metadata"))
        .def_property_readonly("timestamp", &framewright::VideoWriter::getCurrentTimestamp)
        .def("__enter__", [](framewright::VideoWriter& self) -> framewright::VideoWriter& {
            return self;
        })
        .def("__exit__", [](framewright::VideoWriter& self, py::object, py::object, py::object) {
            self.release();
        });
}
