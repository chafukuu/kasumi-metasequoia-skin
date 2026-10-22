#include "window/candidate_typed_stars.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using CandidateTypedStars::Color;
using CandidateTypedStars::Sample;
using CandidateTypedStars::Settings;
using CandidateTypedStars::System;

void Require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void Near(double actual, double expected, double tolerance,
          const char *message) {
  Require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
          message);
}

void Same(const std::vector<Sample> &a, const std::vector<Sample> &b,
          bool compareColor = true) {
  Require(a.size() == b.size(), "Sample size differs");
  for (std::size_t i = 0; i < a.size(); ++i) {
    Near(a[i].x, b[i].x, 0.0, "Seed/frame cadence changed x");
    Near(a[i].y, b[i].y, 0.0, "Seed/frame cadence changed y");
    Near(a[i].width, b[i].width, 0.0, "Seed/frame cadence changed width");
    Near(a[i].height, b[i].height, 0.0, "Seed/frame cadence changed height");
    Near(a[i].rotationDeg, b[i].rotationDeg, 0.0,
         "Seed/frame cadence changed rotation");
    Near(a[i].opacity, b[i].opacity, 0.0, "Seed/frame cadence changed opacity");
    if (compareColor)
      Require(a[i].color == b[i].color, "Seed/frame cadence changed palette");
  }
}

void Bounds(const Sample &sample, const Settings &settings) {
  constexpr double radians = 3.14159265358979323846 / 180.0;
  const double c = std::abs(std::cos(sample.rotationDeg * radians));
  const double s = std::abs(std::sin(sample.rotationDeg * radians));
  const double halfW = (c * sample.width + s * sample.height) * 0.5;
  const double halfH = (s * sample.width + c * sample.height) * 0.5;
  Require(sample.x - halfW >= settings.minX &&
              sample.x + halfW <= settings.maxX &&
              sample.y - halfH >= settings.minY &&
              sample.y + halfH <= settings.maxY,
          "Rotated rectangle escaped configured DIP bounds");
  Require(std::isfinite(sample.rotationDeg) && sample.opacity >= 0.0 &&
              sample.opacity <= 1.0,
          "Invalid opacity/rotation");
  Require(sample.width >= settings.widthMin &&
              sample.width <= settings.widthMax,
          "Width outside configured range");
  const double aspect = sample.color == Color::Blue ? settings.blueAspectRatio
                        : sample.color == Color::Pink
                            ? settings.pinkAspectRatio
                            : settings.yellowAspectRatio;
  Near(sample.width / sample.height, aspect, 1e-12,
       "Original star aspect ratio changed");
}

double Lifetime(const System &system, bool pink,
                const Settings &settings = Settings{}) {
  double low = settings.durationMinMs;
  double high = settings.durationMaxMs;
  for (int iteration = 0; iteration < 48; ++iteration) {
    const double mid = (low + high) * 0.5;
    const auto samples = system.Sample(mid);
    const bool alive = std::any_of(
        samples.begin(), samples.end(), [pink](const Sample &sample) {
          return (sample.color == Color::Pink) == pink;
        });
    if (alive)
      low = mid;
    else
      high = mid;
  }
  return high;
}

void TestPairAndReproducibility() {
  System a(123456), b(123456), other(654321);
  a.SpawnPair(0.0);
  b.SpawnPair(0.0);
  other.SpawnPair(0.0);
  const auto first = a.Sample(250.0);
  Require(first.size() == 2 && first[0].color != first[1].color &&
              first[0].color != Color::Blue && first[1].color != Color::Blue,
          "Event did not create one pink and one yellow star");
  Same(first, b.Sample(250.0));
  Require(first[0].x != other.Sample(250.0)[0].x,
          "Different seeds did not vary flight");
  const auto p0 = a.Sample(0.0);
  for (const auto &sample : p0) {
    Near(sample.x, 66.52, 0.0, "Birth did not originate at guitar body");
    Near(sample.y, 22.57, 0.0, "Birth did not originate at guitar body");
    Near(sample.opacity, 0.0, 0.0, "Birth is not transparent");
  }
  Require(a.Sample(-1.0).empty(), "Star appeared before its event");
  Require(a.Sample(1450.0).empty() && !a.HasLive(1450.0) &&
              !a.NextFrameMs(1450.0),
          "Expired stars retained a frame deadline");
}

