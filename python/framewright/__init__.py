"""framewright — Color-correct video I/O for Python.

Drop-in replacement for cv2.VideoCapture/VideoWriter with explicit
color space control and HDR10 support.
"""

from _framewright import (
    LogLevel,
    set_log_level,
    get_log_level,
    HDR10Metadata,
    VideoReader,
    VideoWriter,
)

__all__ = [
    "LogLevel",
    "set_log_level",
    "get_log_level",
    "HDR10Metadata",
    "VideoReader",
    "VideoWriter",
]
