// Headless render benchmark: wall-clock ms per frame of compose_sequence() + readback through the test harness, on
// three fixed scenarios. Not a test (no ctest entry): run by hand before and after a rendering change.
//
//   amber-bench-render [--rhi gl|vulkan] [--frames N] [--scenario solid|fx5|stack4]
//
// --rhi gl renders on the offscreen QPA platform; --rhi vulkan needs a real one (wayland/xcb, xvfb-run headless).
//
// QRhiStats::totalPipelineCreationTime is not reported: it sums whole milliseconds per pipeline, so sub-ms
// creations count as 0.

#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QSurfaceFormat>

#include <algorithm>
#include <clocale>
#include <cstdio>

#include "effects/effect.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "tests/test_render_harness.h"

namespace {

constexpr int kWidth = 1920;
constexpr int kHeight = 1080;
constexpr double kFps = 30.0;
constexpr int kWarmup = 10;

SequencePtr build(TestRenderHarness& h, const QString& scenario, long length) {
  SequencePtr seq = h.make_sequence(kWidth, kHeight, kFps);
  if (scenario == "solid") {
    h.add_generator_clip(seq.get(), -1, 0, length, EFFECT_INTERNAL_SOLID);
  } else if (scenario == "fx5") {
    Clip* c = h.add_generator_clip(seq.get(), -1, 0, length, EFFECT_INTERNAL_SOLID);
    for (const char* name : {"Invert", "Gaussian Blur", "Hue/Saturation/Brightness", "Vignette", "Sharpen"}) {
      h.attach_xml_shader(c, name);
    }
  } else if (scenario == "stack4") {
    for (int t = 1; t <= 4; t++) {
      Clip* c = h.add_generator_clip(seq.get(), -t, 0, length, EFFECT_INTERNAL_SOLID);
      Effect* transform = c->effects[0].get();
      h.set_field_double(transform, 1, 0, 50.0 + 10 * t);  // scale
      h.set_field_double(transform, 5, 0, 60.0);           // opacity
    }
  } else {
    return nullptr;
  }
  return seq;
}

}  // namespace

int main(int argc, char* argv[]) {
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addOption({"rhi", "gl or vulkan", "backend", "gl"});
  parser.addOption({"frames", "frames per scenario", "n", "300"});
  parser.addOption({"scenario", "solid, fx5 or stack4 (default: all)", "name"});

  // The offscreen QPA plugin cannot create a QVulkanInstance ("This plugin does not support
  // createPlatformVulkanInstance"), so --rhi vulkan keeps the session's platform (wayland/xcb, xvfb-run when
  // headless). Parse once before QGuiApplication to know which; process() below reports bad options.
  QStringList args;
  for (int i = 0; i < argc; i++) args << QString::fromLocal8Bit(argv[i]);
  parser.parse(args);
  if (parser.value("rhi") != "vulkan") qputenv("QT_QPA_PLATFORM", "offscreen");
  // Qt sends warnings to journald when stderr is not a terminal; keep them next to the results when redirected
  qputenv("QT_FORCE_STDERR_LOGGING", "1");
  // Same default format as the GPU tests: GL 3.2 core so the GL QRhi gets GLSL 150
  QSurfaceFormat fmt;
  fmt.setVersion(3, 2);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setRenderableType(QSurfaceFormat::OpenGL);
  QSurfaceFormat::setDefaultFormat(fmt);
  QGuiApplication app(argc, argv);
  // QCoreApplication applies the user locale (setlocale LC_ALL ""): keep printf's decimal point a '.'
  std::setlocale(LC_NUMERIC, "C");

  parser.process(app);

  const QString rhi = parser.value("rhi");
  if (rhi != "gl" && rhi != "vulkan") {
    std::fprintf(stderr, "--rhi must be gl or vulkan\n");
    return 2;
  }
  const int frames = parser.value("frames").toInt();
  if (frames < 1) {
    std::fprintf(stderr, "--frames must be >= 1\n");
    return 2;
  }
  QStringList scenarios = {"solid", "fx5", "stack4"};
  if (parser.isSet("scenario")) scenarios = {parser.value("scenario")};

  TestRenderHarness h(rhi == "vulkan" ? QRhi::Vulkan : QRhi::OpenGLES2);
  if (!h.initialized()) {
    std::fprintf(stderr, "QRhi %s could not be created (see the warning above)\n", qPrintable(rhi));
    return 1;
  }

  std::printf("%-8s %-7s %7s %12s %12s\n", "scenario", "rhi", "frames", "median ms", "p95 ms");
  for (const QString& scenario : scenarios) {
    const long length = frames + kWarmup;
    SequencePtr seq = build(h, scenario, length);
    if (seq == nullptr) {
      std::fprintf(stderr, "unknown scenario %s\n", qPrintable(scenario));
      return 2;
    }
    for (int f = 0; f < kWarmup; f++) h.render_frame(seq.get(), f);
    QVector<double> ms;
    ms.reserve(frames);
    QElapsedTimer timer;
    for (int f = kWarmup; f < length; f++) {
      timer.start();
      const QByteArray pixels = h.render_frame(seq.get(), f);
      ms.append(timer.nsecsElapsed() / 1e6);
      if (pixels.isEmpty()) {
        std::fprintf(stderr, "%s: render failed at frame %d\n", qPrintable(scenario), f);
        return 1;
      }
    }
    std::sort(ms.begin(), ms.end());
    std::printf("%-8s %-7s %7d %12.2f %12.2f\n", qPrintable(scenario), qPrintable(rhi), frames, ms[ms.size() / 2],
                ms[qMin<int>(ms.size() - 1, ms.size() * 95 / 100)]);
  }
  return 0;
}
