#ifndef RHIRESOURCECACHE_H
#define RHIRESOURCECACHE_H

#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <QHash>
#include <QVector>

// Per-QRhi cache of graphics pipelines and a per-frame pool of dynamic buffers, so steady-state
// compositing creates neither. One instance per QRhi, used only on that QRhi's render thread (no locks).
// Must be destroyed before the QRhi it was created with: owners reset it before deleting their QRhi.
class RhiResourceCache {
 public:
  enum class Blend { None, PremultipliedOver };

  // Binding shape of every compositing pass: UBO 0 (vertex), optional UBO 1, sampled textures at 2..
  struct SrbLayout {
    bool paramUbo = true;
    QRhiShaderResourceBinding::StageFlags paramStages = QRhiShaderResourceBinding::FragmentStage;
    int textures = 1;  // 1..3
  };

  struct Stats {
    quint64 pipelinesCreated = 0;
    quint64 buffersCreated = 0;
    int livePipelines = 0;
    int liveBuffers = 0;
  };

  explicit RhiResourceCache(QRhi* rhi, int evict_after_frames = 600);
  ~RhiResourceCache();
  RhiResourceCache(const RhiResourceCache&) = delete;
  RhiResourceCache& operator=(const RhiResourceCache&) = delete;

  // Pipeline (2x Float2 vertex input, stride 16, triangle strip) for this shader pair and blend, usable with
  // any render pass compatible with `rpd` and any SRB whose bindings match `layout`. Null if creation failed;
  // the failure is cached and warned once. The pipeline is destroyed once it goes evict_after_frames frames
  // without a lookup: look it up every frame you draw with it, never store it (e.g. in ClipRhiResources).
  QRhiGraphicsPipeline* pipeline(const QShader& vert, const QShader& frag, Blend blend, const SrbLayout& layout,
                                 QRhiRenderPassDescriptor* rpd);

  // Dynamic buffer of at least `size` bytes not handed out yet in the current frame. Pooled by usage and
  // 256-byte bucket, so a vertex buffer and a uniform buffer are never handed out for each other.
  // Valid only for the current frame: after endFrame() it is handed out again or evicted, so never store it.
  QRhiBuffer* dynamicBuffer(QRhiBuffer::UsageFlags usage, quint32 size);

  // Call after endOffscreenFrame(): rewinds the buffer pool and evicts entries unused for evict_after_frames.
  void endFrame();

  Stats stats() const;

 private:
  struct PipelineKey {
    QShader vert;
    QShader frag;
    Blend blend;
    bool paramUbo;
    int paramStages;
    int textures;
    QVector<quint32> rpdFormat;
    bool operator==(const PipelineKey& o) const {
      return blend == o.blend && paramUbo == o.paramUbo && paramStages == o.paramStages && textures == o.textures &&
             rpdFormat == o.rpdFormat && vert == o.vert && frag == o.frag;
    }
  };
  friend size_t qHash(const PipelineKey& k, size_t seed) noexcept;

  struct PipelineEntry {
    QRhiGraphicsPipeline* pipeline = nullptr;  // null = creation failed
    QRhiShaderResourceBindings* layoutSrb = nullptr;
    quint64 lastUsedFrame = 0;
  };

  struct PooledBuffer {
    QRhiBuffer* buffer = nullptr;
    quint64 lastUsedFrame = 0;
  };
  struct Pool {
    QVector<PooledBuffer> buffers;
    int cursor = 0;  // buffers[0..cursor) are handed out this frame
  };

  QRhiRenderPassDescriptor* ownedRpd(QRhiRenderPassDescriptor* rpd, const QVector<quint32>& format);

  QRhi* rhi_;
  int evict_after_frames_;
  quint64 frame_ = 0;
  QHash<PipelineKey, PipelineEntry> pipelines_;
  QHash<QVector<quint32>, QRhiRenderPassDescriptor*> rpds_;  // one owned rpd per serialized format
  QHash<quint64, Pool> pools_;                               // key: (usage << 32) | bucket size
  Stats stats_;
};

#endif  // RHIRESOURCECACHE_H
