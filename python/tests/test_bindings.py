"""Tests for framewright Python bindings."""

import os
import tempfile

import numpy as np
import pytest

import framewright


@pytest.fixture
def fixtures_dir():
    """Return the test fixtures directory (set by CMake)."""
    d = os.environ.get("TEST_FIXTURES_DIR")
    if not d or not os.path.isdir(d):
        pytest.skip("TEST_FIXTURES_DIR not set or missing")
    return d


@pytest.fixture
def bt709_limited(fixtures_dir):
    p = os.path.join(fixtures_dir, "bt709_limited.mp4")
    if not os.path.isfile(p):
        pytest.skip("bt709_limited.mp4 fixture missing")
    return p


@pytest.fixture
def bt709_full(fixtures_dir):
    p = os.path.join(fixtures_dir, "bt709_full.mp4")
    if not os.path.isfile(p):
        pytest.skip("bt709_full.mp4 fixture missing")
    return p


# --------------------------------------------------------------------------- #
# VideoReader basics
# --------------------------------------------------------------------------- #


class TestVideoReaderBasics:
    def test_open_nonexistent_returns_false(self):
        reader = framewright.VideoReader()
        assert not reader.open("/nonexistent/path.mp4")

    def test_open_and_read(self, bt709_limited):
        reader = framewright.VideoReader()
        assert reader.open(bt709_limited)
        assert reader.width == 1280
        assert reader.height == 720
        assert reader.fps > 0

        frame = reader.read()
        assert frame is not None
        assert isinstance(frame, np.ndarray)
        assert frame.shape == (720, 1280, 3)
        assert frame.dtype == np.uint8
        reader.close()

    def test_context_manager(self, bt709_limited):
        with framewright.VideoReader() as reader:
            reader.open(bt709_limited)
            frame = reader.read()
            assert frame is not None

    def test_iteration(self, bt709_limited):
        reader = framewright.VideoReader()
        reader.open(bt709_limited)
        frames = list(reader)
        assert len(frames) >= 1
        for f in frames:
            assert isinstance(f, np.ndarray)

    def test_returns_none_at_eof(self, bt709_limited):
        reader = framewright.VideoReader()
        reader.open(bt709_limited)
        while reader.read() is not None:
            pass
        assert reader.read() is None
        reader.close()


# --------------------------------------------------------------------------- #
# VideoReader color metadata
# --------------------------------------------------------------------------- #


class TestVideoReaderColorMetadata:
    def test_color_properties(self, bt709_limited):
        reader = framewright.VideoReader()
        reader.open(bt709_limited)

        assert isinstance(reader.color_space, str)
        assert isinstance(reader.color_range, str)
        assert isinstance(reader.color_primaries, str)
        assert isinstance(reader.color_transfer, str)
        # color_space is reliably reported; primaries/transfer may be
        # "unknown" depending on what the container signals
        assert reader.color_space == "bt709"
        assert reader.color_range in ("tv", "pc")
        reader.close()

    def test_forcing_flags_default(self, bt709_limited):
        reader = framewright.VideoReader()
        reader.open(bt709_limited)
        assert reader.forcing_bt709 is False
        assert reader.forcing_full_range is False
        reader.close()

    def test_forcing_flags_set(self, bt709_limited):
        reader = framewright.VideoReader()
        reader.open(bt709_limited, force_bt709=True, force_full_range=True)
        assert reader.forcing_bt709 is True
        assert reader.forcing_full_range is True
        reader.close()

    def test_pixel_format_and_codec(self, bt709_limited):
        reader = framewright.VideoReader()
        reader.open(bt709_limited)
        assert isinstance(reader.pixel_format, str)
        assert isinstance(reader.codec, str)
        assert reader.codec in ("h264", "libx264")
        reader.close()


# --------------------------------------------------------------------------- #
# VideoWriter basics
# --------------------------------------------------------------------------- #


class TestVideoWriterBasics:
    def test_write_h264(self, tmp_path):
        path = str(tmp_path / "test.mp4")
        writer = framewright.VideoWriter()
        assert writer.open(path, codec="h264", width=320, height=240, fps=30)

        frame = np.zeros((240, 320, 3), dtype=np.uint8)
        frame[:, :, 2] = 200  # red in BGR
        assert writer.write(frame)
        writer.release()

        assert os.path.isfile(path)
        assert os.path.getsize(path) > 0

    def test_write_with_options(self, tmp_path):
        path = str(tmp_path / "test_opts.mp4")
        writer = framewright.VideoWriter()
        assert writer.open(path, codec="h264", width=320, height=240,
                           fps=30, full_range=True, lossless=True)

        frame = np.full((240, 320, 3), 128, dtype=np.uint8)
        assert writer.write(frame)
        writer.release()

        assert os.path.getsize(path) > 0

    def test_context_manager(self, tmp_path):
        path = str(tmp_path / "test_ctx.mp4")
        with framewright.VideoWriter() as writer:
            writer.open(path, codec="h264", width=320, height=240, fps=30)
            frame = np.zeros((240, 320, 3), dtype=np.uint8)
            writer.write(frame)

        assert os.path.isfile(path)

    def test_timestamp_advances(self, tmp_path):
        path = str(tmp_path / "test_ts.mp4")
        writer = framewright.VideoWriter()
        writer.open(path, codec="h264", width=320, height=240, fps=30)

        t0 = writer.timestamp
        frame = np.zeros((240, 320, 3), dtype=np.uint8)
        writer.write(frame)
        t1 = writer.timestamp
        assert t1 > t0
        writer.release()

    def test_write_rejects_bad_shape(self, tmp_path):
        path = str(tmp_path / "test_bad.mp4")
        writer = framewright.VideoWriter()
        writer.open(path, codec="h264", width=320, height=240, fps=30)

        with pytest.raises(ValueError):
            writer.write(np.zeros((240, 320), dtype=np.uint8))
        writer.release()


# --------------------------------------------------------------------------- #
# Round-trip
# --------------------------------------------------------------------------- #


class TestRoundTrip:
    def test_write_then_read(self, tmp_path):
        path = str(tmp_path / "roundtrip.mp4")

        # Write
        writer = framewright.VideoWriter()
        writer.open(path, codec="h264", width=320, height=240, fps=30)
        frame_out = np.full((240, 320, 3), 128, dtype=np.uint8)
        for _ in range(5):
            writer.write(frame_out)
        writer.release()

        # Read back
        reader = framewright.VideoReader()
        assert reader.open(path)
        assert reader.width == 320
        assert reader.height == 240

        frames = list(reader)
        assert len(frames) == 5
        for f in frames:
            assert f.shape == (240, 320, 3)


# --------------------------------------------------------------------------- #
# HDR10Metadata
# --------------------------------------------------------------------------- #


class TestHDR10Metadata:
    def test_defaults(self):
        m = framewright.HDR10Metadata()
        assert m.red_x == pytest.approx(0.708)
        assert m.max_cll == 1000
        assert m.max_fall == 400

    def test_readwrite(self):
        m = framewright.HDR10Metadata()
        m.red_x = 0.680
        m.max_cll = 2000
        assert m.red_x == pytest.approx(0.680)
        assert m.max_cll == 2000


# --------------------------------------------------------------------------- #
# LogLevel
# --------------------------------------------------------------------------- #


class TestLogLevel:
    def test_set_and_get(self):
        framewright.set_log_level(framewright.LogLevel.Quiet)
        assert framewright.get_log_level() == framewright.LogLevel.Quiet

        framewright.set_log_level(framewright.LogLevel.Error)
        assert framewright.get_log_level() == framewright.LogLevel.Error
