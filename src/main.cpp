/***

    Olive - Non-Linear Video Editor
    Copyright (C) 2019  Olive Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include <QApplication>
#include <QWindow>
#include <rhi/qrhi.h>

#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#endif

#include "global/debug.h"
#include "global/config.h"
#include "global/global.h"
#include "panels/timeline.h"
#include "ui/mediaiconservice.h"
#include "ui/mainwindow.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
}

#if QT_CONFIG(vulkan)
static QVulkanInstance s_vulkanInstance;
#endif

static RhiBackend parseRhiBackend(const char* name) {
  if (!strcmp(name, "vulkan")) return RhiBackend::Vulkan;
  if (!strcmp(name, "metal")) return RhiBackend::Metal;
  if (!strcmp(name, "d3d12")) return RhiBackend::D3D12;
  if (!strcmp(name, "d3d11")) return RhiBackend::D3D11;
  if (!strcmp(name, "opengl") || !strcmp(name, "gl")) return RhiBackend::OpenGL;
  printf("[WARNING] Unknown RHI backend '%s', using auto\n", name);
  return RhiBackend::Auto;
}

int main(int argc, char *argv[]) {
  olive::Global = std::unique_ptr<OliveGlobal>(new OliveGlobal);

  bool launch_fullscreen = false;
  QString load_proj;

  bool use_internal_logger = true;

  if (argc > 1) {
    for (int i=1;i<argc;i++) {
      if (argv[i][0] == '-') {
        if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
#ifndef GITHASH
          qWarning() << "No Git commit information found";
#endif
          printf("%s\n", olive::AppName.toUtf8().constData());
          return 0;
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
          printf("Usage: %s [options] [filename]\n\n"
                 "[filename] is the file to open on startup.\n\n"
                 "Options:\n"
                 "\t-v, --version\t\tShow version information\n"
                 "\t-h, --help\t\tShow this help\n"
                 "\t-f, --fullscreen\tStart in full screen mode\n"
                 "\t--rhi-backend <name>\tSet RHI backend (vulkan, metal, d3d12, d3d11, opengl)\n"
                 "\t--disable-shaders\tDisable shaders (for debugging)\n"
                 "\t--no-debug\t\tDisable internal debug log and output directly to console\n"
                 "\t--disable-blend-modes\tDisable shader-based blending for older GPUs\n"
                 "\t--translation <file>\tSet an external language file to use\n"
                 "\n"
                 "Environment Variables:\n"
                 "\tAMBER_RHI_BACKEND\tSet RHI backend (vulkan, metal, d3d12, d3d11, opengl)\n"
                 "\tOLIVE_EFFECTS_PATH\tSpecify a path to search for GLSL shader effects\n"
                 "\tFREI0R_PATH\t\tSpecify a path to search for Frei0r effects\n"
                 "\tOLIVE_LANG_PATH\t\tSpecify a path to search for translation files\n"
                 "\n", argv[0]);
          return 0;
        } else if (!strcmp(argv[i], "--fullscreen") || !strcmp(argv[i], "-f")) {
          launch_fullscreen = true;
        } else if (!strcmp(argv[i], "--rhi-backend")) {
          if (i + 1 < argc) {
            olive::CurrentRuntimeConfig.rhi_backend = parseRhiBackend(argv[++i]);
          } else {
            printf("[ERROR] No backend name specified\n");
            return 1;
          }
        } else if (!strcmp(argv[i], "--disable-shaders")) {
          olive::CurrentRuntimeConfig.shaders_are_enabled = false;
        } else if (!strcmp(argv[i], "--no-debug")) {
          use_internal_logger = false;
        } else if (!strcmp(argv[i], "--disable-blend-modes")) {
          olive::CurrentRuntimeConfig.disable_blending = true;
        } else if (!strcmp(argv[i], "--translation")) {
          if (i + 1 < argc && argv[i + 1][0] != '-') {
            olive::CurrentRuntimeConfig.external_translation_file = argv[++i];
          } else {
            printf("[ERROR] No translation file specified\n");
            return 1;
          }
        } else {
          printf("[ERROR] Unknown argument '%s'\n", argv[i]);
          return 1;
        }
      } else if (load_proj.isEmpty()) {
        load_proj = argv[i];
      }
    }
  }

  // Env var override (CLI takes precedence)
  if (olive::CurrentRuntimeConfig.rhi_backend == RhiBackend::Auto) {
    QByteArray env = qgetenv("AMBER_RHI_BACKEND");
    if (!env.isEmpty()) {
      olive::CurrentRuntimeConfig.rhi_backend = parseRhiBackend(env.constData());
    }
  }

  if (use_internal_logger) {
    qInstallMessageHandler(debug_message_handler);
  }

  // Work around Qt 6.10 PipeWire backend bug: Bluetooth audio sinks are not
  // enumerated.  Force the PulseAudio backend which works correctly through
  // pipewire-pulse.  Safe on native PulseAudio systems and no-ops on Windows/macOS.
  qputenv("QT_AUDIO_BACKEND", "pulseaudio");

  // OpenGL fallback surface format (used when GL backend is selected)
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QSurfaceFormat format;
  format.setDepthBufferSize(24);
  format.setVersion(3, 2);
  format.setProfile(QSurfaceFormat::CompatibilityProfile);
  QSurfaceFormat::setDefaultFormat(format);

  QApplication a(argc, argv);
  a.setWindowIcon(QIcon(":/icons/amber64.png"));

  // Create Vulkan instance (needed by both QRhiWidget and offscreen QRhi when using Vulkan)
#if QT_CONFIG(vulkan)
  {
    RhiBackend b = olive::CurrentRuntimeConfig.rhi_backend;
    bool want_vulkan = (b == RhiBackend::Auto || b == RhiBackend::Vulkan);
    if (want_vulkan) {
      s_vulkanInstance.setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
      if (!s_vulkanInstance.create()) {
        qWarning() << "Failed to create QVulkanInstance, Vulkan backend unavailable";
      } else {
        olive::CurrentRuntimeConfig.vulkan_instance = &s_vulkanInstance;
      }
    }
  }
#endif

  // Probe: verify Vulkan QRhi actually works (instance can create without usable GPU)
#if QT_CONFIG(vulkan)
  if (olive::CurrentRuntimeConfig.vulkan_instance != nullptr) {
    QRhiVulkanInitParams probeParams;
    probeParams.inst = &s_vulkanInstance;
    std::unique_ptr<QRhi> probe(QRhi::create(QRhi::Vulkan, &probeParams));
    if (!probe) {
      qWarning() << "Vulkan instance created but no usable GPU found, disabling Vulkan";
      olive::CurrentRuntimeConfig.vulkan_instance = nullptr;
    }
  }
#endif

  // Validate explicit backend is available on this platform
  {
    RhiBackend b = olive::CurrentRuntimeConfig.rhi_backend;
    if (b == RhiBackend::Vulkan && olive::CurrentRuntimeConfig.vulkan_instance == nullptr) {
      qWarning() << "Vulkan requested but unavailable, falling back to OpenGL";
      olive::CurrentRuntimeConfig.rhi_backend = RhiBackend::OpenGL;
    }
#if !defined(Q_OS_MACOS) && !defined(Q_OS_IOS)
    if (b == RhiBackend::Metal) {
      qWarning() << "Metal backend only available on macOS/iOS, falling back to OpenGL";
      olive::CurrentRuntimeConfig.rhi_backend = RhiBackend::OpenGL;
    }
#endif
#if !defined(Q_OS_WIN)
    if (b == RhiBackend::D3D12 || b == RhiBackend::D3D11) {
      qWarning() << "D3D12/D3D11 backend only available on Windows, falling back to OpenGL";
      olive::CurrentRuntimeConfig.rhi_backend = RhiBackend::OpenGL;
    }
#endif
  }

  // Resolve Auto to platform-specific default
  if (olive::CurrentRuntimeConfig.rhi_backend == RhiBackend::Auto) {
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    olive::CurrentRuntimeConfig.rhi_backend = RhiBackend::Metal;
#elif defined(Q_OS_WIN)
    olive::CurrentRuntimeConfig.rhi_backend = RhiBackend::D3D12;
#else
    olive::CurrentRuntimeConfig.rhi_backend =
        (olive::CurrentRuntimeConfig.vulkan_instance != nullptr) ? RhiBackend::Vulkan : RhiBackend::OpenGL;
#endif
  }

  olive::media_icon_service = std::unique_ptr<MediaIconService>(new MediaIconService());

  QCoreApplication::setOrganizationName("ambervideoeditor.org");
  QCoreApplication::setOrganizationDomain("ambervideoeditor.org");
  QCoreApplication::setApplicationName("Amber");
  QGuiApplication::setDesktopFileName("org.ambervideoeditor.Amber");

  MainWindow w(nullptr);

  // Associate Vulkan instance with the main window (required for QRhiWidget Vulkan backend)
#if QT_CONFIG(vulkan)
  if (s_vulkanInstance.isValid()) {
    w.winId();  // force native window handle creation
    if (w.windowHandle()) {
      w.windowHandle()->setVulkanInstance(&s_vulkanInstance);
    }
  }
#endif

  olive::timeline::MultiplyTrackSizesByDPI();

  QObject::connect(&w, &MainWindow::finished_first_paint, olive::Global.get(), &OliveGlobal::finished_initialize, Qt::QueuedConnection);

  if (!load_proj.isEmpty()) {
    olive::Global->load_project_on_launch(load_proj);
  }
  if (launch_fullscreen) {
    w.showFullScreen();
  } else {
    w.showMaximized();
  }

  return a.exec();
}
