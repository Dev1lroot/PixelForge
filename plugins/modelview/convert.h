#pragma once
// Converter middleware: formats without a built-in loader are turned into a binary glTF (.glb) by an
// installed tool (assimp, FBX2glTF or Blender), cached by source path + modification time, and then
// loaded with the glTF loader. Tools are tried in turn until one succeeds.

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QTimer>

namespace ModelView {

struct ConverterTool {
    QString name;                 // "Blender"
    QString program;              // resolved executable
    QStringList suffixes;         // source formats it accepts
};

QVector<ConverterTool> converterTools();                 // the installed ones, in preference order
QStringList convertibleSuffixes();                       // union of their formats
QString conversionCacheFile(const QString &source);      // where the converted .glb of this file version goes

class Conversion : public QObject
{
    Q_OBJECT
public:
    explicit Conversion(QObject *parent = nullptr);
    ~Conversion() override;

    // starts converting `source` (or finishes at once from the cache); emits finished() exactly once
    void start(const QString &source);
    void cancel();
    bool isRunning() const { return m_process != nullptr; }
    QString source() const { return m_source; }
    QString currentTool() const;

    // blocking helper (tests, scripts): returns the .glb path or an empty string
    static QString convert(const QString &source, QString *error = nullptr, int timeoutMs = 180000);

signals:
    void finished(const QString &source, const QString &glb, const QString &error);

private:
    void tryNext();
    void processDone(bool ok);

    QString m_source, m_target, m_tmp;
    QVector<ConverterTool> m_tools;
    int m_next = 0;
    QPointer<QProcess> m_process;
    QTimer m_timeout;
    QStringList m_errors;
};

} // namespace ModelView
