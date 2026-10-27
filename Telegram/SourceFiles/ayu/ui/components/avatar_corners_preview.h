// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ui/rp_widget.h"
#include "ui/userpic_view.h"

namespace Window {
class SessionController;
} // namespace Window

class PeerData;

class AvatarCornersPreview final : public Ui::RpWidget {
public:
	AvatarCornersPreview(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	PeerData *_peer = nullptr;
	Ui::PeerUserpicView _userpicView;
	QString _subtitle;

};
