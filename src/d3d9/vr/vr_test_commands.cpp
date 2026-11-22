#include "vr_test_commands.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "vr_math.h"

namespace dxvk {

  namespace {

    constexpr float DegToRad = 3.14159265358979f / 180.0f;

    const char* const ActionNames[VrActionCount] = {
      "attack", "twohandgrip", "activate", "jump", "reload", "sneak", "pausehold", "vats", "pause",
      "grab", "wait", "magazine", "menuselect", "menuback", "menualternate", "menuoption",
      "menuprevious", "menunext", "menulefttrigger", "menurighttrigger",
    };

    const char* const PanelNames[VrPanelCount] = {
      "menu", "world0", "world1", "world2", "world3", "world4", "world5", "world6", "world7", "world8",
      "compass", "hudhead", "hudwrist", "hudprompt", "hudcharge", "hudammo", "quickwheel", "terminal",
      "caravanvalues", "pipboyhint", "pipboylampbar",
    };

    std::string lower(std::string_view s) {
      std::string out(s);

      for (char& c : out)
        c = char(std::tolower(static_cast<unsigned char>(c)));

      return out;
    }

    std::vector<std::string_view> splitWords(std::string_view line) {
      std::vector<std::string_view> words;
      size_t i = 0;

      while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r'))
          i++;

        size_t start = i;

        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r')
          i++;

        if (i > start)
          words.push_back(line.substr(start, i - start));
      }

      return words;
    }

    /// The rest of the line after its first \c count words, trimmed (for paths with spaces)
    std::string_view restAfter(std::string_view line, size_t count) {
      size_t i = 0;

      for (size_t w = 0; w < count; w++) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
          i++;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t')
          i++;
      }

      std::string_view rest = line.substr(i);

      while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
        rest.remove_prefix(1);
      while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t' || rest.back() == '\r'))
        rest.remove_suffix(1);