void TestAnalyticFlight() {
  System sparse(42), dense(42);
  sparse.SpawnPair(100.0);
  dense.SpawnPair(100.0);
  for (int t = 100; t <= 600; t += 5)
    (void)dense.Sample(t);
  Same(sparse.Sample(600.0), dense.Sample(600.0));
  const auto a = sparse.Sample(200.0);
  const auto b = sparse.Sample(300.0);
  const auto c = sparse.Sample(400.0);
  const auto d = sparse.Sample(500.0);
  for (std::size_t i = 0; i < 2; ++i) {
    Near(c[i].x - b[i].x, b[i].x - a[i].x, 1e-12,
         "Horizontal velocity is not constant");
    Near(d[i].y - 2.0 * c[i].y + b[i].y, c[i].y - 2.0 * b[i].y + a[i].y, 1e-12,
         "Vertical acceleration is not constant");
    Require(c[i].y - 2.0 * b[i].y + a[i].y > 0.0,
            "Flight does not curve downward");
  }
}

void TestLifetimeAndFade() {
  for (std::uint32_t seed = 0; seed < 64; ++seed) {
    System system(seed);
    system.SpawnPair(0.0);
    for (double t = 1; t <= 89; ++t) {
      const auto now = system.Sample(t);
      const auto before = system.Sample(t - 1);
      Require(now[0].opacity > before[0].opacity &&
                  now[1].opacity > before[1].opacity,
              "Fade-in is not smooth and monotonic");
    }
    const auto plateau = system.Sample(300.0);
    Near(plateau[0].opacity, 1.0, 0.0, "Fade-in never reaches full opacity");
    Near(plateau[1].opacity, 1.0, 0.0, "Fade-in never reaches full opacity");
    const auto at1200 = system.Sample(1199.0);
    Require(at1200.size() == 2, "Star expired before minimum lifetime");
    Require(system.Sample(1450.0).empty(), "Star exceeded maximum lifetime");
    // Track either palette through expiry without exposing private random
    // parameters.
    for (bool pink : {false, true}) {
      double priorOpacity = 1.0;
      double lastOpacity = 1.0;
      double lastTime = 0.0;
      for (double t = 800.0; t < 1450.0; t += 0.25) {
        const auto samples = system.Sample(t);
        const auto found = std::find_if(
            samples.begin(), samples.end(), [pink](const Sample &s) {
              return (s.color == Color::Pink) == pink;
            });
        if (found == samples.end())
          break;
        Require(found->opacity <= priorOpacity + 1e-12,
                "Fade-out increased opacity");
        lastTime = t;
        lastOpacity = found->opacity;
        priorOpacity = found->opacity;
      }
      Require(lastTime >= 1199.75 && lastTime < 1450.0 && lastOpacity < 0.00001,
              "End of flight is not smoothly transparent");
    }
  }
}

