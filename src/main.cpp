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

#include <rhi/qrhi.h>
#include <QApplication>
#include <QWindow>

#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#endif
#if QT_CONFIG(vulkan) && defined(VK_VERSION_1_0)
#define AMBER_HAS_VULKAN 1
#endif

#include "global/config.h"
#include "global/debug.h"
#include "global/global.h"
#include "panels/timeline.h"
#include "ui/mainwindow.h"
#include "ui/mediaiconservice.h"

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
}

#if AMBER_HAS_VULKAN
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

static void print_version() {
#ifndef GITHASH
  qWarning() << "No Git commit information found";
#endif
  printf("%s\n", amber::AppName.toUtf8().constData());
}

static void print_help(const char* prog) {
  printf(
      "Usage: %s [options] [filename]\n\n"
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
      "\tAMBER_EFFECTS_PATH\tSpecify a path to search for GLSL shader effects\n"
      "\tFREI0R_PATH\t\tSpecify a path to search for Frei0r effects\n"
      "\tAMBER_LANG_PATH\t\tSpecify a path to search for translation files\n"
      "\n",
      prog);
}

// Process a single flag argument at argv[i]. Returns false if the caller should exit.
// `i` may be incremented if the flag consumes the next token.
static bool handle_flag(int argc, char* argv[], int& i, bool& launch_fullscreen, bool& use_internal_logger) {
  const char* arg = argv[i];
  if (!strcmp(arg, "--version") || !strcmp(arg, "-v")) {
    print_version();
    return false;
  }
  if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
    print_help(argv[0]);
    return false;
  }
  if (!strcmp(arg, "--fullscreen") || !strcmp(arg, "-f")) {
    launch_fullscreen = true;
    return true;
  }
  if (!strcmp(arg, "--rhi-backend")) {
    if (i + 1 < argc) {
      amber::CurrentRuntimeConfig.rhi_backend = parseRhiBackend(argv[++i]);
      return true;
    }
    printf("[ERROR] No backend name specified\n");
    return false;
  }
  if (!strcmp(arg, "--disable-shaders")) {
    amber::CurrentRuntimeConfig.shaders_are_enabled = false;
    return true;
  }
  if (!strcmp(arg, "--no-debug")) {
    use_internal_logger = false;
    return true;
  }
  if (!strcmp(arg, "--disable-blend-modes")) {
    amber::CurrentRuntimeConfig.disable_blending = true;
    return true;
  }
  if (!strcmp(arg, "--translation")) {
    if (i + 1 < argc && argv[i + 1][0] != '-') {
      amber::CurrentRuntimeConfig.external_translation_file = argv[++i];
      return true;
    }
    printf("[ERROR] No translation file specified\n");
    return false;
  }
  printf("[ERROR] Unknown argument '%s'\n", arg);
  return false;
}

// Parse command-line arguments. Returns false if the caller should exit (--version/--help/error).
// Sets launch_fullscreen, load_proj, use_internal_logger via out-params; also mutates
// amber::CurrentRuntimeConfig directly for backend/shader flags.
static bool parse_args(int argc, char* argv[], bool& launch_fullscreen, QString& load_proj, bool& use_internal_logger) {
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      if (!handle_flag(argc, argv, i, launch_fullscreen, use_internal_logger)) return false;
    } else if (load_proj.isEmpty()) {
      load_proj = argv[i];
    }
  }
  return true;
}

#if AMBER_HAS_VULKAN
static void setup_vulkan_instance() {
  RhiBackend b = amber::CurrentRuntimeConfig.rhi_backend;
  bool want_vulkan = (b == RhiBackend::Auto || b == RhiBackend::Vulkan);
  if (!want_vulkan) return;

  s_vulkanInstance.setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
  if (!s_vulkanInstance.create()) {
    qWarning() << "Failed to create QVulkanInstance, Vulkan backend unavailable";
    return;
  }
  amber::CurrentRuntimeConfig.vulkan_instance = &s_vulkanInstance;
}

static void probe_vulkan_device() {
  if (amber::CurrentRuntimeConfig.vulkan_instance == nullptr) return;

  QRhiVulkanInitParams probeParams;
  probeParams.inst = &s_vulkanInstance;
  std::unique_ptr<QRhi> probe(QRhi::create(QRhi::Vulkan, &probeParams));
  if (!probe) {
    qWarning() << "Vulkan instance created but no usable GPU found, disabling Vulkan";
    amber::CurrentRuntimeConfig.vulkan_instance = nullptr;
    return;
  }
  if (probe->driverInfo().deviceType == QRhiDriverInfo::CpuDevice) {
    qWarning() << "Vulkan device is software-only (" << probe->driverInfo().deviceName
               << "), preferring OpenGL (Vulkan kept as last-resort fallback)";
    amber::CurrentRuntimeConfig.vulkan_is_software = true;
  }
}
#endif

