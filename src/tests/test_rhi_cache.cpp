#include <rhi/qrhi.h>
#include <QFile>
#include <QGuiApplication>
#include <QMatrix4x4>
#include <QOffscreenSurface>
#include <QRegularExpression>
#include <QSurfaceFormat>
#include <QTest>

#include "rendering/rhiresourcecache.h"
#include "tests/test_gpu_required.h"

#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#endif

#include <memory>

class TestRhiCache : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase();
  void cleanupTestCase();
  void samePipelineForSameKey();
  void differentKeysDifferentPipelines();
  void rpdFormatKeying();
  void pipelineOutlivesCallerRpd();
  void buffersDistinctWithinFrameReusedAcrossFrames();
  void bufferPoolsSeparateUsage();
  void failedPipelineCachedOnce();
  void evictionDestroysUnusedEntries();

 private:
  QShader load(const QString& name);
  QRhiTextureRenderTarget* makeTarget(QRhiTexture** tex, QRhiRenderPassDescriptor** rpd, bool preserve);

  QSurface* surface_ = nullptr;
#if QT_CONFIG(vulkan)
  QVulkanInstance vk_;
#endif
  std::unique_ptr<QRhi> rhi_;
  QShader vert_, passthrough_, premultiply_;
};

QShader TestRhiCache::load(const QString& name) {
  QFile f(QStringLiteral(":/shaders/") + name);
  if (!f.open(QIODevice::ReadOnly)) return {};
  return QShader::fromSerialized(f.readAll());
}

void TestRhiCache::initTestCase() {
  // AMBER_TEST_RHI=vulkan runs the same checks on Vulkan (local only; CI is GL)
  if (qEnvironmentVariable("AMBER_TEST_RHI") == QLatin1String("vulkan")) {
#if QT_CONFIG(vulkan)
    vk_.setApiVersion(QVersionNumber(1, 1));
    if (vk_.create()) {
      QRhiVulkanInitParams p;
      p.inst = &vk_;
      rhi_.reset(QRhi::create(QRhi::Vulkan, &p));
    }
#endif
    if (!rhi_) QSKIP("Vulkan requested but unavailable");
  } else {
    surface_ = QRhiGles2InitParams::newFallbackSurface();
    QRhiGles2InitParams p;
    p.fallbackSurface = surface_;
    rhi_.reset(QRhi::create(QRhi::OpenGLES2, &p));
    if (!rhi_) AMBER_SKIP_NO_GPU("QRhi OpenGLES2 creation failed");
  }
  vert_ = load("common.vert.qsb");
  passthrough_ = load("passthrough.frag.qsb");
  premultiply_ = load("premultiply.frag.qsb");
  QVERIFY2(vert_.isValid() && passthrough_.isValid() && premultiply_.isValid(), "core shaders missing from QRC");
}

void TestRhiCache::cleanupTestCase() {
  rhi_.reset();
  delete surface_;
}

QRhiTextureRenderTarget* TestRhiCache::makeTarget(QRhiTexture** tex, QRhiRenderPassDescriptor** rpd, bool preserve) {
  *tex = rhi_->newTexture(QRhiTexture::RGBA8, QSize(16, 16), 1, QRhiTexture::RenderTarget);
  (*tex)->create();
  QRhiTextureRenderTarget* rt = rhi_->newTextureRenderTarget(
      {*tex}, preserve ? QRhiTextureRenderTarget::PreserveColorContents : QRhiTextureRenderTarget::Flags());
  *rpd = rt->newCompatibleRenderPassDescriptor();
  rt->setRenderPassDescriptor(*rpd);
  rt->create();
  return rt;
}

void TestRhiCache::samePipelineForSameKey() {
  RhiResourceCache cache(rhi_.get());
  QRhiTexture* tex;
  QRhiRenderPassDescriptor* rpd;
  std::unique_ptr<QRhiTextureRenderTarget> rt(makeTarget(&tex, &rpd, true));
  RhiResourceCache::SrbLayout layout;
  QRhiGraphicsPipeline* a = cache.pipeline(vert_, passthrough_, RhiResourceCache::Blend::None, layout, rpd);
  QRhiGraphicsPipeline* b = cache.pipeline(vert_, passthrough_, RhiResourceCache::Blend::None, layout, rpd);
  QVERIFY(a != nullptr);
  QCOMPARE(a, b);
  QCOMPARE(cache.stats().pipelinesCreated, quint64(1));
  rt.reset();
  delete rpd;
  delete tex;
}