      return rest;
    }

    bool parseFloat(std::string_view word, float& out) {
      std::string text(word);
      char* end = nullptr;
      float value = std::strtof(text.c_str(), &end);

      if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(value))
        return false;

      out = value;
      return true;
    }

    bool parseCount(std::string_view word, uint64_t& out) {
      if (word.empty() || word.size() > 18)
        return false;

      uint64_t value = 0;

      for (char c : word) {
        if (c < '0' || c > '9')
          return false;

        value = value * 10 + uint64_t(c - '0');
      }

      out = value;
      return true;
    }

    bool parseHand(std::string_view word, VrHand& hand) {
      std::string name = lower(word);

      if (name == "left")  { hand = VrHand::Left;  return true; }
      if (name == "right") { hand = VrHand::Right; return true; }
      return false;
    }

    /// Parses "<x> <y> <z> <yaw> <pitch> [roll]" starting at words[first]
    bool parsePose(const std::vector<std::string_view>& words, size_t first, VrPose& pose) {
      size_t n = words.size() - first;

      if (n != 5 && n != 6)
        return false;

      float v[6] = { };

      for (size_t i = 0; i < n; i++) {
        if (!parseFloat(words[first + i], v[i]))
          return false;
      }

      pose = vrTestPose({ v[0], v[1], v[2] }, v[3], v[4], v[5]);
      return true;
    }

    VrTestCommand invalid(std::string why) {
      VrTestCommand command;
      command.error = std::move(why);
      return command;
    }

  }


  VrPose vrTestPose(const VrVector3& position, float yawDeg, float pitchDeg, float rollDeg) {
    VrQuaternion yaw   = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, yawDeg   * DegToRad);
    VrQuaternion pitch = vrQuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, pitchDeg * DegToRad);
    VrQuaternion roll  = vrQuaternionFromAxisAngle({ 0.0f, 0.0f, 1.0f }, rollDeg  * DegToRad);

    VrPose pose;
    pose.orientation = vrNormalize(yaw * pitch * roll);
    pose.position    = position;
    return pose;
  }


  const char* vrTestActionName(VrAction action) {
    uint32_t index = uint32_t(action);
    return index < VrActionCount ? ActionNames[index] : "?";
  }


  const char* vrTestPanelName(VrPanelId panel) {
    uint32_t index = uint32_t(panel);
    return index < VrPanelCount ? PanelNames[index] : "?";
  }


  bool vrTestFindAction(std::string_view name, VrAction& action) {
    std::string key = lower(name);

    for (uint32_t i = 0; i < VrActionCount; i++) {
      if (key == ActionNames[i]) {
        action = VrAction(i);
        return true;
      }
    }

    return false;
  }


  bool vrTestFindPanel(std::string_view name, VrPanelId& panel) {
    std::string key = lower(name);

    for (uint32_t i = 0; i < VrPanelCount; i++) {
      if (key == PanelNames[i]) {
        panel = VrPanelId(i);
        return true;
      }
    }

    return false;
  }


  VrTestCommand vrParseTestCommand(std::string_view line) {
    if (line.size() > VrTestMaxLine)
      return invalid("line too long");

    for (char c : line) {
      if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\r')
        return invalid("control characters");
    }

    std::vector<std::string_view> words = splitWords(line);

    if (words.empty())
      return invalid("empty line");

    std::string verb = lower(words[0]);
    size_t n = words.size();
    VrTestCommand command;

    if (verb == "ping" && n == 1) {
      command.verb = VrTestVerb::Ping;
    } else if (verb == "state" && n == 1) {
      command.verb = VrTestVerb::State;
    } else if (verb == "memory" && n == 1) {
      command.verb = VrTestVerb::Memory;
    } else if (verb == "head") {
      if (!parsePose(words, 1, command.pose))
        return invalid("head <x> <y> <z> <yaw> <pitch> [roll]");
      command.verb = VrTestVerb::Head;
    } else if (verb == "hand") {
      if (n < 2 || !parseHand(words[1], command.hand) || !parsePose(words, 2, command.pose))
        return invalid("hand left|right <x> <y> <z> <yaw> <pitch> [roll]");
      command.verb = VrTestVerb::Hand;
    } else if (verb == "release") {
      std::string what = n == 2 ? lower(words[1]) : std::string();
      static const struct { const char* name; VrTestRelease value; } names[] = {
        { "head", VrTestRelease::Head }, { "left", VrTestRelease::Left }, { "right", VrTestRelease::Right },
        { "move", VrTestRelease::Move }, { "turn", VrTestRelease::Turn }, { "all",   VrTestRelease::All   },
      };
      bool found = false;

      for (const auto& entry : names) {
        if (what == entry.name) {
          command.release = entry.value;
          found = true;
        }
      }

      if (!found)
        return invalid("release head|left|right|move|turn|all");
      command.verb = VrTestVerb::Release;
    } else if (verb == "press" || verb == "hold" || verb == "up") {
      if (n != 2 || !vrTestFindAction(words[1], command.action))
        return invalid(verb + " <action> (attack, activate, menuselect, menuback, ...)");
      command.verb = verb == "press" ? VrTestVerb::Press : verb == "hold" ? VrTestVerb::Hold : VrTestVerb::Up;
    } else if (verb == "stick") {
      std::string which = n == 4 ? lower(words[1]) : std::string();

      if ((which != "move" && which != "turn")
       || !parseFloat(words[2], command.value.x) || !parseFloat(words[3], command.value.y)
       || std::fabs(command.value.x) > 1.0f || std::fabs(command.value.y) > 1.0f)
        return invalid("stick move|turn <x> <y> (each in -1..1)");
      command.stick = which == "move" ? VrTestStick::Move : VrTestStick::Turn;
      command.verb  = VrTestVerb::Stick;
    } else if (verb == "aim") {
      if (n != 5 || !parseHand(words[1], command.hand) || !vrTestFindPanel(words[2], command.panel)
       || !parseFloat(words[3], command.u) || !parseFloat(words[4], command.v))
        return invalid("aim left|right <panel> <u> <v>");
      command.verb = VrTestVerb::Aim;
    } else if (verb == "wait" || verb == "waitframe") {
      if (n != 2 || !parseCount(words[1], command.count))
        return invalid(verb + (verb == "wait" ? " <frames>" : " <frame number>"));
      command.verb = verb == "wait" ? VrTestVerb::Wait : VrTestVerb::WaitFrame;
    } else if (verb == "capture") {
      std::string what = n >= 2 ? lower(words[1]) : std::string();

      if (what == "eyes" && n >= 3) {
        command.verb = VrTestVerb::CaptureEyes;
        command.path = std::string(restAfter(line, 2));
      } else if (what == "panel" && n >= 4 && vrTestFindPanel(words[2], command.panel)) {
        command.verb = VrTestVerb::CapturePanel;
        command.path = std::string(restAfter(line, 3));
      } else {
        return invalid("capture eyes <file prefix> | capture panel <panel> <file>");
      }
    } else if (verb == "pipboy") {
      std::string what = n == 2 ? lower(words[1]) : std::string();

      if (n == 1)
        command.verb = VrTestVerb::PipBoy;
      else if (what == "open")
        command.verb = VrTestVerb::PipBoyOpen;
      else if (what == "close")
        command.verb = VrTestVerb::PipBoyClose;
      else
        return invalid("pipboy [open|close]");
    } else if (verb == "signal") {
      std::string what = n == 2 ? lower(words[1]) : std::string();

      if (what == "on")
        command.verb = VrTestVerb::SignalOn;
      else if (what == "off")
        command.verb = VrTestVerb::SignalOff;
      else
        return invalid("signal on|off");
    } else {
      return invalid("unknown request '" + std::string(words[0]) + "'");
    }

    return command;
  }

}
