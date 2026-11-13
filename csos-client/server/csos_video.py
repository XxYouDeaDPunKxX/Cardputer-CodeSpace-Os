"""Negotiated RGB565 video for desktop rectangles within a wire budget."""
from array import array
from collections import deque
import struct
import sys
import time
import zlib

CAPABILITY = 0x20
VERSION = 1
BAND_ROWS = 16
REFRESH_BUDGET = 4096
SETTLE_SECONDS = .25
INTERACTIVE_INPUT = frozenset(b"KMBWZQNRCDVL")
# Initial budget for the physical Cardputer trial, not a protocol limit.
EXTRA_BYTES_PER_FRAME = 12 * 1024


def lossless_tiles(frame, jpeg_packet, max_payload, extra_bytes):
    x, y, width, height, _ = struct.unpack_from('<HHHHI', jpeg_packet, 1)
    remaining = len(jpeg_packet) + extra_bytes
    packets = []
    for top in range(y, y + height, BAND_ROWS):
        bottom = min(top + BAND_ROWS, y + height)
        pixels = frame.crop((x, top, x + width, bottom)).getdata()
        rgb565 = array('H', (((r & 248) << 8) | ((g & 252) << 3) | (b >> 3)
                            for r, g, b in pixels))
        if sys.byteorder == 'little':
            rgb565.byteswap()
        payload = zlib.compress(rgb565, level=1)
        packet_size = 13 + len(payload)
        if len(payload) > max_payload or packet_size > remaining:
            return None
        packets.append(b'J' + struct.pack('<HHHHI', x, top, width, bottom - top,
                                         len(payload)) + payload)
        remaining -= packet_size
    return packets


def with_lossless_video(Session, tiles_to_rects, tile_width=32, tile_height=16,
                        clock=time.monotonic):
    class VideoSession(Session):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            self.video_enabled = False
            self.video_generation = 0
            self.video_input_at = 0.0
            self.video_dirty = set()
            self.video_packets = deque()
            self.video_snapshot = None
            self.video_geometry = None
            self.video_capture = None
            self.video_header = None
            self.video_ready_at = 0.0
            self.video_jpeg_bytes = 0

        def handle(self, kind, payload):
            if kind == ord('Y'):
                if payload == bytes([VERSION]):
                    with self.lock:
                        self.video_enabled = True
                return
            if kind in INTERACTIVE_INPUT:
                with self.lock:
                    self.video_generation += 1
                    self.video_input_at = clock()
            return super().handle(kind, payload)

        def render(self, *args):
            with self.lock:
                generation = self.video_generation
            result = super().render(*args)
            if not self.video_enabled:
                return result
            frame = result[0]
            self.video_capture = frame
            geometry = (self._touch_pending, *result[1:], self.quality)
            pixels = frame.tobytes()
            if geometry != self.video_geometry:
                self.video_dirty.clear()
                self.video_packets.clear()
                self.video_jpeg_bytes = 0
                self.video_geometry = geometry
                self.video_snapshot = None
            elif self.video_snapshot is not None and pixels != self.video_snapshot:
                # Unsent packets belong to the previous pixels. Dirty coverage
                # still includes their regions, so encode the new view later.
                self.video_packets.clear()
                self.video_snapshot = None
            self.refresh_video(pixels, generation)
            return result

        def send(self, data):
            if data[:1] == b'T' and self.video_enabled and self.quality:
                x, y, width, height, _ = struct.unpack_from('<HHHHI', data, 1)
                self.video_dirty.update((col, row)
                    for row in range(y // tile_height, (y + height + tile_height - 1) // tile_height)
                    for col in range(x // tile_width, (x + width + tile_width - 1) // tile_width))
                self.video_jpeg_bytes = min(2 * self.maxjpeg, self.video_jpeg_bytes + len(data))
            if data[:1] == b'F' and len(data) == 12:
                data = data[:-1] + bytes([data[-1] | CAPABILITY])
                if self.video_enabled and self.video_capture is not None and self.video_dirty:
                    self.video_header = data
                    self.video_snapshot = self.video_capture.tobytes()
                    self.video_ready_at = clock() + SETTLE_SECONDS
                    self.video_packets.clear()
            return super().send(data)

        def refresh_video(self, pixels, generation):
            now = clock()
            with self.lock:
                eligible = (self.alive and self.quality and self.video_dirty and not self.lowq
                    and not self.force_full and self.acked_id == self.sent_id
                    and self.video_snapshot == pixels and now >= self.video_ready_at
                    and now >= self.video_input_at + SETTLE_SECONDS
                    and generation == self.video_generation)
            if not eligible:
                return
            if not self.video_packets:
                # A whole repair must fit the tested desktop traffic budget.
                remaining = min(2 * self.maxjpeg, self.video_jpeg_bytes + EXTRA_BYTES_PER_FRAME)
                rectangles = tiles_to_rects(self.video_dirty, self.video_capture.height)
                for x, y, right, bottom in rectangles:
                    header = b'T' + struct.pack('<HHHHI', x, y, right-x, bottom-y, 0)
                    packets = lossless_tiles(self.video_capture, header,
                        min(self.maxjpeg, REFRESH_BUDGET - 25), remaining - len(header))
                    if packets is None:
                        self.video_dirty.clear()
                        self.video_packets.clear()
                        self.video_jpeg_bytes = 0
                        return
                    self.video_packets.extend(packets)
                    remaining -= sum(map(len, packets))
            with self.lock:
                if (not self.alive or self.force_full or generation != self.video_generation
                        or self.acked_id != self.sent_id):
                    return
                self.sent_id += 1
                header = b'F' + struct.pack('<I', self.sent_id) + self.video_header[5:]
            batch = bytearray()
            while self.video_packets and len(batch) + len(self.video_packets[0]) + len(header) <= REFRESH_BUDGET:
                batch.extend(self.video_packets.popleft())
            try:
                super().send(bytes(batch))
                super().send(header)
            except OSError:
                self.close()
                return
            self._stat_frames += 1
            cap = self.server.cfg['fps'] if self.fps_cap == 255 else self.fps_cap
            self.video_ready_at = clock() + (max(.04, 1 / cap) if cap else .04)
            if not self.video_packets:
                self.video_dirty.clear()
                self.video_jpeg_bytes = 0

    return VideoSession