void TestRhiCache::differentKeysDifferentPipelines() {
  RhiResourceCache cache(rhi_.get());
  QRhiTexture* tex;
  QRhiRenderPassDescriptor* rpd;
  std::unique_ptr<QRhiTextureRenderTarget> rt(makeTarget(&tex, &rpd, true));
  RhiResourceCache::SrbLayout layout;
  RhiResourceCache::SrbLayout two_tex = layout;
  two_tex.textures = 2;
  QRhiGraphicsPipeline* base = cache.pipeline(vert_, passthrough_, RhiResourceCache::Blend::None, layout, rpd);
  QRhiGraphicsPipeline* blend =
      cache.pipeline(vert_, passthrough_, RhiResourceCache::Blend::PremultipliedOver, layout, rpd);
  QRhiGraphicsPipeline* frag = cache.pipeline(vert_, premultiply_, RhiResourceCache::Blend::None, layout, rpd);
  QRhiGraphicsPipeline* lay = cache.pipeline(vert_, passthrough_, RhiResourceCache::Blend::None, two_tex, rpd);
  RhiResourceCache::SrbLayout both_stages = layout;
  both_stages.paramStages = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
  QRhiGraphicsPipeline* stages = cache.pipeline(vert_, passthrough_, RhiResourceCache::Blend::None, both_stages, rpd);
  // premultiply.frag has no UBO 1, so it is valid with and without one in the layout
  RhiResourceCache::SrbLayout no_ubo = layout;
  no_ubo.paramUbo = false;
  QRhiGraphicsPipeline* frag_no_ubo = cache.pipeline(vert_, premultiply_, RhiResourceCache::Blend::None, no_ubo, rpd);
  QVERIFY(base && blend && frag && lay && stages && frag_no_ubo);
  QVERIFY(base != blend && base != frag && base != lay && blend != frag);
  QVERIFY(stages != base && stages != lay);
  QVERIFY(frag_no_ubo != frag);
  QCOMPARE(cache.stats().pipelinesCreated, quint64(6));
  rt.reset();
  delete rpd;
  delete tex;
}

void TestRhiCache::rpdFormatKeying() {
  RhiResourceCache cache(rhi_.get());
  QRhiTexture *t1, *t2;
  QRhiRenderPassDescriptor *preserve_rpd, *clear_rpd;
  std::unique_ptr<QRhiTextureRenderTarget> rt1(makeTarget(&t1, &preserve_rpd, true));
  std::unique_ptr<QRhiTextureRenderTarget> rt2(makeTarget(&t2, &clear_rpd, false));
  RhiResourceCache::SrbLayout layout;
  QRhiGraphicsPipeline* a = cache.pipeline(vert_, passthrough_, RhiResourceCache::Blend::None, layout, preserve_rpd);
  QRhiGraphicsPipeline* b = cache.pipeline(vert_, passthrough_, RhiResourceCache::Blend::None, layout, clear_rpd);
  QVERIFY(a && b);
  if (preserve_rpd->serializedFormat() == clear_rpd->serializedFormat()) {
    QCOMPARE(a, b);  // GL: serializedFormat() is empty for every rpd
  } else {
    QVERIFY(a != b);  // Vulkan encodes load ops
  }
  rt1.reset();
  rt2.reset();
  delete preserve_rpd;
  delete clear_rpd;
  delete t1;
  delete t2;
}

