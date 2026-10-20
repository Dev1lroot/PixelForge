#pragma once
// Software rasterizer for the model preview: perspective-correct texturing, z-buffer, optional
// tangent-space normal maps, alpha cutout. Small windows only need a few hundred thousand pixels.

#include "meshio.h"

namespace ModelView {

struct MaterialTextures {
    QImage base;                  // Format_ARGB32 or null (then the material colour is used)
    QImage normal;                // tangent-space, OpenGL convention (green = +V), or null
    QColor color = Qt::white;
    bool hidden = false;          // the material's triangles are not drawn
};

struct RenderParams {
    double yaw = 0.6, pitch = 0.35;       // camera orbit (radians)
    double distance = 3.3;                // in model radii
    double panX = 0, panY = 0;            // in model radii, screen aligned
    bool pixelated = true;
    bool lighting = true;
    bool normalMaps = true;
    bool twoSided = true;                 // otherwise back faces are culled
    bool wireframe = false;
};

void renderModel(const Model &model, const QVector<MaterialTextures> &textures, const RenderParams &rp, QImage &out);

} // namespace ModelView
