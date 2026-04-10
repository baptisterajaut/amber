#include <rhi/qrhi.h>
#include <QApplication>
#include <QOffscreenSurface>
#include <QTest>

class TestRhiSmoke : public QObject {
  Q_OBJECT
 private slots:
  void canCreateRhi();
  void canCreateTexture();
};

void TestRhiSmoke::canCreateRhi() {
  QSurface* surface = QRhiGles2InitParams::newFallbackSurface();
  QVERIFY(surface != nullptr);

  {
    QRhiGles2InitParams params;
    params.fallbackSurface = surface;
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::OpenGLES2, &params));

    if (!rhi) {
      delete surface;
      QSKIP("QRhi OpenGLES2 creation failed (no GL context available). Install Mesa llvmpipe.");
    }

    QVERIFY(rhi->backend() == QRhi::OpenGLES2);
    // rhi destroyed here, before surface
  }

  delete surface;
}

void TestRhiSmoke::canCreateTexture() {
  QSurface* surface = QRhiGles2InitParams::newFallbackSurface();

  {
    QRhiGles2InitParams params;
    params.fallbackSurface = surface;
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::OpenGLES2, &params));

    if (!rhi) {
      delete surface;
      QSKIP("QRhi not available");
    }

    std::unique_ptr<QRhiTexture> tex(rhi->newTexture(QRhiTexture::RGBA8, QSize(64, 64), 1,
                                                     QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    QVERIFY(tex->create());

    QRhiColorAttachment att(tex.get());
    std::unique_ptr<QRhiTextureRenderTarget> rt(rhi->newTextureRenderTarget({att}));
    std::unique_ptr<QRhiRenderPassDescriptor> rp(rt->newCompatibleRenderPassDescriptor());
    rt->setRenderPassDescriptor(rp.get());
    QVERIFY(rt->create());
    // rp, rt, tex, rhi destroyed here in reverse order
  }

  delete surface;
}

// QApplication needed for QRhi GL context
int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  TestRhiSmoke test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_rhi_smoke.moc"