void TestRhiCache::pipelineOutlivesCallerRpd() {
  RhiResourceCache cache(rhi_.get());
  RhiResourceCache::SrbLayout layout;
  QRhiGraphicsPipeline* pipeline = nullptr;
  {
    QRhiTexture* tex;
    QRhiRenderPassDescriptor* rpd;
    std::unique_ptr<QRhiTextureRenderTarget> rt(makeTarget(&tex, &rpd, true));
    pipeline = cache.pipeline(vert_, passthrough_, RhiResourceCache::Blend::None, layout, rpd);
    QVERIFY(pipeline);
    rt.reset();
    delete rpd;  // the caller's rpd dies, like a closed clip's ClipRhiResources
    delete tex;
  }
  // Draw with the cached pipeline into a brand-new compatible target with a fresh SRB.
  QRhiTexture* tex;
  QRhiRenderPassDescriptor* rpd;
  std::unique_ptr<QRhiTextureRenderTarget> rt(makeTarget(&tex, &rpd, true));
  QCOMPARE(cache.pipeline(vert_, passthrough_, RhiResourceCache::Blend::None, layout, rpd), pipeline);

  std::unique_ptr<QRhiTexture> src(rhi_->newTexture(QRhiTexture::RGBA8, QSize(16, 16)));
  src->create();
  std::unique_ptr<QRhiSampler> sampler(rhi_->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
  sampler->create();
  QRhiCommandBuffer* cb = nullptr;
  QCOMPARE(rhi_->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
  QRhiBuffer* vbuf = cache.dynamicBuffer(QRhiBuffer::VertexBuffer, 64);
  QRhiBuffer* vubo = cache.dynamicBuffer(QRhiBuffer::UniformBuffer, 64);
  QRhiBuffer* fubo = cache.dynamicBuffer(QRhiBuffer::UniformBuffer, 16);
  std::unique_ptr<QRhiShaderResourceBindings> srb(rhi_->newShaderResourceBindings());
  srb->setBindings({
      QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, vubo),
      QRhiShaderResourceBinding::uniformBuffer(1, QRhiShaderResourceBinding::FragmentStage, fubo),
      QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, src.get(), sampler.get()),
  });
  QVERIFY(srb->create());
  float quad[] = {-1, -1, 0, 0, -1, 1, 0, 1, 1, -1, 1, 0, 1, 1, 1, 1};
  QMatrix4x4 mvp;
  float mult[4] = {1, 1, 1, 1};
  QRhiResourceUpdateBatch* u = rhi_->nextResourceUpdateBatch();
  u->updateDynamicBuffer(vbuf, 0, sizeof(quad), quad);
  u->updateDynamicBuffer(vubo, 0, 64, mvp.constData());
  u->updateDynamicBuffer(fubo, 0, 16, mult);
  cb->beginPass(rt.get(), QColor(0, 0, 0, 0), {1.0f, 0}, u);
  cb->setGraphicsPipeline(pipeline);
  cb->setViewport({0, 0, 16, 16});
  cb->setShaderResources(srb.get());
  const QRhiCommandBuffer::VertexInput vin(vbuf, 0);
  cb->setVertexInput(0, 1, &vin);
  cb->draw(4);
  cb->endPass();
  QCOMPARE(rhi_->endOffscreenFrame(), QRhi::FrameOpSuccess);
  cache.endFrame();
  rt.reset();
  delete rpd;
  delete tex;
}

void TestRhiCache::buffersDistinctWithinFrameReusedAcrossFrames() {
  RhiResourceCache cache(rhi_.get());
  QRhiBuffer* a1 = cache.dynamicBuffer(QRhiBuffer::UniformBuffer, 64);
  QRhiBuffer* a2 = cache.dynamicBuffer(QRhiBuffer::UniformBuffer, 64);
  QRhiBuffer* a3 = cache.dynamicBuffer(QRhiBuffer::UniformBuffer, 200);  // same 256 bucket
  QVERIFY(a1 && a2 && a3);
  QVERIFY(a1 != a2 && a2 != a3 && a1 != a3);
  QVERIFY(a3->size() >= 200);
  QCOMPARE(cache.stats().buffersCreated, quint64(3));
  cache.endFrame();
  QCOMPARE(cache.dynamicBuffer(QRhiBuffer::UniformBuffer, 64), a1);
  QCOMPARE(cache.dynamicBuffer(QRhiBuffer::UniformBuffer, 64), a2);
  QCOMPARE(cache.dynamicBuffer(QRhiBuffer::UniformBuffer, 16), a3);
  QCOMPARE(cache.stats().buffersCreated, quint64(3));
  QRhiBuffer* big = cache.dynamicBuffer(QRhiBuffer::UniformBuffer, 300);  // next bucket
  QVERIFY(big != a1 && big != a2 && big != a3);
  QVERIFY(big->size() >= 300);
}

void TestRhiCache::bufferPoolsSeparateUsage() {
  RhiResourceCache cache(rhi_.get());
  QRhiBuffer* v = cache.dynamicBuffer(QRhiBuffer::VertexBuffer, 64);
  cache.endFrame();
  QRhiBuffer* u = cache.dynamicBuffer(QRhiBuffer::UniformBuffer, 64);
  QVERIFY(u != v);
  QVERIFY(u->usage() & QRhiBuffer::UniformBuffer);
  QVERIFY(v->usage() & QRhiBuffer::VertexBuffer);
}

