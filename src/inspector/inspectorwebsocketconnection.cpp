/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "inspectorwebsocketconnection.h"
#include "leakdetector.h"
#include "logger.h"
#include "loghandler.h"
#include "mozillavpn.h"
#include "qmlengineholder.h"

#include <QHostAddress>
#include <QImage>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QStandardPaths>
#include <QWebSocket>
#include <QTest>

namespace {
Logger logger(LOG_INSPECTOR, "InspectorWebSocketConnection");

bool s_stealUrls = false;
QUrl s_lastUrl;
QString s_updateVersion;

}  // namespace

static QQuickItem* findObject(const QString& name) {
  QStringList parts = name.split("/");
  Q_ASSERT(!parts.isEmpty());

  QQuickItem* parent = nullptr;
  QQmlApplicationEngine* engine = QmlEngineHolder::instance()->engine();
  for (QObject* rootObject : engine->rootObjects()) {
    if (!rootObject) {
      continue;
    }

    parent = rootObject->findChild<QQuickItem*>(parts[0]);
    if (parent) {
      break;
    }
  }

  if (!parent || parts.length() == 1) {
    return parent;
  }

  for (int i = 1; i < parts.length(); ++i) {
    QQuickItem* contentItem =
        parent->property("contentItem").value<QQuickItem*>();
    QList<QQuickItem*> contentItemChildren = contentItem->childItems();

    bool found = false;
    for (QQuickItem* item : contentItemChildren) {
      if (item->objectName() == parts[i]) {
        parent = item;
        found = true;
        break;
      }
    }

    if (!found) {
      return nullptr;
    }
  }

  return parent;
}

static bool cmdReset(QWebSocket*, const QList<QByteArray>&) {
  MozillaVPN::instance()->reset(true);
  return true;
}

static bool cmdActivate(QWebSocket*, const QList<QByteArray>&) {
  MozillaVPN::instance()->activate();
  return true;
}

static bool cmdDeactivate(QWebSocket*, const QList<QByteArray>&) {
  MozillaVPN::instance()->deactivate();
  return true;
}

static bool cmdLogout(QWebSocket*, const QList<QByteArray>&) {
  MozillaVPN::instance()->logout();
  return true;
}

static bool cmdQuit(QWebSocket*, const QList<QByteArray>&) {
  MozillaVPN::instance()->controller()->quit();
  return true;
}

static bool cmdHas(QWebSocket*, const QList<QByteArray>& arguments) {
  return !!findObject(arguments[1]);
}

static bool cmdProperty(QWebSocket* socket,
                        const QList<QByteArray>& arguments) {
  QQuickItem* obj = findObject(arguments[1]);
  if (!obj) {
    return false;
  }

  QVariant property = obj->property(arguments[2]);
  if (!property.isValid()) {
    return false;
  }

  socket->sendTextMessage(
      QString("-%1-").arg(property.toString().toHtmlEscaped()).toLocal8Bit());
  return true;
}

static bool cmdClick(QWebSocket*, const QList<QByteArray>& arguments) {
  QQuickItem* obj = findObject(arguments[1]);
  if (!obj) {
    return false;
  }

  QPointF pointF = obj->mapToScene(QPoint(0, 0));
  QPoint point = pointF.toPoint();
  point.rx() += obj->width() / 2;
  point.ry() += obj->height() / 2;
  QTest::mouseClick(obj->window(), Qt::LeftButton, Qt::NoModifier, point);

  return true;
}

static bool cmdStealurls(QWebSocket*, const QList<QByteArray>&) {
  s_stealUrls = true;
  return true;
}

static bool cmdLasturl(QWebSocket* socket, const QList<QByteArray>&) {
  socket->sendTextMessage(
      QString("-%1-").arg(s_lastUrl.toString()).toLocal8Bit());
  return true;
}

static bool cmdForceUpdateCheck(QWebSocket*,
                                const QList<QByteArray>& arguments) {
  s_updateVersion = arguments[1];
  MozillaVPN::instance()->releaseMonitor()->runSoon();
  return true;
}

static bool cmdForceCaptivePortalCheck(QWebSocket*, const QList<QByteArray>&) {
  MozillaVPN::instance()->captivePortalDetection()->detectCaptivePortal();
  return true;
}

static bool cmdForceCaptivePortalDetection(QWebSocket*,
                                           const QList<QByteArray>&) {
  MozillaVPN::instance()->captivePortalDetection()->captivePortalDetected();
  return true;
}

static bool cmdForceUnsecuredNetwork(QWebSocket*, const QList<QByteArray>&) {
  MozillaVPN::instance()->networkWatcher()->unsecuredNetwork("Dummy", "Dummy");
  return true;
}

bool cmdScreenCaptureWriteImage(QWebSocket* socket, const QImage& image,
                                QStandardPaths::StandardLocation location) {
  if (!QFileInfo::exists(QStandardPaths::writableLocation(location))) {
    return false;
  }

  QString filename;
  QDate now = QDate::currentDate();

  QTextStream(&filename) << "mozillavpn-" << now.year() << "-" << now.month()
                         << "-" << now.day() << ".png";

  QDir dir(QStandardPaths::writableLocation(location));
  QString file = dir.filePath(filename);

  if (QFileInfo::exists(file)) {
    logger.log() << file << "exists. Let's try a new filename";

    for (uint32_t i = 1;; ++i) {
      QString filename;
      QTextStream(&filename)
          << "mozillavpn-" << now.year() << "-" << now.month() << "-"
          << now.day() << "_" << i << ".png";
      file = dir.filePath(filename);
      if (!QFileInfo::exists(file)) {
        logger.log() << "Filename found!" << i;
        break;
      }
    }
  }

  if (!image.save(file)) {
    logger.log() << "Unable to save the pixmap in" << file;
    return false;
  }

  socket->sendTextMessage(QString("-%1-").arg(file));
  return true;
}