void TestRandomnessAndBounds() {
  const Settings settings;
  System system(777);
  std::array<double, 2> priorX{};
  bool varied = false;
  bool sawPinkFirst = false;
  bool sawYellowFirst = false;
  for (int pair = 0; pair < 256; ++pair) {
    system.Reset();
    system.SpawnPair(0.0);
    const auto at100 = system.Sample(100.0);
    const auto at200 = system.Sample(200.0);
    sawPinkFirst = sawPinkFirst || at100[0].color == Color::Pink;
    sawYellowFirst = sawYellowFirst || at100[0].color == Color::Yellow;
    varied = varied || (pair > 0 && priorX[0] != at100[0].x);
    priorX = {at100[0].x, at100[1].x};
    for (double t = 0.0; t < settings.durationMaxMs; t += 2.0)
      for (const auto &sample : system.Sample(t)) {
        Bounds(sample, settings);
      }
    std::array<double, 2> endX{};
    for (std::size_t i = 0; i < 2; ++i) {
      const double duration = Lifetime(system, at100[i].color == Color::Pink);
      endX[i] = settings.originX + (at200[i].x - at100[i].x) / 100.0 * duration;
      Require(endX[i] >= settings.endXMin && endX[i] <= settings.endXMax,
              "Horizontal endpoint outside declared random range");
      const double accelerationCoefficient =
          (at200[i].y - 2.0 * at100[i].y + settings.originY) / 20000.0;
      const double velocityCoefficient =
          (at100[i].y - settings.originY - accelerationCoefficient * 10000.0) /
          100.0;
      const double apexTime =
          -velocityCoefficient / (2.0 * accelerationCoefficient);
      const double rise = velocityCoefficient * velocityCoefficient /
                          (4.0 * accelerationCoefficient);
      Require(rise >= settings.riseMin - 1e-9 &&
                  rise <= settings.riseMax + 1e-9,
              "Arc height outside configured range");
      const double endY = settings.originY + velocityCoefficient * duration +
                          accelerationCoefficient * duration * duration;
      Require(endY >= settings.endYMin - 1e-9 &&
                  endY <= settings.endYMax + 1e-9,
              "Final drop outside configured random endpoint range");
      const double drop = endY - settings.originY;
      Near(apexTime / duration,
           std::sqrt(rise) / (std::sqrt(rise) + std::sqrt(rise + drop)), 1e-12,
           "Rise and drop do not determine the exact parabola");
      Require(apexTime > 0.0 && apexTime < 0.5 * duration,
              "Downward endpoint did not follow the apex");
    }
    Require(std::abs(endX[1] - endX[0]) >= settings.pairSeparation - 1e-9,
            "Two trajectories failed to separate");
  }
  Require(varied && sawPinkFirst && sawYellowFirst,
          "Repeated events did not vary trajectory/palette order");
}

void TestCapacityAndReset() {
  System system(10);
  for (int pair = 0; pair < 100; ++pair) {
    system.SpawnPair(0.0);
    Require(system.Sample(0.0).size() ==
                (std::min)(std::size_t(2 * pair + 2), System::kCapacity),
            "Capacity did not retain whole two-star events");
  }
  const auto next = system.NextFrameMs(0.0);
  Require(next.has_value(), "Live particles did not request a frame");
  Near(*next, System::kFrameMs, 1e-12, "Frame deadline differs from 60 fps");
  system.Reset();
  Require(system.Sample(0.0).empty() && !system.HasLive(0.0) &&
              !system.NextFrameMs(0.0),
          "Reset retained particles or deadlines");
  system.SpawnPair(5000.0);
  Require(system.Sample(5000.0).size() == 2, "Reset broke later events");
  system.Configure(Settings{});
  Require(system.Sample(5000.0).empty(),
          "Configuration change retained old effects");
  Settings invalid;
  invalid.minX = std::numeric_limits<double>::quiet_NaN();
  system.Configure(invalid);
  system.SpawnPair(0.0);
  Require(system.Sample(0.0).empty(), "Nonfinite settings emitted stars");
  system.Configure(Settings{});
  system.SpawnPair(std::numeric_limits<double>::infinity());
  Require(system.Sample(0.0).empty(), "Nonfinite event emitted stars");
}

