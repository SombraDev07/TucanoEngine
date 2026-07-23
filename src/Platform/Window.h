#pragma once

#include <cstdint>
#include <functional>
#include <string>

struct GLFWwindow;

namespace tucano {

struct WindowDesc {
  uint32_t width = 1280;
  uint32_t height = 720;
  std::string title = "Tucano Engine";
  bool resizable = true;
  bool decorated = true;
};

class Window {
public:
  using ResizeCallback = std::function<void(uint32_t, uint32_t)>;

  explicit Window(const WindowDesc& desc = {});
  ~Window();

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  bool shouldClose() const;
  void pollEvents();
  void setTitle(const std::string& title);

  GLFWwindow* handle() const { return m_window; }
  void* nativeHandle() const;
  uint32_t width() const { return m_width; }
  uint32_t height() const { return m_height; }
  float aspect() const;

  void setResizeCallback(ResizeCallback cb) { m_resizeCb = std::move(cb); }

private:
  GLFWwindow* m_window = nullptr;
  uint32_t m_width = 0;
  uint32_t m_height = 0;
  ResizeCallback m_resizeCb;
};

} // namespace tucano
