#include "convert.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace ModelView {

// Imports anything Blender can read (by extension, using whichever importer this Blender version has)
// and exports a GLB with embedded textures. Arguments after "--": source, target.
static const char *kBlenderScript = R"PY(
import bpy, sys, os
src, out = sys.argv[sys.argv.index("--") + 1:][:2]
ext = os.path.splitext(src)[1].lower()
importers = {
    ".fbx": ["import_scene.fbx"], ".dae": ["wm.collada_import"], ".abc": ["wm.alembic_import"],
    ".usd": ["wm.usd_import"], ".usda": ["wm.usd_import"], ".usdc": ["wm.usd_import"], ".usdz": ["wm.usd_import"],
    ".3ds": ["import_scene.max3ds", "import_scene.autodesk_3ds"], ".x3d": ["import_scene.x3d"], ".wrl": ["import_scene.x3d"],
    ".dxf": ["import_scene.dxf"], ".obj": ["wm.obj_import"], ".ply": ["wm.ply_import"], ".stl": ["wm.stl_import"],
    ".gltf": ["import_scene.gltf"], ".glb": ["import_scene.gltf"],
}
if ext != ".blend":
    bpy.ops.wm.read_homefile(use_empty=True)
    for name in importers.get(ext, []):
        mod, op = name.split(".")
        fn = getattr(getattr(bpy.ops, mod), op)
        try:
            fn.get_rna_type()
        except Exception:
            continue
        fn(filepath=src)
        break
    else:
        sys.stderr.write("PIXELFORGE: this Blender has no importer for %s\n" % ext)
        sys.exit(2)
if not any(o.type == "MESH" for o in bpy.context.scene.objects):
    sys.stderr.write("PIXELFORGE: no meshes in the scene\n")
    sys.exit(3)
bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_apply=True, export_yup=True)
)PY";

QVector<ConverterTool> converterTools()
{
    QVector<ConverterTool> tools;
    auto find = [](const QString &envVar, const QStringList &names) {
        const QString env = qEnvironmentVariable(envVar.toLatin1().constData());
        if (!env.isEmpty() && QFileInfo(env).isExecutable())
            return env;
        for (const QString &n : names) {
            const QString p = QStandardPaths::findExecutable(n);
            if (!p.isEmpty())
                return p;
        }
        return QString();
    };
    // assimp reads the most formats and is fast; FBX2glTF is FBX only; Blender is slow to start but thorough
    const QString assimp = find("PIXELFORGE_ASSIMP", {"assimp"});
    if (!assimp.isEmpty())
        tools.append({"assimp", assimp,
                      {"fbx", "dae", "3ds", "ase", "x", "ms3d", "lwo", "lws", "md2", "md3", "md5mesh", "mdl", "off", "smd", "ac",
                       "ifc", "irr", "irrmesh", "ogex", "3mf", "b3d", "cob", "q3o", "q3s", "x3d", "dxf", "nff", "raw", "xgl",
                       "zgl", "amf", "3d", "hmp", "ter", "csm", "blend", "usd", "usda", "usdc", "usdz"}});
    const QString fbx2gltf = find("PIXELFORGE_FBX2GLTF", {"FBX2glTF", "fbx2gltf"});
    if (!fbx2gltf.isEmpty())
        tools.append({"FBX2glTF", fbx2gltf, {"fbx"}});
    const QString blender = find("PIXELFORGE_BLENDER", {"blender"});
    if (!blender.isEmpty())
        tools.append({"Blender", blender, {"blend", "fbx", "dae", "abc", "usd", "usda", "usdc", "usdz", "3ds", "x3d", "wrl", "dxf"}});
    return tools;
}

QStringList convertibleSuffixes()
{
    QStringList out;
    for (const ConverterTool &t : converterTools())
        for (const QString &s : t.suffixes)
            if (!out.contains(s))
                out << s;
    return out;
}

QString conversionCacheFile(const QString &source)
{
    const QFileInfo fi(source);
    const QByteArray id = (fi.absoluteFilePath() + '|' + QString::number(fi.lastModified().toMSecsSinceEpoch()) + '|' +
                           QString::number(fi.size())).toUtf8();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/pixelforge/modelview";
    return dir + "/" + fi.completeBaseName() + "-" + QCryptographicHash::hash(id, QCryptographicHash::Sha1).toHex().left(16) + ".glb";
}

// keeps the cache from growing forever: newest 32 conversions stay
static void pruneCache(const QString &dir)
{
    const QFileInfoList files = QDir(dir).entryInfoList({"*.glb"}, QDir::Files, QDir::Time);
    for (int i = 32; i < files.size(); ++i)
        QFile::remove(files[i].absoluteFilePath());
}