static void validate_explicit_backend() {
  RhiBackend b = amber::CurrentRuntimeConfig.rhi_backend;
  if (b == RhiBackend::Vulkan && amber::CurrentRuntimeConfig.vulkan_instance == nullptr) {
    qWarning() << "Vulkan requested but unavailable, falling back to OpenGL";
    amber::CurrentRuntimeConfig.rhi_backend = RhiBackend::OpenGL;
  }
#if !defined(Q_OS_MACOS) && !defined(Q_OS_IOS)
  if (b == RhiBackend::Metal) {
    qWarning() << "Metal backend only available on macOS/iOS, falling back to OpenGL";
    amber::CurrentRuntimeConfig.rhi_backend = RhiBackend::OpenGL;
  }
#endif
#if !defined(Q_OS_WIN)
  if (b == RhiBackend::D3D12 || b == RhiBackend::D3D11) {
    qWarning() << "D3D12/D3D11 backend only available on Windows, falling back to OpenGL";
    amber::CurrentRuntimeConfig.rhi_backend = RhiBackend::OpenGL;
  }
#endif
}

static void resolve_auto_backend() {
  if (amber::CurrentRuntimeConfig.rhi_backend != RhiBackend::Auto) return;

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
  amber::CurrentRuntimeConfig.rhi_backend = RhiBackend::Metal;
#elif defined(Q_OS_WIN)
  amber::CurrentRuntimeConfig.rhi_backend = RhiBackend::D3D12;
#else
  amber::CurrentRuntimeConfig.rhi_backend =
      (amber::CurrentRuntimeConfig.vulkan_instance != nullptr && !amber::CurrentRuntimeConfig.vulkan_is_software)
          ? RhiBackend::Vulkan
          : RhiBackend::OpenGL;
#endif
}

static void apply_env_rhi_backend() {
  if (amber::CurrentRuntimeConfig.rhi_backend != RhiBackend::Auto) return;
  QByteArray env = qgetenv("AMBER_RHI_BACKEND");
  if (!env.isEmpty()) {
    amber::CurrentRuntimeConfig.rhi_backend = parseRhiBackend(env.constData());
  }
}

static void setup_qt_surface_format() {
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QSurfaceFormat format;
  format.setDepthBufferSize(24);
  format.setVersion(3, 2);
  format.setProfile(QSurfaceFormat::CompatibilityProfile);
  QSurfaceFormat::setDefaultFormat(format);
}

static void set_app_metadata() {
  QCoreApplication::setOrganizationName("ambervideoeditor.org");
  QCoreApplication::setOrganizationDomain("ambervideoeditor.org");
  QCoreApplication::setApplicationName("Amber");
  QGuiApplication::setDesktopFileName("org.ambervideoeditor.Amber");
}

#if AMBER_HAS_VULKAN
static void attach_vulkan_to_window(MainWindow& w) {
  if (!s_vulkanInstance.isValid()) return;
  w.winId();  // force native window handle creation
  if (w.windowHandle()) {
    w.windowHandle()->setVulkanInstance(&s_vulkanInstance);
  }
}
#endif

int main(int argc, char* argv[]) {
  amber::Global = std::unique_ptr<AmberGlobal>(new AmberGlobal);

  bool launch_fullscreen = false;
  QString load_proj;
  bool use_internal_logger = true;

  if (argc > 1 && !parse_args(argc, argv, launch_fullscreen, load_proj, use_internal_logger)) {
    return 0;
  }

  apply_env_rhi_backend();

  if (use_internal_logger) qInstallMessageHandler(debug_message_handler);

  // Work around Qt 6.10 PipeWire backend bug: Bluetooth audio sinks are not
  // enumerated.  Force the PulseAudio backend which works correctly through
  // pipewire-pulse.  Safe on native PulseAudio systems and no-ops on Windows/macOS.
  qputenv("QT_AUDIO_BACKEND", "pulseaudio");

  setup_qt_surface_format();

  QApplication a(argc, argv);
  a.setWindowIcon(QIcon(":/icons/amber64.png"));

  // Create Vulkan instance (needed by both QRhiWidget and offscreen QRhi when using Vulkan)
#if AMBER_HAS_VULKAN
  setup_vulkan_instance();
  probe_vulkan_device();
#endif

  validate_explicit_backend();
  resolve_auto_backend();

  amber::media_icon_service = std::unique_ptr<MediaIconService>(new MediaIconService());

  set_app_metadata();

  MainWindow w(nullptr);

  // Associate Vulkan instance with the main window (required for QRhiWidget Vulkan backend)
#if AMBER_HAS_VULKAN
  attach_vulkan_to_window(w);
#endif

  amber::timeline::MultiplyTrackSizesByDPI();

  QObject::connect(&w, &MainWindow::finished_first_paint, amber::Global.get(), &AmberGlobal::finished_initialize,
                   Qt::QueuedConnection);

  if (!load_proj.isEmpty()) amber::Global->load_project_on_launch(load_proj);

  if (launch_fullscreen) {
    w.showFullScreen();
  } else {
    w.showMaximized();
  }

  int ret = a.exec();

  // Null the Vulkan instance so QWaylandVulkanWindow::invalidateSurface()
  // skips its vkDestroySurfaceKHR call, preventing a double-free.
#if AMBER_HAS_VULKAN
  if (w.windowHandle()) w.windowHandle()->setVulkanInstance(nullptr);
#endif

  return ret;
}