void TestCustomSettingsAndImageRatios() {
  Settings settings;
  settings.widthMin = settings.widthMax = 21.5;
  settings.riseMin = settings.riseMax = 31.0;
  settings.endXMin = 40.0;
  settings.endXMax = 82.0;
  settings.endYMin = settings.endYMax = 103.0;
  settings.durationMinMs = settings.durationMaxMs = 1400.0;
  settings.fadeInMinMs = settings.fadeInMaxMs = 100.0;
  settings.fadeOutMinMs = settings.fadeOutMaxMs = 300.0;
  settings.rotationMinDeg = settings.rotationMaxDeg = -10.0;
  settings.spinMinDeg = settings.spinMaxDeg = 60.0;
  settings.pairSeparation = 30.0;
  settings.yellowAspectRatio = 2.0;
  settings.pinkAspectRatio = 0.625;
  Require(System::IsValidSettings(settings),
          "Valid custom settings or distinct PNG aspect ratios rejected");
  System system(492);
  system.Configure(settings);
  system.SpawnPair(0.0);
  for (double t = 0.0; t < settings.durationMaxMs; t += 2.0)
    for (const auto &sample : system.Sample(t))
      Bounds(sample, settings);
  const auto halfIn = system.Sample(50.0);
  const auto halfOut = system.Sample(1250.0);
  const auto a = system.Sample(100.0), b = system.Sample(200.0);
  for (std::size_t i = 0; i < 2; ++i) {
    Near(halfIn[i].opacity, 0.5, 1e-12, "Configured fade-in duration ignored");
    Near(halfOut[i].opacity, 0.5, 1e-12,
         "Configured fade-out duration ignored");
    Near(a[i].width, settings.widthMin, 0.0, "Configured star width ignored");
    Near(a[i].rotationDeg, -10.0 + 60.0 * 100.0 / 1400.0, 1e-12,
         "Configured whole-flight spin ignored");
    const double acceleration =
        (b[i].y - 2.0 * a[i].y + settings.originY) / 20000.0;
    const double velocity =
        (a[i].y - settings.originY - acceleration * 10000.0) / 100.0;
    const double endY =
        settings.originY + velocity * 1400.0 + acceleration * 1400.0 * 1400.0;
    Near(endY, settings.endYMin, 1e-9,
         "Configured exact terminal height ignored");
    Near(velocity * velocity / (4.0 * acceleration), settings.riseMin, 1e-9,
         "Configured exact rise ignored");
  }
  Require(system.Sample(1400.0).empty(), "Configured lifetime ignored");
  // Sampling many random widths and rotations still respects both PNG ratios.
  settings.widthMin = 19.0;
  settings.widthMax = 23.0;
  system.Configure(settings);
  for (int pair = 0; pair < 64; ++pair) {
    system.Reset();
    system.SpawnPair(0.0);
    for (double t = 0.0; t < settings.durationMaxMs; t += 7.0)
      for (const auto &sample : system.Sample(t))
        Bounds(sample, settings);
  }
}