Conversion::Conversion(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(180000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_process) {
            m_errors << tr("%1 timed out").arg(currentTool());
            m_process->kill();
        }
    });
}

Conversion::~Conversion() { cancel(); }

QString Conversion::currentTool() const
{
    return m_next > 0 && m_next <= m_tools.size() ? m_tools[m_next - 1].name : QString();
}

void Conversion::cancel()
{
    if (!m_process)
        return;
    QProcess *p = m_process;
    m_process = nullptr;
    p->disconnect(this);
    p->kill();
    p->waitForFinished(2000);
    p->deleteLater();
    m_timeout.stop();
    QFile::remove(m_tmp);
}

void Conversion::start(const QString &source)
{
    cancel();
    m_source = QFileInfo(source).absoluteFilePath();
    m_target = conversionCacheFile(m_source);
    m_errors.clear();
    m_next = 0;
    if (QFileInfo::exists(m_target)) {
        QFile(m_target).setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);   // recently used
        emit finished(m_source, m_target, QString());
        return;
    }
    QDir().mkpath(QFileInfo(m_target).absolutePath());
    const QString suffix = QFileInfo(m_source).suffix().toLower();
    m_tools.clear();
    for (const ConverterTool &t : converterTools())
        if (t.suffixes.contains(suffix))
            m_tools.append(t);
    if (m_tools.isEmpty()) {
        emit finished(m_source, QString(),
                      tr("No converter for .%1 files is installed. Install Blender or assimp to open them.").arg(suffix));
        return;
    }
    tryNext();
}

void Conversion::tryNext()
{
    if (m_next >= m_tools.size()) {
        emit finished(m_source, QString(), m_errors.join('\n'));
        return;
    }
    const ConverterTool &tool = m_tools[m_next++];
    // write next to the final file and rename on success, so a crash never leaves a broken cache entry
    m_tmp = m_target + ".part.glb";
    QFile::remove(m_tmp);
    QStringList args;
    if (tool.name == "assimp") {
        args << "export" << m_source << m_tmp << "-fglb2";
    } else if (tool.name == "FBX2glTF") {
        args << "--binary" << "--input" << m_source << "--output" << m_tmp.chopped(4);
    } else {
        args << "-b";
        if (m_source.endsWith(".blend", Qt::CaseInsensitive))
            args << m_source;
        args << "--python-exit-code" << "1" << "--python-expr" << kBlenderScript << "--" << m_source << m_tmp;
    }
    auto *p = new QProcess(this);
    p->setProcessChannelMode(QProcess::MergedChannels);
    p->setWorkingDirectory(QFileInfo(m_source).absolutePath());
    m_process = p;
    connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus status) {
        if (p != m_process)
            return;
        const bool ok = status == QProcess::NormalExit && code == 0 && QFileInfo(m_tmp).size() > 0;
        if (!ok) {
            // the last meaningful output lines usually say what went wrong
            QStringList lines = QString::fromLocal8Bit(p->readAll()).split('\n', Qt::SkipEmptyParts);
            QStringList tail = lines.mid(std::max(0, int(lines.size()) - 3));
            for (const QString &l : lines)
                if (l.startsWith("PIXELFORGE: "))
                    tail = {l.mid(12)};
            m_errors << tr("%1 failed: %2").arg(currentTool(), tail.join(' ').trimmed().left(300));
        }
        processDone(ok);
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (p != m_process || e != QProcess::FailedToStart)
            return;
        m_errors << tr("%1 could not be started").arg(currentTool());
        processDone(false);
    });
    m_timeout.start();
    p->start(tool.program, args);
}

void Conversion::processDone(bool ok)
{
    m_timeout.stop();
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (ok) {
        QFile::remove(m_target);
        if (QFile::rename(m_tmp, m_target)) {
            pruneCache(QFileInfo(m_target).absolutePath());
            emit finished(m_source, m_target, QString());
            return;
        }
        m_errors << tr("Could not write %1").arg(m_target);
    }
    QFile::remove(m_tmp);
    tryNext();
}

QString Conversion::convert(const QString &source, QString *error, int timeoutMs)
{
    Conversion c;
    c.m_timeout.setInterval(timeoutMs);
    QString result, err;
    bool done = false;
    QEventLoop loop;
    connect(&c, &Conversion::finished, &loop, [&](const QString &, const QString &glb, const QString &e) {
        result = glb;
        err = e;
        done = true;
        loop.quit();
    });
    c.start(source);
    if (!done)
        loop.exec();
    if (error)
        *error = err;
    return result;
}

} // namespace ModelView
