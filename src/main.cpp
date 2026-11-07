#include <QApplication>
#include <QFontDatabase>
#include <QLockFile>
#include <QDir>
#include <QImageReader>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QThread>
#ifdef Q_OS_UNIX
#include <QSocketNotifier>
#include <csignal>
#include <unistd.h>
#endif
#include "core/Application.h"
#include "core/MorphCore.h"
#include "server/PhoneServer.h"
#include "utils/PathProvider.h"

// Замок и сокет лежат в папке данных, а не во временной: у launchd и у окна
// временные папки бывают разные, и тогда они не увидели бы друг друга
static QString lockPath() {
    QDir().mkpath(PathProvider::getDataPath());
    return PathProvider::getDataPath() + "/morph.lock";
}

// через этот сокет окно просит morph --server освободить данные
static QString serverSocket() {
    return PathProvider::getDataPath() + "/server.sock";
}

#ifdef Q_OS_UNIX
static int g_signalPipe[2] = {-1, -1};

// launchd останавливает сервер сигналом: выходим через цикл событий, чтобы
// настройки и библиотека успели дописаться на диск
static void quitOnSignals(QCoreApplication& app) {
    if (::pipe(g_signalPipe) != 0) return;
    auto* notifier = new QSocketNotifier(g_signalPipe[0], QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app, &QCoreApplication::quit);
    auto handler = [](int) {
        char byte = 1;
        [[maybe_unused]] auto written = ::write(g_signalPipe[1], &byte, 1);
    };
    std::signal(SIGTERM, handler);
    std::signal(SIGINT, handler);
}
#endif

// morph --server: только сервер для телефона, без окна, например из launchd
// при входе в систему. Данными владеет кто-то один, поэтому сервер ждёт, пока
// открыто окно, а окно при запуске просит сервер уйти
static int runServer(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QLockFile lockFile(lockPath());
    while (!lockFile.tryLock(100)) QThread::sleep(15);
#ifdef Q_OS_UNIX
    quitOnSignals(app);
#endif

    QLocalServer::removeServer(serverSocket());
    QLocalServer handoff;
    QObject::connect(&handoff, &QLocalServer::newConnection, &app, [&handoff]() {
        while (QLocalSocket* socket = handoff.nextPendingConnection()) socket->deleteLater();
        QCoreApplication::quit();
    });
    handoff.listen(serverSocket());

    MorphCore core;
    // включён и в настройках: окно, которое заберёт данные, продолжит раздавать
    core.phone()->setEnabled(true);
    qInfo().noquote() << "morph server:" << core.phone()->address() << "user" << core.phone()->user();
    if (!core.phone()->error().isEmpty()) qWarning().noquote() << core.phone()->error();
    return app.exec();
}

int main(int argc, char *argv[]) {
#ifdef Q_OS_MACOS
    // .app из Finder стартует с урезанным PATH, без brew-папок не найдутся
    // yt-dlp, ffprobe и deno, без которого yt-dlp больше не ходит в ютуб
    qputenv("PATH", "/opt/homebrew/bin:/usr/local/bin:" + qgetenv("PATH"));
#endif
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--server") == 0) return runServer(argc, argv);
    }
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QImageReader::setAllocationLimit(64);
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/assets/logo.svg"));

    QLockFile lockFile(lockPath());
    if (!lockFile.tryLock(100)) {
        // данные держит morph --server: он допишет их на диск и уйдёт
        QLocalSocket server;
        server.connectToServer(serverSocket());
        if (!server.waitForConnected(1000) || !lockFile.tryLock(10000)) return 0;
    }
    
    int fontId = QFontDatabase::addApplicationFont(":/assets/fonts/interblack.otf");
    if (fontId != -1) {
        QString family = QFontDatabase::applicationFontFamilies(fontId).at(0);
        app.setFont(QFont(family));
    }
    
    Application morph;
    morph.start();
    
    return app.exec();
}
