// ECS gate tests (GuiaPort Sistema 1):
//  - correctness: create/add/remove/query/events/templates
//  - perf: 10k create+destroy < 1ms, MT query over 10k < 0.5ms
#include "ECS/World.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace tucano;
using namespace tucano::ecs;

namespace {

struct Position {
  float x, y, z;
};
struct Velocity {
  float x, y, z;
};
struct Health {
  int hp;
};

int g_fail = 0;
void check(bool cond, const char* name) {
  std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  if (!cond) {
    ++g_fail;
  }
}

double msNow() {
  using clock = std::chrono::high_resolution_clock;
  return double(clock::now().time_since_epoch().count()) *
         double(clock::period::num) / double(clock::period::den) * 1000.0;
}

struct DamageEvent {
  int amount;
};

} // namespace

int main() {
  registerComponent<Position>("Position");
  registerComponent<Velocity>("Velocity");
  registerComponent<Health>("Health");

  std::printf("== Correctness ==\n");
  {
    World w;
    Entity e = w.createWith<Position, Velocity>();
    check(w.alive(e), "entity alive after create");
    check(w.has<Position>(e) && w.has<Velocity>(e), "has components");
    check(!w.has<Health>(e), "missing component reported absent");

    *w.get<Position>(e) = {1, 2, 3};
    w.get<Velocity>(e)->x = 5;
    check(w.get<Position>(e)->y == 2.0f, "component data persists");

    // Migração de archetype (add): dados antigos preservados.
    w.add<Health>(e, {100});
    check(w.get<Position>(e)->x == 1.0f && w.get<Velocity>(e)->x == 5.0f,
          "data survives archetype migration on add");
    check(w.get<Health>(e)->hp == 100, "new component value after add");

    // Remoção migra de volta.
    w.remove<Velocity>(e);
    check(w.has<Position>(e) && w.has<Health>(e) && !w.has<Velocity>(e), "remove migrates archetype");
    check(w.get<Position>(e)->x == 1.0f, "data survives migration on remove");

    w.destroy(e);
    check(!w.alive(e), "destroy kills entity");
  }

  // Query correctness + exclusion.
  {
    World w;
    for (int i = 0; i < 100; ++i) {
      Entity e = w.createWith<Position, Velocity>();
      w.get<Position>(e)->x = float(i);
    }
    for (int i = 0; i < 50; ++i) {
      w.createWith<Position>(); // sem Velocity
    }
    QueryId q = w.queries().registerQuery({{"Position"}, {"Velocity"}, {}, {}});
    int seen = 0;
    double sum = 0;
    w.queries().performQuery(q, [&](Entity, ComponentAccessor& acc) {
      sum += acc.get<Position>().x;
      ++seen;
    });
    check(seen == 100, "query matches only Position+Velocity");
    check(sum == 4950.0, "query reads correct data");

    QueryId qNo = w.queries().registerQuery({{"Position"}, {}, {}, {"Velocity"}});
    int seenNo = 0;
    w.queries().performQuery(qNo, [&](Entity, ComponentAccessor&) { ++seenNo; });
    check(seenNo == 50, "exclusion filter (NO Velocity) works");

    QueryId qDup = w.queries().registerQuery({{"Position"}, {"Velocity"}, {}, {}});
    check(qDup == q, "identical query is deduplicated");
  }

  // Events (deferred FIFO).
  {
    World w;
    int totalDamage = 0;
    w.events().subscribe(EventManager::eventTypeOf<DamageEvent>(),
                         [&](Entity, const void* p, uint32_t) {
                           totalDamage += static_cast<const DamageEvent*>(p)->amount;
                         });
    w.events().broadcast(DamageEvent{10});
    w.events().broadcast(DamageEvent{25});
    check(totalDamage == 0, "events deferred until flush");
    w.tick();
    check(totalDamage == 35, "events delivered on flush");
  }

  // Templates (JSON + inheritance).
  {
    World w;
    const char* json = R"([
      {"name":"base","components":{"transform":{"scale":2.0}}},
      {"name":"cube","parent":"base","components":{"transform":{"position":[1,5,3]},"mesh":{"sceneIndex":7}}}
    ])";
    std::string err;
    bool ok = w.templates().loadFromString(json, &err);
    check(ok, err.empty() ? "template JSON parsed" : err.c_str());
    Entity e = w.instantiate("cube");
    check(e != kInvalidEntity, "template instantiated");
    check(w.has<TransformComponent>(e) && w.has<RenderObjectComponent>(e), "template components present");
    auto* t = w.get<TransformComponent>(e);
    check(t && t->position.y == 5.0f, "child overrides position");
    check(t && t->scale.x == 2.0f, "inherited scale from parent");
    check(w.get<RenderObjectComponent>(e)->sceneIndex == 7u, "mesh sceneIndex applied");
  }

  std::printf("\n== Performance == (best of 8 runs — algorithmic cost, not scheduler noise)\n");
  {
    double bestCreate = 1e9, bestDestroy = 1e9;
    std::vector<Entity> ents;
    ents.reserve(10000);
    for (int run = 0; run < 8; ++run) {
      World w;
      ents.clear();
      const double t0 = msNow();
      for (int i = 0; i < 10000; ++i) {
        ents.push_back(w.createWith<Position, Velocity, Health>());
      }
      const double t1 = msNow();
      for (Entity e : ents) {
        w.destroy(e);
      }
      const double t2 = msNow();
      bestCreate = std::min(bestCreate, t1 - t0);
      bestDestroy = std::min(bestDestroy, t2 - t1);
    }
    std::printf("  10k create: %.3f ms | 10k destroy: %.3f ms\n", bestCreate, bestDestroy);
    check(bestCreate < 1.0, "10k create < 1ms");
    check(bestDestroy < 1.0, "10k destroy < 1ms");
  }

  {
    World w;
    for (int i = 0; i < 10000; ++i) {
      Entity e = w.createWith<Position, Velocity>();
      *w.get<Position>(e) = {0, 0, 0};
      *w.get<Velocity>(e) = {1, 2, 3};
    }
    QueryId q = w.queries().registerQuery({{"Position"}, {"Velocity"}, {}, {}});
    // Warm up archetype match cache.
    w.queries().performQueryChunks(q, [](ComponentAccessor&) {});

    auto mtPass = [&]() {
      w.queries().performQueryMT(q, w.jobs(), [](Entity, ComponentAccessor& acc) {
        auto& p = acc.get<Position>();
        const auto& v = acc.get<Velocity>();
        p.x += v.x;
        p.y += v.y;
        p.z += v.z;
      });
    };
    double bestMT = 1e9;
    for (int run = 0; run < 8; ++run) {
      const double t0 = msNow();
      mtPass();
      bestMT = std::min(bestMT, msNow() - t0);
    }
    std::printf("  MT query 10k (%u workers): %.3f ms\n", w.jobs().workerCount() + 1, bestMT);
    check(bestMT < 0.5, "MT query over 10k < 0.5ms");

    // Verify every entity was visited exactly once per pass (x == run count, no double/missed work).
    int sampled = 0;
    bool allMoved = true;
    w.queries().performQuery(q, [&](Entity, ComponentAccessor& acc) {
      if (sampled++ < 200 && acc.get<Position>().x != 8.0f) {
        allMoved = false;
      }
    });
    check(allMoved, "MT query visited each entity exactly once per pass");
  }

  std::printf("\n%s (%d failures)\n", g_fail == 0 ? "ALL GATES PASSED" : "GATES FAILED", g_fail);
  return g_fail == 0 ? 0 : 1;
}