void TestThreeColorBagAndUnchangedMotion() {
  Settings settings;
  settings.blueEnabled = true;
  bool sawBluePair = false, sawNonBluePair = false;
  for (std::uint32_t seed = 0; seed < 64; ++seed) {
    System legacy(seed), colored(seed), repeat(seed);
    colored.Configure(settings);
    repeat.Configure(settings);
    std::array<int, 3> counts{};
    for (int event = 0; event < 64; ++event) {
      // Reset must not restart an unfinished palette bag when the window hides.
      legacy.Reset();
      colored.Reset();
      repeat.Reset();
      legacy.SpawnPair(0.0);
      colored.SpawnPair(0.0);
      repeat.SpawnPair(0.0);
      const auto pair = colored.Sample(250.0);
      Require(pair.size() == 2,
              "Three-color event did not emit exactly two stars");
      sawBluePair = sawBluePair || (pair[0].color == Color::Blue &&
                                    pair[1].color == Color::Blue);
      sawNonBluePair = sawNonBluePair || (pair[0].color != Color::Blue &&
                                          pair[1].color != Color::Blue);
      for (const auto &sample : pair)
        ++counts[static_cast<std::size_t>(sample.color)];
      if (event % 2 == 1) {
        Require(counts[static_cast<std::size_t>(Color::Blue)] == 2 &&
                    counts[static_cast<std::size_t>(Color::Pink)] == 1 &&
                    counts[static_cast<std::size_t>(Color::Yellow)] == 1,
                "Completed palette bag did not preserve exact blue:pink:yellow "
                "2:1:1");
        counts = {};
      }
      for (double t = 0.0; t < settings.durationMaxMs; t += 7.0) {
        (void)repeat.Sample(
            t - 0.25); // Different sampling cadence cannot affect state.
        const auto samples = colored.Sample(t);
        Same(samples, repeat.Sample(t));
        Same(samples, legacy.Sample(t), false);
        for (const auto &sample : samples)
          Bounds(sample, settings);
      }
    }
  }
  Require(sawBluePair && sawNonBluePair,
          "Shuffled palette remained a fixed one-blue pair pattern");

  // Image metadata can differ for optional third-party skins; each color keeps
  // its own proportions while sharing the same conservative flight envelope.
  settings.blueAspectRatio = 0.625;
  settings.yellowAspectRatio = 2.0;
  System custom(971);
  custom.Configure(settings);
  bool sawBlue = false;
  for (int event = 0; event < 64; ++event) {
    custom.Reset();
    custom.SpawnPair(0.0);
    for (double t = 0.0; t < settings.durationMaxMs; t += 7.0)
      for (const auto &sample : custom.Sample(t)) {
        Bounds(sample, settings);
        sawBlue = sawBlue || sample.color == Color::Blue;
      }
  }
  Require(sawBlue, "Custom three-color ratios did not exercise blue");
  settings.blueAspectRatio = 0.25;
  Require(!System::IsValidSettings(settings),
          "Unsafe blue image ratio escaped complete geometry validation");
}

