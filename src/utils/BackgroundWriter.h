#ifndef BACKGROUND_WRITER_H
#define BACKGROUND_WRITER_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>

// Файлы данных пишутся в одном фоновом потоке, по очереди: окно не ждёт
// диск, а две записи одного файла не перемешаются. Если в очереди несколько
// версий одного файла, пишется только последняя. Запись идёт во временный
// файл, который потом подменяет старый, так что при падении остаётся целая
// прошлая версия
namespace BackgroundWriter {
void write(const QString& path, const QByteArray& data);
// сериализация тоже в фоне: копия QJsonObject дешёвая, данные общие до правки
void write(const QString& path, const QJsonObject& json);
// дождаться всех записей: при выходе и перед тем, как читать файл заново
void waitForAll();
} // namespace BackgroundWriter

#endif
