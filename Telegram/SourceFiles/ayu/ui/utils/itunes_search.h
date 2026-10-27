// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <QtGui/QImage>

namespace Ayu::Ui::Itunes {

// Blocks the calling thread on the network, so it must never run on the
// main thread. It hands back a QImage rather than a QPixmap: pixmaps are
// only valid on the GUI thread, and this is always called off it.
QImage FetchCover(const QString &performer,
                  const QString &title,
                  int sizeHintPx = 300,
                  int timeoutMs = 5000);

}
