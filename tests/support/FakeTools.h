#ifndef FAKE_TOOLS_H
#define FAKE_TOOLS_H

#include <QFile>
#include <QTemporaryDir>

// Подменяет внешние программы (yt-dlp) shell-скриптами: тест не ходит
// в сеть и не зависит от того, что установлено на машине
class FakeTools {
public:
    FakeTools() { qputenv("PATH", m_dir.path().toUtf8() + ":" + qgetenv("PATH")); }

    void add(const QString& name, const QByteArray& script) {
        QFile file(m_dir.filePath(name));
        if (!file.open(QIODevice::WriteOnly)) qFatal("FakeTools: cannot write %s", qPrintable(file.fileName()));
        file.write("#!/bin/sh\n" + script);
        file.close();
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
    }

    QString path(const QString& name) const { return m_dir.filePath(name); }

private:
    QTemporaryDir m_dir;
};

#endif
