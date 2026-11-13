#pragma once
#include <cstdint>
#include <utility>

// Device-level contract shared by boot, painting and touch. M5GFX owns the
// actual ILI9341 transport; this module owns when the transport may be used.
// It does not claim SPI completion is equivalent to LCD scanout/visibility.
enum class ExternalDisplayState : uint8_t { Uninitialized, Ready, Suspended, Fault };
enum class ExternalDisplayError : uint8_t {
  None, PanelInit, TouchInit, GeometryMismatch, NotReady, InvalidRegion
};

template<class Panel, class Touch>
class ExternalDisplayDevice {
public:
  using TouchPoint = decltype(std::declval<Touch&>().getPoint());

  ExternalDisplayDevice(Panel& panel, Touch& touch, int width, int height)
    : panel_(panel), touch_(touch), width_(width), height_(height) {}

  template<class ConfigurePanel, class InitializeTouch>
  bool initialize(ConfigurePanel configurePanel, InitializeTouch initializeTouch) {
    if (state_ != ExternalDisplayState::Uninitialized) return fail(ExternalDisplayError::NotReady);
    if (!panel_.init()) return fault(ExternalDisplayError::PanelInit);
    configurePanel(panel_);
    if (panel_.width() != width_ || panel_.height() != height_)
      return fault(ExternalDisplayError::GeometryMismatch);
    if (!initializeTouch(touch_)) return fault(ExternalDisplayError::TouchInit);
    state_ = ExternalDisplayState::Ready;
    error_ = ExternalDisplayError::None;
    return true;
  }

  ExternalDisplayState state() const { return state_; }
  ExternalDisplayError error() const { return error_; }
  int width() const { return width_; }
  int height() const { return height_; }

  // The caller owns a pixel buffer until the next completion fence.
  // Reject invalid windows *before* any SPI operations.
  template<class Pixel>
  bool present(int x, int y, int w, int h, const Pixel* pixels) {
    if (!ready()) return false;
    if (!pixels || x < 0 || y < 0 || w <= 0 || h <= 0 ||
        x >= width_ || y >= height_ || w > width_ - x || h > height_ - y)
      return fail(ExternalDisplayError::InvalidRegion);
    waitComplete();
    panel_.pushImageDMA(x, y, w, h, pixels);
    pending_ = true;
    error_ = ExternalDisplayError::None;
    return true;
  }

  bool waitComplete() {
    if (!ready()) return false;
    if (pending_) {
      panel_.waitDMA();
      pending_ = false;
    }
    return true;
  }

  // In particular, terminate any remaining SPI/DMA mode before Arduino
  // XPT2046 acquires the same hardware bus.
  template<class OnWaitComplete>
  bool finishTransfer(OnWaitComplete onWaitComplete) {
    if (!ready()) return false;
    // A NOP sent inside an outer startWrite() does not release TFT CS or
    // the bus lock. Never hand SPI to XPT2046 until that owner closes the batch.
    if (panel_.getStartCount() != 0) return fail(ExternalDisplayError::NotReady);
    panel_.waitDMA(); // also handles driver-side pending work not tracked here
    pending_ = false;
    onWaitComplete(); // after waitDMA, before SPI ownership housekeeping
    panel_.startWrite();
    panel_.writeCommand(uint8_t{0x00});
    panel_.endWrite();
    return true;
  }
  bool finishTransfer() { return finishTransfer([]{}); }

  bool beginBatch() {
    if (!ready()) return false;
    if (panel_.getStartCount() != 0) return fail(ExternalDisplayError::NotReady);
    panel_.startWrite();
    return true;
  }
  bool endBatch() {
    if (!ready()) return false;
    if (panel_.getStartCount() == 0) return fail(ExternalDisplayError::NotReady);
    if (!waitComplete()) return false;
    panel_.endWrite();
    return true;
  }

  // Optional, explicit diagnostic. A successful call means a register
  // observation was obtained, NOT that transport or panel identity is verified.
  // The panel backend owns indexed ILI9341 command details.
  template<class Observation>
  bool inspectIdentity(Observation& out) {
    if (!ready()) return false;
    // Register reads require an independent LCD session, not a nested batch.
    if (panel_.getStartCount() != 0) return fail(ExternalDisplayError::NotReady);
    if (!waitComplete()) return false;
    out = panel_.readIndexedId4();
    error_ = ExternalDisplayError::None;
    return true;
  }

  bool clear(uint16_t color) {
    if (!ready()) return false;
    waitComplete();
    panel_.fillScreen(color);
    return true;
  }
  bool setBrightness(uint8_t brightness) {
    if (!ready()) return false;
    brightness_ = brightness;
    panel_.setBrightness(brightness);
    return true;
  }

  // Command-level lifecycle, not proof that the physical LCD changed state.
  // Explicit delay callback makes the mandatory ILI9341 settle time testable.
  // Do not switch controller state while a caller owns an LCD write session.
  template<class Delay>
  bool suspend(Delay delayMs) {
    if (!ready()) return false;
    if (panel_.getStartCount() != 0) return fail(ExternalDisplayError::NotReady);
    if (!finishTransfer()) return false;
    panel_.setBrightness(0);
    panel_.setControllerSleep(true);
    delayMs(120);
    state_ = ExternalDisplayState::Suspended;
    error_ = ExternalDisplayError::None;
    return true;
  }

  template<class Delay>
  bool resume(Delay delayMs) {
    if (state_ != ExternalDisplayState::Suspended)
      return fail(ExternalDisplayError::NotReady);
    panel_.setControllerSleep(false);
    delayMs(120);
    panel_.setBrightness(brightness_);
    state_ = ExternalDisplayState::Ready;
    error_ = ExternalDisplayError::None;
    return true;
  }
  void armTouch() {
    if (state_ == ExternalDisplayState::Ready) touch_.isrWake = true;
  }
  bool readTouch(TouchPoint& point) {
    if (!finishTransfer()) return false;
    point = touch_.getPoint();
    return true;
  }

  void shutdown() {
    if (state_ == ExternalDisplayState::Ready) finishTransfer();
    state_ = ExternalDisplayState::Uninitialized;
    pending_ = false;
    error_ = ExternalDisplayError::None;
  }

private:
  bool ready() {
    if (state_ == ExternalDisplayState::Ready) return true;
    return fail(ExternalDisplayError::NotReady);
  }
  bool fail(ExternalDisplayError error) { error_ = error; return false; }
  bool fault(ExternalDisplayError error) {
    state_ = ExternalDisplayState::Fault;
    return fail(error);
  }
  Panel& panel_;
  Touch& touch_;
  int width_, height_;
  bool pending_ = false;
  uint8_t brightness_ = 127;
  ExternalDisplayState state_ = ExternalDisplayState::Uninitialized;
  ExternalDisplayError error_ = ExternalDisplayError::None;
};
