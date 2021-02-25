/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "inspectorhttpconnection.h"
#include "leakdetector.h"
#include "logger.h"
#include "mozillavpn.h"
#include "qmlengineholder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpSocket>

constexpr const char* HTTP_404_RESPONSE =
    "HTTP/1.1 404 Not Found\nServer: mozillavpn\nContent-Type: "
    "text/html\n\n<html><h1>Not found</h1></html>\n";

namespace {
Logger logger(LOG_INSPECTOR, "InspectorHttpConnection");

struct Path {
  QString m_path;
  QString m_file;
  QString m_mime;
};

static QList<Path> s_paths{
    Path{"/", ":webserver/index.html", "text/html"},
    Path{"/index.html", ":webserver/index.html", "text/html"},
    Path{"/main.css", ":webserver/main.css", "text/css"},
};

}  // namespace

InspectorHttpConnection::InspectorHttpConnection(QObject* parent,
                                                 QTcpSocket* connection)
    : QObject(parent), m_connection(connection) {
  MVPN_COUNT_CTOR(InspectorHttpConnection);

  // `::ffff:127.0.0.1` is the IPv4 localhost address written with the IPv6
  // notation.
  Q_ASSERT(connection->localAddress() == QHostAddress("::ffff:127.0.0.1") ||
           connection->localAddress() == QHostAddress::LocalHost ||
           connection->localAddress() == QHostAddress::LocalHostIPv6);

  logger.log() << "New connection received";

  Q_ASSERT(m_connection);
  connect(m_connection, &QTcpSocket::readyRead, this,
          &InspectorHttpConnection::readData);
}

InspectorHttpConnection::~InspectorHttpConnection() {
  MVPN_COUNT_DTOR(InspectorHttpConnection);
  logger.log() << "Connection released";
}

void InspectorHttpConnection::readData() {
  Q_ASSERT(m_connection);
  QByteArray input = m_connection->readAll();
  m_buffer.append(input);

  while (true) {
    int pos = m_buffer.indexOf("\n");
    if (pos == -1) {
      break;
    }

    QByteArray line = m_buffer.left(pos);
    m_buffer.remove(0, pos + 1);

    QString header = QString(line).trimmed();
    if (header == "") {
      processHeaders();
      break;
    }

    m_headers.append(header);
  }
}

bool InspectorHttpConnection::processScreenCapture(
    const QString& path, QStandardPaths::StandardLocation location) {
  if (!QFileInfo::exists(QStandardPaths::writableLocation(location))) {
    return false;
  }

  QDir dir(QStandardPaths::writableLocation(location));
  QString fileName = dir.filePath(path);

  if (!QFileInfo::exists(fileName)) {
    return false;
  }

  QFile file(fileName);

  if (!file.open(QIODevice::ReadOnly)) {
    logger.log() << "Unable to read file" << fileName;
    return false;
  }

  QByteArray content = file.readAll();

  QByteArray response;
  {
    QTextStream out(&response);
    out << "HTTP/1.1 200 OK\n";
    out << "Content-Type: image/png\n";
    out << "Content-Length: " << content.length() << "\n";
    out << "Server: mozillavpn\n\n";
  }

  m_connection->write(response);
  m_connection->write(content);
  m_connection->close();
  return true;
}

void InspectorHttpConnection::processHeaders() {
  logger.log() << "process headers:" << m_headers;

  if (m_headers.isEmpty()) {
    m_connection->close();
    return;
  }

  QStringList parts = m_headers[0].split(" ");
  if (parts[0] != "GET" || parts.length() < 2) {
    m_connection->write(HTTP_404_RESPONSE);
    m_connection->close();
    return;
  }

  QString path = parts[1];
  for (const Path& p : s_paths) {
    if (path == p.m_path) {
      QFile file(p.m_file);

      if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        logger.log() << "Unable to read file" << p.m_file;
        break;
      }

      QByteArray content = file.readAll();

      QByteArray response;
      {
        QTextStream out(&response);
        out << "HTTP/1.1 200 OK\n";
        out << "Content-Type: " << p.m_mime << "\n";
        out << "Content-Length: " << content.length() << "\n";
        out << "Server: mozillavpn\n\n";
      }

      m_connection->write(response);
      m_connection->write(content);
      m_connection->close();
      return;
    }
  }

  // Special requests for the screen capture files
  logger.log() << "PATH:" << path;
  if (path.startsWith("/screenCapture/") && !path.contains("..") &&
      path.endsWith(".png")) {
    path.remove(0, 15);

    if (processScreenCapture(path, QStandardPaths::DesktopLocation)) {
      return;
    }

    if (processScreenCapture(path, QStandardPaths::HomeLocation)) {
      return;
    }

    if (processScreenCapture(path, QStandardPaths::TempLocation)) {
      return;
    }
  }

  m_connection->write(HTTP_404_RESPONSE);
  m_connection->close();
}
