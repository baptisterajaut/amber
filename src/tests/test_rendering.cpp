#include <rhi/qrhi.h>
#include <QApplication>
#include <QOffscreenSurface>
#include <QTest>

#include "core/appcontext.h"
#include "engine/sequence.h"
#include "global/config.h"
#include "tests/test_appcontext_stub.h"

class TestRendering : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase();
  void cleanupTestCase();
  void sequenceCreation();
  void rhiClearAndReadback();

 private:
  TestAppContext stub_ctx_;
};

void TestRendering::initTestCase() {
  amber::app_ctx = &stub_ctx_;
  amber::CurrentConfig = {};
}

void TestRendering::cleanupTestCase() { amber::app_ctx = nullptr; }

void TestRendering::sequenceCreation() {
  auto seq = std::make_shared<Sequence>();
  seq->name = "Test Sequence";
  seq->width = 320;
  seq->height = 240;
  seq->frame_rate = 30.0;
  seq->audio_frequency = 48000;
  seq->audio_layout = 3;

  QCOMPARE(seq->width, 320);
  QCOMPARE(seq->height, 240);
  QCOMPARE(seq->clips.size(), 0);
}

void TestRendering::rhiClearAndReadback() {
  QRhiGles2InitParams params;
  params.fallbackSurface = QRhiGles2InitParams::newFallbackSurface();

  {
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::OpenGLES2, &params));
    if (!rhi) {
      delete params.fallbackSurface;
      QSKIP("QRhi OpenGLES2 not available");
    }

    // Create 320x240 RGBA8 texture
    std::unique_ptr<QRhiTexture> tex(rhi->newTexture(QRhiTexture::RGBA8, QSize(320, 240), 1,
                                                     QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    QVERIFY(tex->create());

    // Create render target
    QRhiColorAttachment att(tex.get());
    std::unique_ptr<QRhiTextureRenderTarget> rt(rhi->newTextureRenderTarget({att}));
    std::unique_ptr<QRhiRenderPassDescriptor> rp(rt->newCompatibleRenderPassDescriptor());
    rt->setRenderPassDescriptor(rp.get());
    QVERIFY(rt->create());

    // Begin offscreen frame
    QRhiCommandBuffer* cb = nullptr;
    QRhi::FrameOpResult result = rhi->beginOffscreenFrame(&cb);
    QCOMPARE(result, QRhi::FrameOpSuccess);
    QVERIFY(cb != nullptr);

    // Clear to green
    cb->beginPass(rt.get(), QColor(0, 255, 0, 255), {1.0f, 0});
    cb->endPass();

    // Readback
    QRhiReadbackResult readResult;
    bool readCompleted = false;
    readResult.completed = [&readCompleted] { readCompleted = true; };
    QRhiResourceUpdateBatch* u = rhi->nextResourceUpdateBatch();
    u->readBackTexture(QRhiReadbackDescription(tex.get()), &readResult);
    cb->resourceUpdate(u);

    rhi->endOffscreenFrame();

    QVERIFY(readCompleted);
    QVERIFY(!readResult.data.isEmpty());

    // Verify pixels — should be green (RGBA: 0, 255, 0, 255)
    const uchar* pixels = reinterpret_cast<const uchar*>(readResult.data.constData());
    QVERIFY(readResult.data.size() >= 4);
    QVERIFY2(pixels[0] < 10, "Red channel should be ~0 for green clear");
    QVERIFY2(pixels[1] > 240, "Green channel should be ~255 for green clear");
    QVERIFY2(pixels[2] < 10, "Blue channel should be ~0 for green clear");
  }

  delete params.fallbackSurface;
}

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  TestRendering test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_rendering.moc"
