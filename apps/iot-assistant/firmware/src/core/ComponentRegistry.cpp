#include "core/ComponentRegistry.h"

void ComponentRegistry::beginAll() {
  for (auto* c : components_) c->begin();
}

void ComponentRegistry::loopAll() {
  for (auto* c : components_) c->loop();
}

Component* ComponentRegistry::find(const String& name) {
  for (auto* c : components_) {
    if (name.equals(c->name())) return c;
  }
  return nullptr;
}
