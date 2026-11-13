"""Absolute touch extension for the pinned CXL Session implementation."""
from collections import OrderedDict
import struct
import threading

# Protect per-server lease creation and ownership across session factories.
_lease_lock = threading.Lock()


def absolute_session(base):
    # base is the imported pinned server module, not its Session class.
    class AbsoluteSession(base.Session):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            self._touch_frames = OrderedDict()
            self._touch_pending = None
            self._pan_frames = OrderedDict()
            self._pan_pending = None
            self._manual_view = None

        def _render_manual(self, sct, monitors):
            mon = monitors[self.mon_idx % len(monitors)]
            if mon != self.cur_mon:
                self._manual_view = None
                return super().render(sct, monitors)
            rx, ry = self.cur_ratio
            L, T, W, H = (mon[k] for k in ('left', 'top', 'width', 'height'))
            vw, vh = round(self.ow * rx), round(self.oh * ry)
            vx, vy = self._manual_view
            vx = L+(W-vw)//2 if vw >= W else min(max(vx, L), L+W-vw)
            vy = T+(H-vh)//2 if vh >= H else min(max(vy, T), T+H-vh)
            self.view = self._manual_view = (vx, vy)
            ix0, iy0 = max(vx, L), max(vy, T)
            ix1, iy1 = min(vx+vw, L+W), min(vy+vh, T+H)
            canvas = base.Image.new('RGB', (self.ow, self.oh))
            if ix1 > ix0 and iy1 > iy0:
                shot = sct.grab(dict(left=ix0, top=iy0, width=ix1-ix0, height=iy1-iy0))
                key = (ix0, iy0, ix1, iy1, rx, ry)
                if key != self.cache_key or shot.bgra != self.cache_raw:
                    img = base.Image.frombytes('RGB', shot.size, shot.bgra, 'raw', 'BGRX')
                    size = (max(1, round((ix1-ix0)/rx)), max(1, round((iy1-iy0)/ry)))
                    if img.size != size:
                        img = img.resize(size, base.LANCZOS, reducing_gap=2.0)
                    self.cache_key, self.cache_raw, self.cache_img = key, shot.bgra, img
                canvas.paste(self.cache_img, (round((ix0-vx)/rx), round((iy0-vy)/ry)))
            if self.server.cfg['cursor']:
                cx, cy = self.inp.cursor_pos()
                px, py = (cx-vx)/rx, (cy-vy)/ry
                if 0 <= px < self.ow and 0 <= py < self.oh:
                    base.draw_cursor(canvas, round(px), round(py))
                elif self.zoom_idx:
                    # A stable server-side hint; the existing dirty-tile path
                    # transmits it only when the composed image changes.
                    x, y = min(max(round(px),4),self.ow-5), min(max(round(py),4),self.oh-5)
                    base.ImageDraw.Draw(canvas).ellipse((x-3,y-3,x+3,y+3),
                                                       fill=(255,220,0),outline=(0,0,0))
            fit = max(W/self.ow, H/self.oh)
            count = 1+sum(r < fit-1e-6 for r in base.ZOOM_RATIOS)
            return canvas, max(rx, ry), self.zoom_idx, count, False

        def render(self, sct, monitors):
            # The upstream Condition has an RLock; retain it through capture so
            # the transform is derived from the same geometry as the image.
            with self.lock:
                result = (self._render_manual(sct, monitors) if self._manual_view is not None
                          else super().render(sct, monitors))
                _, _, zi, _, stretch = result
                if zi and self._manual_view is None:
                    self._manual_view = self.view
                mon = dict(self.cur_mon)
                L, T, W, H = (mon[k] for k in ('left', 'top', 'width', 'height'))
                rx, ry = self.cur_ratio
                self._pan_pending = (zi, self.mon_idx, dict(mon), rx, ry, self.view, self.ow, self.oh)
                if stretch:
                    vx, vy, vw, vh = L, T, W, H
                else:
                    vw, vh = round(self.ow * rx), round(self.oh * ry)
                    vx, vy = ((L + (W - vw) // 2, T + (H - vh) // 2)
                              if zi == 0 else self.view)
                ix0, iy0 = max(vx, L), max(vy, T)
                ix1, iy1 = min(vx + vw, L + W), min(vy + vh, T + H)
                if ix1 <= ix0 or iy1 <= iy0:
                    self._touch_pending = None
                else:
                    dx, dy = round((ix0 - vx) / rx), round((iy0 - vy) / ry)
                    dw, dh = max(1, round((ix1 - ix0) / rx)), max(1, round((iy1 - iy0) / ry))
                    self._touch_pending = (self.ow, self.oh, dx, dy, dw, dh, ix0, iy0, ix1, iy1)
                return result

        def send(self, data):
            if len(data) == 12 and data[:1] == b'F':
                with self.lock:
                    if self._touch_pending is not None:
                        frame_id = struct.unpack_from('<I', data, 1)[0]
                        self._touch_frames[frame_id] = self._touch_pending
                        self._pan_frames[frame_id] = self._pan_pending
                        self._touch_frames.move_to_end(frame_id)
                        while len(self._touch_frames) > 128:
                            expired, _ = self._touch_frames.popitem(last=False)
                            self._pan_frames.pop(expired, None)
                        data = data[:-1] + bytes((data[-1] | 8 | 16,))
                return super().send(data)
            return super().send(data)

        def _claim_control(self):
            with _lease_lock:
                owner = getattr(self.server, '_csos_controller', None)
                if owner is None:
                    self.server._csos_controller = self
                    return True
                return owner is self

        def close(self, reason=None):
            # Serialize cleanup with input; keep the lease until cleanup ends.
            with self.lock:
                self.release_all()
                super().close(reason)
                with _lease_lock:
                    if getattr(self.server, '_csos_controller', None) is self:
                        self.server._csos_controller = None

        def handle(self, t, p):
            rejected = False
            with self.lock:
                if not self.alive:
                    return
                if t == ord('V'):
                    if len(p) != 8:
                        return
                    fid, dx, dy = struct.unpack('<Ihh', p)
                    frame = self._pan_frames.get(fid)
                    if frame is None or not self.zoom_idx:
                        return
                    zi, mi, mon, rx, ry, origin, ow, oh = frame
                    if (zi != self.zoom_idx or mi != self.mon_idx or mon != self.cur_mon
                            or (rx, ry) != self.cur_ratio or origin is None or self.view is None
                            or (ow, oh) != (self.ow, self.oh)
                            or abs(dx) > 32 or abs(dy) > 32):
                        return
                    if self.buttons_down:
                        return  # EASD must not implicitly move a dragged object.
                    vx, vy = self._manual_view if self._manual_view is not None else self.view
                    previous = vx, vy
                    L, T, W, H = (mon[k] for k in ('left', 'top', 'width', 'height'))
                    vw, vh = round(self.ow*rx), round(self.oh*ry)
                    vx += round(dx*rx)
                    vy += round(dy*ry)
                    vx = L+(W-vw)//2 if vw >= W else min(max(vx, L), L+W-vw)
                    vy = T+(H-vh)//2 if vh >= H else min(max(vy, T), T+H-vh)
                    if previous == (vx, vy):
                        return
                    # Pan changes only the view: do not move the X11 cursor or
                    # disturb hover menus, and do not claim the input lease.
                    self._manual_view = self.view = (vx, vy)
                    self.force_full = True
                    self.wake.set()
                    return
                if t in (ord('Z'), ord('N')):
                    self._manual_view = None
                if t != ord('D'):
                    if t in (ord('K'), ord('M'), ord('B'), ord('W')):
                        if self.server.view_only:
                            return
                        if not self._claim_control():
                            rejected = True
                        else:
                            return super().handle(t, p)
                    else:
                        return super().handle(t, p)
                elif len(p) == 9 and not self.server.view_only:
                    frame_id, x, y, button = struct.unpack('<IHHB', p)
                    if button not in (0, 1):
                        return
                    geometry = self._touch_frames.get(frame_id)
                    if geometry is None:
                        self.force_full = True
                        self.wake.set()
                        return
                    ow, oh, dx, dy, dw, dh, ix0, iy0, ix1, iy1 = geometry
                    if not (0 <= x < ow and 0 <= y < oh and dx <= x < dx + dw and dy <= y < dy + dh):
                        return
                    if not self._claim_control():
                        rejected = True
                    else:
                        px = ix0 + (x - dx) * (ix1 - ix0) // dw
                        py = iy0 + (y - dy) * (iy1 - iy0) // dh
                        self.inp.set_cursor(px, py)
                        self.mouse_f = None
                        self.buttons_down.add(button)
                        try:
                            self.inp.button(button, True)
                        finally:
                            self.inp.button(button, False)
                            self.buttons_down.discard(button)
                        self.wake.set()
            if rejected:
                self.close(reason=4)

        def move_mouse(self, dvx, dvy):
            # Base input already enforces the monitor bounds and subpixel speed.
            # Keep the chosen view until continued motion actually crosses its edge.
            previous_cursor = self.inp.cursor_pos()
            super().move_mouse(dvx, dvy)
            if not self.zoom_idx or self.cur_mon is None or self.view is None:
                return
            vx, vy = self._manual_view if self._manual_view is not None else self.view
            rx, ry = self.cur_ratio
            vw, vh = round(self.ow*rx), round(self.oh*ry)
            if not (vx <= previous_cursor[0] < vx+vw and vy <= previous_cursor[1] < vy+vh):
                return  # Offscreen cursor must re-enter both axes before following.
            cx, cy = self.inp.cursor_pos()
            if dvx < 0 and cx < vx: vx = cx
            elif dvx > 0 and cx >= vx+vw: vx = cx-vw+1
            if dvy < 0 and cy < vy: vy = cy
            elif dvy > 0 and cy >= vy+vh: vy = cy-vh+1
            mon = self.cur_mon
            L, T, W, H = (mon[k] for k in ('left','top','width','height'))
            vx = L+(W-vw)//2 if vw >= W else min(max(vx,L),L+W-vw)
            vy = T+(H-vh)//2 if vh >= H else min(max(vy,T),T+H-vh)
            self._manual_view = self.view = (vx,vy)

    return AbsoluteSession
