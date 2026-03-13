#include "quadbuffer.h"

#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>

// Default fullscreen quad: position (xy) + texcoord (st) interleaved
static const float kDefaultQuad[] = {
  // x,    y,   s,   t
  -1.0f, -1.0f, 0.0f, 0.0f,  // bottom-left
   1.0f, -1.0f, 1.0f, 0.0f,  // bottom-right
   1.0f,  1.0f, 1.0f, 1.0f,  // top-right
  -1.0f,  1.0f, 0.0f, 1.0f,  // top-left
};

void QuadBuffer::draw(QOpenGLFunctions* f, const float* coords, const float* texcoords) {
  auto* ef = QOpenGLContext::currentContext()->extraFunctions();

  // Build interleaved vertex data
  float buf[16];
  if (coords) {
    for (int i = 0; i < 4; i++) {
      buf[i * 4 + 0] = coords[i * 2 + 0];
      buf[i * 4 + 1] = coords[i * 2 + 1];
      buf[i * 4 + 2] = texcoords ? texcoords[i * 2 + 0] : kDefaultQuad[i * 4 + 2];
      buf[i * 4 + 3] = texcoords ? texcoords[i * 2 + 1] : kDefaultQuad[i * 4 + 3];
    }
  } else {
    memcpy(buf, kDefaultQuad, sizeof(kDefaultQuad));
  }

  // Transient VAO + VBO — no shared state between threads
  GLuint vao, vbo;
  ef->glGenVertexArrays(1, &vao);
  ef->glBindVertexArray(vao);

  f->glGenBuffers(1, &vbo);
  f->glBindBuffer(GL_ARRAY_BUFFER, vbo);
  f->glBufferData(GL_ARRAY_BUFFER, sizeof(buf), buf, GL_STREAM_DRAW);

  f->glEnableVertexAttribArray(0);
  f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
  f->glEnableVertexAttribArray(1);
  f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

  f->glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

  ef->glBindVertexArray(0);
  ef->glDeleteVertexArrays(1, &vao);
  f->glDeleteBuffers(1, &vbo);
  f->glBindBuffer(GL_ARRAY_BUFFER, 0);
}
