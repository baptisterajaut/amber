#ifndef QUADBUFFER_H
#define QUADBUFFER_H

#include <QOpenGLFunctions>

// Stateless quad drawing utility. Each draw() call creates transient GL objects
// (VAO + VBO) to avoid shared-state races between the render thread and UI thread.
class QuadBuffer {
public:
  // Draw a textured quad. Caller must have bound shader and textures.
  // coords: [x0,y0, x1,y1, x2,y2, x3,y3] for vertex positions (8 floats)
  // texcoords: [s0,t0, s1,t1, s2,t2, s3,t3] for UVs (8 floats)
  // If nullptr, uses fullscreen defaults: positions [-1,-1 to 1,1], UVs [0,0 to 1,1]
  static void draw(QOpenGLFunctions* f,
                   const float* coords = nullptr,
                   const float* texcoords = nullptr);
};

#endif // QUADBUFFER_H
