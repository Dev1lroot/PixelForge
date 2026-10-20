#pragma once
// Minimal loaders for the 3D model preview: Wavefront OBJ (+MTL), glTF 2.0 (.gltf / .glb), FBX (binary 7.x,
// ASCII 6/7), STL and PLY. Other formats go through an external converter (see convert.h).
// Everything is flattened into one triangle list grouped by material.

#include <QColor>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector2D>
#include <QVector3D>

namespace ModelView {

struct Vertex {
    QVector3D pos, normal;
    float u = 0, v = 0;           // the selected UV map; OpenGL convention: v = 0 is the bottom row of the texture
};

struct Material {
    QString name;
    QColor color = Qt::white;     // base colour factor (used when no texture is assigned)
    QImage texture;               // the model's own base colour / normal textures, if any
    QImage normalTexture;
};

struct Part {
    int material = 0;
    int first = 0;                // range in Model::indices
    int count = 0;
};

struct Model {
    QString file;
    QVector<Vertex> vertices;
    QVector<quint32> indices;     // triangles
    QVector<Part> parts;          // one per used material
    QVector<Material> materials;
    bool hasUV = false;
    QStringList uvSets;           // names of the UV maps (several for lightmaps, detail maps...)
    QVector<QVector<QVector2D>> uvData;   // per UV map, per vertex
    int uvSet = 0;                // the one copied into Vertex::u/v
    QVector3D center;             // bounding box centre and radius of the bounding sphere around it
    float radius = 1;

    bool isEmpty() const { return indices.isEmpty(); }
    int triangleCount() const { return int(indices.size() / 3); }
    void selectUVSet(int set);
};

QStringList modelSuffixes();                  // "obj", "gltf", ...
QString modelFileFilter();                    // for QFileDialog
Model loadModel(const QString &path, QString *error = nullptr);

} // namespace ModelView