void TestRhiCache::failedPipelineCachedOnce() {
  RhiResourceCache cache(rhi_.get());
  QRhiTexture* tex;
  QRhiRenderPassDescriptor* rpd;
  std::unique_ptr<QRhiTextureRenderTarget> rt(makeTarget(&tex, &rpd, true));
  RhiResourceCache::SrbLayout layout;
  // An empty fragment shader can't produce a pipeline on any backend. The first lookup warns (QRhi, then the
  // cache); the second must be silent.
  QTest::ignoreMessage(QtWarningMsg, "Empty shader passed to graphics pipeline");
  QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^\\[RHI-PIPELINE\\] pipeline creation FAILED"));
  QCOMPARE(cache.pipeline(vert_, QShader(), RhiResourceCache::Blend::None, layout, rpd),
           static_cast<QRhiGraphicsPipeline*>(nullptr));
  QTest::failOnWarning();
  QCOMPARE(cache.pipeline(vert_, QShader(), RhiResourceCache::Blend::None, layout, rpd),
           static_cast<QRhiGraphicsPipeline*>(nullptr));
  QCOMPARE(cache.stats().pipelinesCreated, quint64(1));
  QCOMPARE(cache.stats().livePipelines, 0);
  rt.reset();
  delete rpd;
  delete tex;
}

void TestRhiCache::evictionDestroysUnusedEntries() {
  RhiResourceCache cache(rhi_.get(), /*evict_after_frames=*/3);
  QRhiTexture* tex;
  QRhiRenderPassDescriptor* rpd;
  std::unique_ptr<QRhiTextureRenderTarget> rt(makeTarget(&tex, &rpd, true));
  RhiResourceCache::SrbLayout layout;
  // The stale pipeline/buffer are used in frame 0 only; the hot ones in every frame (a vertex buffer, so it
  // sits in its own pool and the stale uniform buffer is a pool tail).
  QRhiGraphicsPipeline* stale = cache.pipeline(vert_, passthrough_, RhiResourceCache::Blend::None, layout, rpd);
  QVERIFY(stale);
  QVERIFY(cache.dynamicBuffer(QRhiBuffer::UniformBuffer, 64));
  auto use_hot = [&] {
    QRhiGraphicsPipeline* p = cache.pipeline(vert_, premultiply_, RhiResourceCache::Blend::None, layout, rpd);
    QRhiBuffer* b = cache.dynamicBuffer(QRhiBuffer::VertexBuffer, 64);
    return p && b;
  };
  QVERIFY(use_hot());
  QCOMPARE(cache.stats().livePipelines, 2);
  QCOMPARE(cache.stats().liveBuffers, 2);
  // Evicted when frame_ - lastUsedFrame > 3: live at frames 1..3, gone at frame 4
  for (int i = 0; i < 3; i++) {
    cache.endFrame();
    QVERIFY(use_hot());
  }
  QCOMPARE(cache.stats().livePipelines, 2);  // 3 frames unused: still within the window
  QCOMPARE(cache.stats().liveBuffers, 2);
  cache.endFrame();
  QCOMPARE(cache.stats().livePipelines, 1);  // the 4th frame unused evicts the stale entries
  QCOMPARE(cache.stats().liveBuffers, 1);
  for (int i = 0; i < 10; i++) {
    QVERIFY(use_hot());
    cache.endFrame();
  }
  QCOMPARE(cache.stats().livePipelines, 1);  // used every frame: never evicted, never recreated
  QCOMPARE(cache.stats().liveBuffers, 1);
  QCOMPARE(cache.stats().pipelinesCreated, quint64(2));
  QCOMPARE(cache.stats().buffersCreated, quint64(2));
  // An evicted key is a miss: it is created again
  QVERIFY(cache.pipeline(vert_, passthrough_, RhiResourceCache::Blend::None, layout, rpd));
  QCOMPARE(cache.stats().pipelinesCreated, quint64(3));
  rt.reset();
  delete rpd;
  delete tex;
}

int main(int argc, char* argv[]) {
  // GL 3.2 core so the offscreen context exposes GLSL 150, which the core .qsb files are baked for (same as
  // test_effects_gpu.cpp); Qt's offscreen platform otherwise hands out GL 2.1 and every pipeline fails.
  QSurfaceFormat fmt;
  fmt.setVersion(3, 2);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setRenderableType(QSurfaceFormat::OpenGL);
  QSurfaceFormat::setDefaultFormat(fmt);

  if (qEnvironmentVariable("AMBER_TEST_RHI") != QLatin1String("vulkan")) qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  TestRhiCache test;
  return QTest::qExec(&test, argc, argv);
}
#include "test_rhi_cache.moc"
