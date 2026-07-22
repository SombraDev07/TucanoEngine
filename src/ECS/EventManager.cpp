#include "ECS/EventManager.h"

#include <algorithm>

namespace tucano::ecs {

void EventManager::subscribe(EventType type, EventHandler handler) {
  m_handlers[type].push_back(std::move(handler));
}

void EventManager::enqueue(EventType type, Entity target, const void* payload, uint32_t size) {
  EventHeader h;
  h.type = type;
  h.target = target;
  h.size = size;
  h.payloadOffset = uint32_t(m_payloads.size());
  m_payloads.resize(m_payloads.size() + size);
  if (size > 0) {
    std::memcpy(m_payloads.data() + h.payloadOffset, payload, size);
  }
  m_queue.push_back(h);
}

void EventManager::flush() {
  // Processa só o snapshot atual; eventos gerados dentro dos handlers ficam para o próximo flush.
  const uint32_t total = uint32_t(m_queue.size());
  const uint32_t budget = std::min(total, m_rateLimit);

  for (uint32_t i = 0; i < budget; ++i) {
    const EventHeader& h = m_queue[i];
    auto it = m_handlers.find(h.type);
    if (it != m_handlers.end()) {
      const void* payload = h.size ? m_payloads.data() + h.payloadOffset : nullptr;
      for (auto& handler : it->second) {
        handler(h.target, payload, h.size);
      }
    }
  }

  // Adapta o rate limit: se sobrou fila, aumenta o teto; caso contrário relaxa de volta.
  if (budget < total) {
    m_rateLimit = std::min(m_rateLimit * 2u, 4096u);
    m_queue.erase(m_queue.begin(), m_queue.begin() + budget);
    // Payloads consumidos permanecem até o esvaziamento completo (compactação abaixo).
  } else {
    m_rateLimit = std::max(16u, m_rateLimit / 2u);
    m_queue.clear();
    m_payloads.clear();
  }
}

} // namespace tucano::ecs
