#include "SerialConsole.h"

void SerialConsole::begin() {
  buffer_.reserve(64);
  printHelp();
}

void SerialConsole::loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (buffer_.length()) {
        dispatch(buffer_);
        buffer_ = "";
      }
    } else {
      buffer_ += c;
    }
  }
}

void SerialConsole::dispatch(const String& line) {
  String s = line;
  s.trim();
  if (s.length() == 0) return;

  // Split into up to three tokens: <name> <action> <arg>.
  int sp1 = s.indexOf(' ');
  String name = (sp1 < 0) ? s : s.substring(0, sp1);
  String rest = (sp1 < 0) ? "" : s.substring(sp1 + 1);
  rest.trim();
  int sp2 = rest.indexOf(' ');
  String action = (sp2 < 0) ? rest : rest.substring(0, sp2);
  String arg = (sp2 < 0) ? "" : rest.substring(sp2 + 1);
  arg.trim();

  if (name == "list") { listComponents(); return; }
  if (name == "help") { printHelp(); return; }
  if (name == "get") {
    // "get <component>" -> report that component's status().
    Component* c = registry_.find(action);
    if (c == nullptr) {
      Serial.printf("? unknown component '%s'\n", action.c_str());
    } else {
      Serial.printf("%s: %s\n", c->name(), c->status().c_str());
    }
    return;
  }

  Component* c = registry_.find(name);
  if (c == nullptr) {
    Serial.printf("? unknown component '%s' (try 'list')\n", name.c_str());
    return;
  }
  if (c->handleCommand(action, arg)) {
    Serial.printf("ok %s %s%s%s\n", name.c_str(), action.c_str(),
                  arg.length() ? " " : "", arg.c_str());
  } else {
    Serial.printf("? %s can't '%s'\n", name.c_str(), action.c_str());
  }
}

void SerialConsole::listComponents() {
  Serial.println("components:");
  for (auto* c : registry_.all()) {
    Serial.printf("  %-10s %s\n", c->name(), c->status().c_str());
  }
}

void SerialConsole::printHelp() {
  Serial.println("commands: <name> <action> [arg]");
  Serial.println("  led:    on | off | toggle | brightness <0-255> | blink [ms] | solid");
  Serial.println("          pattern <sos|heartbeat|strobe> | seq <on,off,...ms> | pulse [ms]");
  Serial.println("  leds:   alternate [ms] | together [ms] | pattern <name> | answer <yes|no> | off");
  Serial.println("  rgb:    color <r,g,b> | blink [ms] | off");
  Serial.println("  servo:  angle <deg> | heading <0-360> | compass <N|NE|E|...>");
  Serial.println("  meta:   list | get <name> | help");
}
