// Copyright (c) 2002-present, OpenMS Inc. -- EKU Tuebingen, ETH Zurich, and FU Berlin
// SPDX-License-Identifier: BSD-3-Clause
// $Maintainer: OpenMS Team $
// $Authors: OpenMS Team $

#include <QFile>

int main()
{
  // Link the actual GUI resource archive; neither Core nor a data directory is used.
  QFile stylesheet(":/GUISTYLE/qtStyleSheet.qss");
  if (!stylesheet.open(QFile::ReadOnly) || stylesheet.readAll().isEmpty())
  {
    return 1;
  }
  QFile icon(":/TOPPView.png");
  return icon.open(QFile::ReadOnly) && !icon.readAll().isEmpty() ? 0 : 1;
}
