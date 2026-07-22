#pragma once

#include "ECS/ComponentTypes.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <vector>

namespace tucano::ecs {

// Eventos deferred: enfileirados durante o tick, entregues em flush() (FIFO), com rate limit
// adaptativo (começa em 16/tick, cresce sob carga) para evitar loops de eventos descontrolados.
using EventType = uint32_t;

struct EventHeader {
  EventType type;
  Entity target;    // kInvalidEntity = broadcast
  uint32_t size;    // bytes do payload
  uint32_t payloadOffset;
};

using EventHandler = std::function<void(Entity target, const void* payload, uint32_t size)>;

class EventManager {
public:
  void subscribe(EventType type, EventHandler handler);

  template <typename T>
  void send(Entity target, const T& evt) {
    static_assert(std::is_trivially_copyable_v<T>, "event payload must be trivially copyable");
    enqueue(eventTypeOf<T>(), target, &evt, sizeof(T));
  }
  template <typename T>
  void broadcast(const T& evt) {
    send<T>(kInvalidEntity, evt);
  }

  // Entrega os eventos pendentes; eventos gerados durante a entrega ficam para o próximo flush.
  void flush();
  uint32_t pending() const { return uint32_t(m_queue.size()); }

  template <typename T>
  static EventType eventTypeOf() {
    static const EventType id = s_nextType++;
    return id;
  }

private:
  void enqueue(EventType type, Entity target, const void* payload, uint32_t size);

  std::vector<EventHeader> m_queue;
  std::vector<uint8_t> m_payloads;
  std::unordered_map<EventType, std::vector<EventHandler>> m_handlers;
  uint32_t m_rateLimit = 16;
  static inline EventType s_nextType = 0;
};

} // namespace tucano::ecs
