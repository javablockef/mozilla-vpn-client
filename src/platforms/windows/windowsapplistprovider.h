/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WINDOWSAPPLISTPROVIDER_H
#define WINDOWSAPPLISTPROVIDER_H

#include "applistprovider.h"
#include <QObject>

class WindowsAppListProvider : public AppListProvider {
  Q_OBJECT
 public:
  WindowsAppListProvider(QObject* parent);
  ~WindowsAppListProvider();
  void getApplicationList() override;
 private:
  void readLinkFiles(QString path,QMap<QString,QString>* out);
};

#endif  // WINDOWSAPPLISTPROVIDER_H
