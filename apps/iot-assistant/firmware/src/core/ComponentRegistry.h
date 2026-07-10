#pragma once
#include <vector>
#include "core/Component.h"

// Holds every component on the board and fans lifecycle calls out to them.
// Lets main.cpp and the control surfaces work against the collection instead
// of naming concrete devices.
class ComponentRegistry {
 public:
  void add(Component* c) { components_.push_back(c); }

  void beginAll();
  void loopAll();

  // Look a component up by its name(), or nullptr if there's no match.
  Component* find(const String& name);

  const std::vector<Component*>& all() const { return components_; }

 private:
  std::vector<Component*> components_;
};