void TestInvalidSettings() {
  const Settings defaults;
  Require(System::IsValidSettings(defaults), "Default settings invalid");
  const auto reject = [](const Settings &settings) {
    Require(!System::IsValidSettings(settings),
            "Invalid complete settings accepted");
    System system(12);
    system.Configure(settings);
    system.SpawnPair(0.0);
    Require(system.Sample(0.0).empty() && !system.HasLive(0.0) &&
                !system.NextFrameMs(0.0),
            "Invalid settings emitted stars or scheduled a frame");
  };
  double Settings::*const fields[] = {&Settings::originX,
                                      &Settings::originY,
                                      &Settings::minX,
                                      &Settings::maxX,
                                      &Settings::minY,
                                      &Settings::maxY,
                                      &Settings::widthMin,
                                      &Settings::widthMax,
                                      &Settings::riseMin,
                                      &Settings::riseMax,
                                      &Settings::endXMin,
                                      &Settings::endXMax,
                                      &Settings::endYMin,
                                      &Settings::endYMax,
                                      &Settings::durationMinMs,
                                      &Settings::durationMaxMs,
                                      &Settings::fadeInMinMs,
                                      &Settings::fadeInMaxMs,
                                      &Settings::fadeOutMinMs,
                                      &Settings::fadeOutMaxMs,
                                      &Settings::rotationMinDeg,
                                      &Settings::rotationMaxDeg,
                                      &Settings::spinMinDeg,
                                      &Settings::spinMaxDeg,
                                      &Settings::pairSeparation,
                                      &Settings::yellowAspectRatio,
                                      &Settings::pinkAspectRatio,
                                      &Settings::blueAspectRatio};
  for (const auto field : fields) {
    Settings bad = defaults;
    bad.*field = std::numeric_limits<double>::quiet_NaN();
    reject(bad);
    bad.*field = std::numeric_limits<double>::infinity();
    reject(bad);
  }
  const auto changed = [&reject, &defaults](double Settings::*field,
                                            double value) {
    Settings bad = defaults;
    bad.*field = value;
    reject(bad);
  };
  changed(&Settings::originX, 0.0);
  changed(&Settings::originY, -95.0);
  changed(&Settings::minX, defaults.maxX);
  changed(&Settings::maxY, defaults.minY);
  changed(&Settings::widthMin, 0.0);
  changed(&Settings::widthMin, defaults.widthMax + 1.0);
  changed(&Settings::widthMax, 70.0);
  changed(&Settings::riseMin, 0.0);
  changed(&Settings::riseMin, defaults.riseMax + 1.0);
  changed(&Settings::riseMax, 200.0);
  changed(&Settings::endXMin, defaults.endXMax + 1.0);
  changed(&Settings::endXMax, defaults.minX);
  changed(&Settings::endYMin, defaults.endYMax + 1.0);
  changed(&Settings::endYMin, defaults.originY - 1.0);
  changed(&Settings::endYMax, defaults.maxY);
  changed(&Settings::durationMinMs, 0.0);
  changed(&Settings::durationMinMs, defaults.durationMaxMs + 1.0);
  changed(&Settings::fadeInMinMs, 0.0);
  changed(&Settings::fadeInMinMs, defaults.fadeInMaxMs + 1.0);
  changed(&Settings::fadeOutMinMs, 0.0);
  changed(&Settings::fadeOutMinMs, defaults.fadeOutMaxMs + 1.0);
  changed(&Settings::fadeOutMaxMs, defaults.durationMinMs);
  changed(&Settings::rotationMinDeg, defaults.rotationMaxDeg + 1.0);
  changed(&Settings::spinMinDeg, defaults.spinMaxDeg + 1.0);
  changed(&Settings::pairSeparation, 0.0);
  changed(&Settings::pairSeparation, 100.0);
  changed(&Settings::yellowAspectRatio, 0.0);
  changed(&Settings::blueAspectRatio, 0.0);
  changed(&Settings::pinkAspectRatio, 0.25);
  Settings overflow = defaults;
  overflow.rotationMaxDeg = overflow.spinMaxDeg =
      std::numeric_limits<double>::max();
  reject(overflow);
  overflow = defaults;
  overflow.rotationMinDeg = -std::numeric_limits<double>::max();
  overflow.rotationMaxDeg = std::numeric_limits<double>::max();
  reject(overflow);
  overflow = defaults;
  overflow.spinMinDeg = -std::numeric_limits<double>::max();
  overflow.spinMaxDeg = std::numeric_limits<double>::max();
  reject(overflow);
  changed(&Settings::fadeInMinMs, std::numeric_limits<double>::denorm_min());
  changed(&Settings::fadeOutMinMs, std::numeric_limits<double>::denorm_min());
  System system(56);
  system.SpawnPair(std::numeric_limits<double>::max());
  system.SpawnPair(std::numeric_limits<double>::quiet_NaN());
  Require(system.Sample(0.0).empty(),
          "Nonrepresentable event time emitted stars");
  Require(system.Sample(std::numeric_limits<double>::infinity()).empty() &&
              !system.HasLive(std::numeric_limits<double>::infinity()) &&
              !system.NextFrameMs(std::numeric_limits<double>::infinity()),
          "Nonfinite sample time produced state or a deadline");
}

