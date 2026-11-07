#include "PinterestCovers.h"
#include "../utils/BackgroundWriter.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

// больше с одной доски не нужно, а листать её всю долго
static const int kMaxPins = 500;

// «Morph Covers», «morph-covers» и «morph_covers» это одна доска
static QString boardName(const QString& text) {
    return QString(text).toLower().replace('-', ' ').replace('_', ' ').simplified();
}

PinterestCovers::PinterestCovers(NetworkManager* network, const QString& statePath, QObject* parent)
    : QObject(parent), net(network), m_api(qEnvironmentVariable("MORPH_PINTEREST_API", "https://api.pinterest.com/v5")),
      m_statePath(statePath), m_random(QRandomGenerator::securelySeeded()) {
    load();
}

void PinterestCovers::setAccount(const QString& token, const QString& board) {
    QString cleanToken = token.trimmed();
    if (cleanToken.startsWith("Bearer ")) cleanToken = cleanToken.mid(7).trimmed();
    if (cleanToken == m_token && board.trimmed() == m_board) return;

    m_token = cleanToken;
    m_board = board.trimmed();
    ++m_generation;
    m_images.clear();
    if (!m_token.isEmpty() && !m_board.isEmpty()) findBoard();
}

QString PinterestCovers::next(const QString& key) {
    if (m_images.isEmpty()) return {};

    QStringList fresh;
    for (const QString& image : std::as_const(m_images)) {
        if (!m_used.contains(image)) fresh.append(image);
    }
    // доска кончилась: новый круг, но не с той картинкой, что сейчас у плейлиста
    if (fresh.isEmpty()) {
        m_used.clear();
        fresh = m_images;
        if (fresh.size() > 1) fresh.removeAll(m_assigned.value(key));
    }

    QString pick = fresh[m_random.bounded(int(fresh.size()))];
    m_used.insert(pick);
    m_assigned.insert(key, pick);
    save();
    return pick;
}

// В настройке может быть id доски, ссылка на неё или имя. Id идёт сразу,
// остальное ищется среди твоих досок по имени
void PinterestCovers::findBoard() {
    static const QRegularExpression digits("^\\d+$");
    if (digits.match(m_board).hasMatch()) {
        fetchPins(m_board, {}, {});
        return;
    }

    QString wanted = m_board;
    // https://www.pinterest.com/melidadr/morph-covers/: имя доски вторым в пути
    if (wanted.contains("pinterest.")) wanted = QUrl(wanted).path().split('/', Qt::SkipEmptyParts).value(1);
    wanted = boardName(wanted);

    QUrl url(m_api + "/boards");
    url.setQuery("page_size=100");
    int generation = m_generation;
    net->get(
        url, headers(),
        [this, wanted, generation](QNetworkReply* reply) {
            if (generation != m_generation) return;
            QJsonArray boards = QJsonDocument::fromJson(reply->readAll()).object()["items"].toArray();
            if (reply->error() != QNetworkReply::NoError) {
                emit failed("Pinterest: the token was rejected or has no access to boards");
                return;
            }
            for (const auto& b : boards) {
                QJsonObject board = b.toObject();
                if (boardName(board["name"].toString()) == wanted) {
                    fetchPins(board["id"].toString(), {}, {});
                    return;
                }
            }
            emit failed("Pinterest: board not found: " + m_board);
        },
        false);
}

void PinterestCovers::fetchPins(const QString& boardId, const QString& bookmark, QStringList collected) {
    QUrl url(QString("%1/boards/%2/pins").arg(m_api, boardId));
    QUrlQuery q;
    q.addQueryItem("page_size", "100");
    if (!bookmark.isEmpty()) q.addQueryItem("bookmark", bookmark);
    url.setQuery(q);

    int generation = m_generation;
    net->get(
        url, headers(),
        [this, boardId, collected = std::move(collected), generation](QNetworkReply* reply) mutable {
            if (generation != m_generation) return;
            QJsonObject page = QJsonDocument::fromJson(reply->readAll()).object();
            bool ok = reply->error() == QNetworkReply::NoError;
            if (ok) {
                for (const auto& pin : page["items"].toArray()) {
                    QString image = imageOf(pin.toObject());
                    if (!image.isEmpty() && !collected.contains(image)) collected.append(image);
                }
                QString nextPage = page["bookmark"].toString();
                if (!nextPage.isEmpty() && collected.size() < kMaxPins) {
                    fetchPins(boardId, nextPage, collected);
                    return;
                }
            }
            if (collected.isEmpty()) {
                emit failed(ok ? "Pinterest: the board has no pictures" : "Pinterest: could not read the board");
                return;
            }
            m_images = collected;
            emit loaded(m_images.size());
        },
        false);
}

// самая крупная готовая нарезка, но не оригинал: он бывает в десятки мегабайт
QString PinterestCovers::imageOf(const QJsonObject& pin) {
    QJsonObject images = pin["media"].toObject()["images"].toObject();
    for (const char* size : {"1200x", "600x", "400x300", "150x150"}) {
        QString url = images[size].toObject()["url"].toString();
        if (!url.isEmpty()) return url;
    }
    return {};
}

QMap<QString, QByteArray> PinterestCovers::headers() const {
    return {{"Authorization", "Bearer " + m_token.toUtf8()}, {"Accept", "application/json"}};
}

void PinterestCovers::load() {
    BackgroundWriter::waitForAll();
    QFile file(m_statePath);
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonObject saved = QJsonDocument::fromJson(file.readAll()).object();
    for (const auto& url : saved["used"].toArray()) m_used.insert(url.toString());
    QJsonObject assigned = saved["assigned"].toObject();
    for (auto it = assigned.begin(); it != assigned.end(); ++it) m_assigned.insert(it.key(), it.value().toString());
}

void PinterestCovers::save() const {
    QJsonArray used;
    for (const QString& url : m_used) used.append(url);
    QJsonObject assigned;
    for (auto it = m_assigned.cbegin(); it != m_assigned.cend(); ++it) assigned.insert(it.key(), it.value());
    BackgroundWriter::write(m_statePath, QJsonObject{{"used", used}, {"assigned", assigned}});
}