static bool cmdScreenCapture(QWebSocket* socket, const QList<QByteArray>&) {
  QWindow* window = QmlEngineHolder::instance()->window();
  QQuickWindow* quickWindow = qobject_cast<QQuickWindow*>(window);
  if (!quickWindow) {
    logger.log() << "Unable to identify the quick window";
    return false;
  }

  QImage image = quickWindow->grabWindow();
  if (!image.isNull() || image.width() == 0 || image.height() == 0) {
    logger.log() << "Unable to grab the window";
  }

  logger.log() << "Generated image with width:" << image.width()
               << "height:" << image.height();

  if (cmdScreenCaptureWriteImage(socket, image,
                                 QStandardPaths::DesktopLocation)) {
    return true;
  }

  if (cmdScreenCaptureWriteImage(socket, image, QStandardPaths::HomeLocation)) {
    return true;
  }

  if (cmdScreenCaptureWriteImage(socket, image, QStandardPaths::TempLocation)) {
    return true;
  }

  logger.log()
      << "Unable to write the image: no desktop, no home no temp folder.";
  return false;
}

struct WebSocketCommand {
  QString m_commandName;
  int32_t m_arguments;
  bool (*m_callback)(QWebSocket* webSocket, const QList<QByteArray>& arguments);
};

static QList<WebSocketCommand> s_commands{
    WebSocketCommand{"reset", 0, cmdReset},
    WebSocketCommand{"quit", 0, cmdQuit},
    WebSocketCommand{"has", 1, cmdHas},
    WebSocketCommand{"property", 2, cmdProperty},
    WebSocketCommand{"click", 1, cmdClick},
    WebSocketCommand{"stealurls", 0, cmdStealurls},
    WebSocketCommand{"lasturl", 0, cmdLasturl},
    WebSocketCommand{"force_update_check", 1, cmdForceUpdateCheck},
    WebSocketCommand{"force_captive_portal_check", 0,
                     cmdForceCaptivePortalCheck},
    WebSocketCommand{"force_captive_portal_detection", 0,
                     cmdForceCaptivePortalDetection},
    WebSocketCommand{"force_unsecured_network", 0, cmdForceUnsecuredNetwork},
    WebSocketCommand{"activate", 0, cmdActivate},
    WebSocketCommand{"deactivate", 0, cmdDeactivate},
    WebSocketCommand{"logout", 0, cmdLogout},
    WebSocketCommand{"screen_capture", 0, cmdScreenCapture},
};

InspectorWebSocketConnection::InspectorWebSocketConnection(
    QObject* parent, QWebSocket* connection)
    : QObject(parent), m_connection(connection) {
  MVPN_COUNT_CTOR(InspectorWebSocketConnection);

  // `::ffff:127.0.0.1` is the IPv4 localhost address written with the IPv6
  // notation.
  Q_ASSERT(connection->localAddress() == QHostAddress("::ffff:127.0.0.1") ||
           connection->localAddress() == QHostAddress::LocalHost ||
           connection->localAddress() == QHostAddress::LocalHostIPv6);

  logger.log() << "New connection received";

  Q_ASSERT(m_connection);
  connect(m_connection, &QWebSocket::textMessageReceived, this,
          &InspectorWebSocketConnection::textMessageReceived);
  connect(m_connection, &QWebSocket::binaryMessageReceived, this,
          &InspectorWebSocketConnection::binaryMessageReceived);

  connect(LogHandler::instance(), &LogHandler::logEntryAdded, this,
          &InspectorWebSocketConnection::logEntryAdded);
}

InspectorWebSocketConnection::~InspectorWebSocketConnection() {
  MVPN_COUNT_DTOR(InspectorWebSocketConnection);
  logger.log() << "Connection released";
}

void InspectorWebSocketConnection::textMessageReceived(const QString& message) {
  logger.log() << "Text message received";
  parseCommand(message.toLocal8Bit());
}

void InspectorWebSocketConnection::binaryMessageReceived(
    const QByteArray& message) {
  logger.log() << "Binary message received";
  parseCommand(message);
}

void InspectorWebSocketConnection::parseCommand(const QByteArray& command) {
  logger.log() << "command received:" << command;

  if (command.isEmpty()) {
    return;
  }

  QList<QByteArray> parts = command.split(' ');
  Q_ASSERT(!parts.isEmpty());

  QString cmdName = parts[0].trimmed();

  for (const WebSocketCommand& command : s_commands) {
    if (cmdName == command.m_commandName) {
      if (parts.length() != command.m_arguments + 1) {
        m_connection->sendTextMessage(
            QString("too many arguments (%1 expected)")
                .arg(command.m_arguments)
                .toLocal8Bit());
        return;
      }

      if (command.m_callback(m_connection, parts)) {
        m_connection->sendTextMessage("ok");
      } else {
        m_connection->sendTextMessage("ko");
      }

      return;
    }
  }

  m_connection->sendTextMessage("invalid command");
}

void InspectorWebSocketConnection::logEntryAdded(const QByteArray& log) {
  // No logger here to avoid loops!

  QByteArray buffer;
  buffer.append("!");
  buffer.append(log);

  m_connection->sendTextMessage(buffer);
}

// static
void InspectorWebSocketConnection::setLastUrl(const QUrl& url) {
  s_lastUrl = url;
}

// static
bool InspectorWebSocketConnection::stealUrls() { return s_stealUrls; }

// static
QString InspectorWebSocketConnection::appVersionForUpdate() {
  if (s_updateVersion.isEmpty()) {
    return APP_VERSION;
  }

  return s_updateVersion;
}