void WriteSettings(std::ostream &stream, const Settings &settings) {
  stream << std::setprecision(17) << '{';
  bool first = true;
  const auto write = [&stream, &first](const char *key, double value) {
    if (!first)
      stream << ',';
    first = false;
    stream << '"' << key << "\":" << value;
  };
#define WRITE_SETTING(field) write(#field, settings.field)
  WRITE_SETTING(originX);
  WRITE_SETTING(originY);
  WRITE_SETTING(minX);
  WRITE_SETTING(maxX);
  WRITE_SETTING(minY);
  WRITE_SETTING(maxY);
  WRITE_SETTING(widthMin);
  WRITE_SETTING(widthMax);
  WRITE_SETTING(riseMin);
  WRITE_SETTING(riseMax);
  WRITE_SETTING(endXMin);
  WRITE_SETTING(endXMax);
  WRITE_SETTING(endYMin);
  WRITE_SETTING(endYMax);
  WRITE_SETTING(durationMinMs);
  WRITE_SETTING(durationMaxMs);
  WRITE_SETTING(fadeInMinMs);
  WRITE_SETTING(fadeInMaxMs);
  WRITE_SETTING(fadeOutMinMs);
  WRITE_SETTING(fadeOutMaxMs);
  WRITE_SETTING(rotationMinDeg);
  WRITE_SETTING(rotationMaxDeg);
  WRITE_SETTING(spinMinDeg);
  WRITE_SETTING(spinMaxDeg);
  WRITE_SETTING(pairSeparation);
  WRITE_SETTING(yellowAspectRatio);
  WRITE_SETTING(pinkAspectRatio);
  WRITE_SETTING(blueAspectRatio);
#undef WRITE_SETTING
  stream << '}' << std::setprecision(6);
}

void WriteFrames(const std::filesystem::path &output) {
  std::ofstream stream(output, std::ios::binary | std::ios::trunc);
  Require(stream.is_open(), "Cannot write preview frames JSON");
  stream << std::fixed << std::setprecision(6);
  stream << "{\"schema\":\"kasumi.typed-star-particle-frames/"
            "v1\",\"units\":\"card-local DIP and monotonic ms\","
            "\"seed\":20261022,\"fps\":60,\"durationMs\":4800,"
            "\"note\":\"Synthetic two-character event pairs only; no user text "
            "or input data.\","
            "\"settingsSource\":\"CandidateTypedStars::Settings defaults; not "
            "a package configuration read\","
            "\"settings\":";
  WriteSettings(stream, Settings{});
  stream << ",\"spawnPairMs\":[0,300,650,1100,1450,1800],\"states\":[";
  System system(20261022);
  constexpr std::array<double, 6> events{0.0,    300.0,  650.0,
                                         1100.0, 1450.0, 1800.0};
  std::size_t event = 0;
  for (int frame = 0; frame <= 288; ++frame) {
    const double time = frame * System::kFrameMs;
    while (event < events.size() && events[event] <= time + 1e-8) {
      system.SpawnPair(events[event]);
      ++event;
    }
    if (frame != 0)
      stream << ',';
    stream << "{\"timeMs\":" << time << ",\"particles\":[";
    const auto samples = system.Sample(time);
    for (std::size_t i = 0; i < samples.size(); ++i) {
      const auto &s = samples[i];
      if (i != 0)
        stream << ',';
      stream << "{\"x\":" << s.x << ",\"y\":" << s.y << ",\"width\":" << s.width
             << ",\"height\":" << s.height
             << ",\"rotationDeg\":" << s.rotationDeg
             << ",\"opacity\":" << s.opacity
             << ",\"pink\":" << (s.color == Color::Pink ? "true" : "false")
             << '}';
    }
    stream << "]}";
  }
  stream << "]}\n";
  Require(stream.good(), "Preview frames JSON write failed");
}
} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    TestPairAndReproducibility();
    TestAnalyticFlight();
    TestLifetimeAndFade();
    TestRandomnessAndBounds();
    TestCapacityAndReset();
    TestCustomSettingsAndImageRatios();
    TestThreeColorBagAndUnchangedMotion();
    TestInvalidSettings();
    if (argc == 2)
      WriteFrames(std::filesystem::path(argv[1]));
    std::cout
        << "PASS: pairs, reproducibility, randomness, analytic "
           "trajectory, cadence independence, smooth alpha, "
           "19-23 DIP widths, exact 96-108 DIP endpoints, configured PNG "
           "aspect ratios, "
           "rotated bounds, capacity, reset, invalid paired ranges, "
           "nonfinite values, deadlines, exact three-color 2:1:1 shuffled "
           "bags, "
           "legacy two-color compatibility, identical motion across palettes, "
           "three PNG aspect ratios\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
