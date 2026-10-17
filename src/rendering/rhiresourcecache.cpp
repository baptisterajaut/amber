#include "rendering/rhiresourcecache.h"

#include <QDebug>

namespace {
constexpr quint32 kBucket = 256;
quint32 bucketSize(quint32 size) { return ((qMax<quint32>(size, 1) + kBucket - 1) / kBucket) * kBucket; }
}  // namespace

size_t qHash(const RhiResourceCache::PipelineKey& k, size_t seed) noexcept {
  return qHashMulti(seed, k.vert, k.frag, int(k.blend), k.paramUbo, k.paramStages, k.textures, k.rpdFormat);
}

RhiResourceCache::RhiResourceCache(QRhi* rhi, int evict_after_frames)
    : rhi_(rhi), evict_after_frames_(evict_after_frames) {}

RhiResourceCache::~RhiResourceCache() {
  // Pipelines first: they hold raw pointers to the layout SRBs and owned rpds below.
  for (PipelineEntry& e : pipelines_) {
    delete e.pipeline;
    delete e.layoutSrb;
  }
  pipelines_.clear();
  qDeleteAll(rpds_);
  rpds_.clear();
  for (Pool& p : pools_) {
    for (PooledBuffer& b : p.buffers) delete b.buffer;
  }
  pools_.clear();
}

QRhiRenderPassDescriptor* RhiResourceCache::ownedRpd(QRhiRenderPassDescriptor* rpd, const QVector<quint32>& format) {
  auto it = rpds_.find(format);
  if (it != rpds_.end()) return it.value();
  QRhiRenderPassDescriptor* owned = rpd->newCompatibleRenderPassDescriptor();
  rpds_.insert(format, owned);
  return owned;
}

QRhiGraphicsPipeline* RhiResourceCache::pipeline(const QShader& vert, const QShader& frag, Blend blend,
                                                 const SrbLayout& layout, QRhiRenderPassDescriptor* rpd) {
  const QVector<quint32> format = rpd->serializedFormat();
  PipelineKey key{vert, frag, blend, layout.paramUbo, int(layout.paramStages), layout.textures, format};
  auto it = pipelines_.find(key);
  if (it != pipelines_.end()) {
    it->lastUsedFrame = frame_;
    return it->pipeline;
  }

  // Layout-only SRB: null resources are allowed for pipeline creation (QRhiGraphicsPipeline docs)
  QVector<QRhiShaderResourceBinding> bindings;
  bindings.append(QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, nullptr));
  if (layout.paramUbo) {
    bindings.append(QRhiShaderResourceBinding::uniformBuffer(1, layout.paramStages, nullptr));
  }
  for (int i = 0; i < layout.textures; i++) {
    bindings.append(
        QRhiShaderResourceBinding::sampledTexture(2 + i, QRhiShaderResourceBinding::FragmentStage, nullptr, nullptr));
  }
  PipelineEntry entry;
  entry.lastUsedFrame = frame_;
  entry.layoutSrb = rhi_->newShaderResourceBindings();
  entry.layoutSrb->setBindings(bindings.cbegin(), bindings.cend());
  stats_.pipelinesCreated++;
  if (!entry.layoutSrb->create()) {
    // Cached as a failure, like a failed pipeline below
    qWarning().nospace() << "[RHI-PIPELINE] layout SRB creation FAILED (frag hash " << Qt::hex << qHash(frag) << Qt::dec
                         << ", paramUbo " << layout.paramUbo << ", textures " << layout.textures << ")";
    delete entry.layoutSrb;
    entry.layoutSrb = nullptr;
    pipelines_.insert(key, entry);
    return nullptr;
  }

  QRhiGraphicsPipeline* p = rhi_->newGraphicsPipeline();
  p->setShaderStages({{QRhiShaderStage::Vertex, vert}, {QRhiShaderStage::Fragment, frag}});
  QRhiVertexInputLayout inputLayout;
  inputLayout.setBindings({{4 * sizeof(float)}});
  inputLayout.setAttributes({
      {0, 0, QRhiVertexInputAttribute::Float2, 0},
      {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
  });
  p->setVertexInputLayout(inputLayout);
  p->setTopology(QRhiGraphicsPipeline::TriangleStrip);
  QRhiGraphicsPipeline::TargetBlend tb;
  if (blend == Blend::PremultipliedOver) {
    // SrcOver for premultiplied alpha: result = src + dst * (1 - srcAlpha)
    tb.enable = true;
    tb.srcColor = QRhiGraphicsPipeline::One;
    tb.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    tb.srcAlpha = QRhiGraphicsPipeline::One;
    tb.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
  } else {
    tb.enable = false;
  }
  p->setTargetBlends({tb});
  p->setShaderResourceBindings(entry.layoutSrb);
  p->setRenderPassDescriptor(ownedRpd(rpd, format));
  if (p->create()) {
    entry.pipeline = p;
  } else {
    // Cached as a failure so a broken shader isn't re-created (and re-warned) every frame
    qWarning().nospace() << "[RHI-PIPELINE] pipeline creation FAILED (frag hash " << Qt::hex << qHash(frag) << Qt::dec
                         << ", blend " << int(blend) << ", textures " << layout.textures << ")";
    delete p;
  }
  pipelines_.insert(key, entry);
  return entry.pipeline;
}

QRhiBuffer* RhiResourceCache::dynamicBuffer(QRhiBuffer::UsageFlags usage, quint32 size) {
  const quint32 bucket = bucketSize(size);
  Pool& pool = pools_[(quint64(usage.toInt()) << 32) | bucket];
  if (pool.cursor < pool.buffers.size()) {
    PooledBuffer& b = pool.buffers[pool.cursor++];
    b.lastUsedFrame = frame_;
    return b.buffer;
  }
  QRhiBuffer* buf = rhi_->newBuffer(QRhiBuffer::Dynamic, usage, bucket);
  // CBA: a failed create() is warned and the buffer still handed out, as before the cache: callers update it
  // without a null check. Return null (and guard every caller) if this ever fires.
  if (!buf->create()) qWarning() << "[RHI-BUFFER] dynamic buffer creation FAILED, size" << bucket;
  stats_.buffersCreated++;
  pool.buffers.append({buf, frame_});
  pool.cursor = pool.buffers.size();
  return buf;
}

void RhiResourceCache::endFrame() {
  frame_++;
  const quint64 limit = quint64(evict_after_frames_);
  for (auto it = pipelines_.begin(); it != pipelines_.end();) {
    if (frame_ - it->lastUsedFrame > limit) {
      delete it->pipeline;
      delete it->layoutSrb;
      it = pipelines_.erase(it);
    } else {
      ++it;
    }
  }
  for (Pool& pool : pools_) {
    pool.cursor = 0;
    // Unused buffers sit at the tail: a frame always hands out buffers[0..n) in order
    while (!pool.buffers.isEmpty() && frame_ - pool.buffers.last().lastUsedFrame > limit) {
      delete pool.buffers.last().buffer;
      pool.buffers.removeLast();
    }
  }
  // CBA: owned rpds (one per serialized format, a handful at most) are kept until destruction.
}

RhiResourceCache::Stats RhiResourceCache::stats() const {
  Stats s = stats_;
  s.livePipelines = 0;
  for (const PipelineEntry& e : pipelines_) {
    if (e.pipeline) s.livePipelines++;
  }
  s.liveBuffers = 0;
  for (const Pool& p : pools_) s.liveBuffers += p.buffers.size();
  return s;
}
