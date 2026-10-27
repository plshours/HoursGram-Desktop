/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_section_stack.h"

#include "ui/painter.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/labels.h"
#include "ui/ui_utility.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "ui/rect_part.h"

#include "styles/style_info.h"

namespace Info::Profile {
namespace {

// A section of the profile reads as a card: a rounded shape inset from the
// sides of the panel, with the rows of the section drawn on top of it.
//
// The rows paint their own background across the full width of the card, so
// they would square off its corners. Rather than repaint the whole card over
// them, four small squares sit in the corners above the rows, each cutting a
// quarter circle out of itself and leaving the panel colour behind. They are
// the size of the radius, so what gets repainted is a few hundred pixels
// instead of the area of the card.
//
// The radius is capped at half of the shorter side, which turns a section of
// a single row into a pill.
class SectionCard final : public Ui::PaddingWrap<Ui::RpWidget> {
public:
	SectionCard(QWidget *parent, object_ptr<Ui::RpWidget> content);

protected:
	int resizeGetHeight(int newWidth) override;
	void wrappedSizeUpdated(QSize size) override;
	void paintEvent(QPaintEvent *e) override;

private:
	static constexpr auto kCorners = 4;

	void createCorners();
	void placeCorners();
	void scheduleCornerUpdate();
	[[nodiscard]] QRect cardRect() const;
	[[nodiscard]] int cardRadius(QSize size) const;

	std::array<Ui::RpWidget*, kCorners> _corners = { nullptr };
	bool _cornerUpdateScheduled = false;

};

SectionCard::SectionCard(QWidget *parent, object_ptr<Ui::RpWidget> content)
: PaddingWrap(parent, std::move(content), st::infoProfileCardPadding) {
	createCorners();
	sizeValue() | rpl::on_next([=](QSize) {
		placeCorners();
	}, lifetime());
	events() | rpl::filter([](not_null<QEvent*> event) {
		const auto type = event->type();
		return (type == QEvent::ChildAdded)
			|| (type == QEvent::LayoutRequest);
	}) | rpl::on_next([=](not_null<QEvent*>) {
		scheduleCornerUpdate();
	}, lifetime());
}

QRect SectionCard::cardRect() const {
	const auto padding = st::infoProfileCardPadding;
	return rect().marginsRemoved(QMargins(padding.left(), 0, padding.right(), 0));
}

int SectionCard::cardRadius(QSize size) const {
	return std::max(1, std::min(
		st::infoProfileCardRadius,
		std::min(size.width(), size.height()) / 2));
}

void SectionCard::createCorners() {
	for (auto i = 0; i != kCorners; ++i) {
		const auto corner = Ui::CreateChild<Ui::RpWidget>(this);
		corner->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto right = (i % 2) != 0;
		const auto bottom = (i / 2) != 0;
		corner->paintRequest() | rpl::on_next([=] {
			const auto side = corner->width();
			auto cut = QPainterPath();
			cut.addRect(corner->rect());
			auto circle = QPainterPath();
			circle.addEllipse(QRectF(
				right ? -side : 0,
				bottom ? -side : 0,
				2 * side,
				2 * side));
			auto p = QPainter(corner);
			auto hq = PainterHighQualityEnabler(p);
			p.fillPath(cut.subtracted(circle), st::boxBg);
		}, corner->lifetime());
		_corners[i] = corner;
	}
}

void SectionCard::placeCorners() {
	const auto card = cardRect();
	if (card.isEmpty()) {
		for (const auto corner : _corners) {
			corner->hide();
		}
		return;
	}
	const auto side = cardRadius(card.size());
	for (auto i = 0; i != kCorners; ++i) {
		const auto corner = _corners[i];
		const auto right = (i % 2) != 0;
		const auto bottom = (i / 2) != 0;
		corner->setGeometry(
			right ? (card.x() + card.width() - side) : card.x(),
			bottom ? (card.y() + card.height() - side) : card.y(),
			side,
			side);
		corner->show();
		corner->raise();
		corner->update();
	}
}

// The rows of a section are created again when the section is rebuilt, for
// instance on leaving the edit mode, and a fresh child sits above the
// corners. Any such event puts them back on top, once per event loop pass so
// that a section rebuilt row by row is not reordered on every one of them.
void SectionCard::scheduleCornerUpdate() {
	if (_cornerUpdateScheduled) {
		return;
	}
	_cornerUpdateScheduled = true;
	Ui::PostponeCall(this, [=] {
		_cornerUpdateScheduled = false;
		placeCorners();
	});
}

// A section that hides itself collapses to no height at all, and the card
// collapses with it so that no empty rounded shape is left behind.
int SectionCard::resizeGetHeight(int newWidth) {
	const auto result = PaddingWrap::resizeGetHeight(newWidth);
	const auto weak = wrapped();
	return (weak && weak->height()) ? result : 0;
}

void SectionCard::wrappedSizeUpdated(QSize size) {
	if (size.height() > 0) {
		PaddingWrap::wrappedSizeUpdated(size);
		return;
	}
	const auto padding = this->padding();
	resize(padding.left() + size.width() + padding.right(), 0);
}

void SectionCard::paintEvent(QPaintEvent *e) {
	const auto card = cardRect();
	if (card.isEmpty()) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = cardRadius(card.size());
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBg);
	p.drawRoundedRect(card, radius, radius);
}

[[nodiscard]] not_null<Ui::SlideWrap<>*> CreatePlainSeparator(
		not_null<Ui::VerticalLayout*> layout) {
	auto inner = object_ptr<Ui::VerticalLayout>(layout);
	Ui::AddSkip(inner.data(), st::infoProfileCardSkip);
	return layout->add(object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		layout,
		std::move(inner)));
}

[[nodiscard]] not_null<Ui::SlideWrap<>*> CreateTextSeparator(
		not_null<Ui::VerticalLayout*> layout,
		rpl::producer<TextWithEntities> text,
		Fn<void(not_null<Ui::FlatLabel*>)> setup) {
	auto inner = object_ptr<Ui::VerticalLayout>(layout);
	Ui::AddSkip(inner.data(), st::infoProfileSkip);
	auto label = object_ptr<Ui::FlatLabel>(
		inner.data(),
		std::move(text),
		st::defaultDividerLabel.label);
	const auto rawLabel = label.data();

	// The cards already separate the sections, so the explanation under one
	// of them is plain text on the panel instead of a divider band.
	inner->add(object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
		inner.data(),
		std::move(label),
		st::defaultBoxDividerLabelPadding));
	Ui::AddSkip(inner.data(), st::infoProfileSkip);
	const auto wrap = layout->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			layout,
			std::move(inner)));
	if (setup) {
		setup(rawLabel);
	}
	return wrap;
}

} // namespace

SectionStack::SectionStack(not_null<Ui::VerticalLayout*> layout)
: _layout(layout) {
}

void SectionStack::add(Section section) {
	Expects(!_finalized);
	Expects(section.widget != nullptr);

	_rows.push_back({
		.type = RowType::Block,
		.widget = std::move(section.widget),
		.shown = std::move(section.shown),
	});
}

void SectionStack::addPlainSeparator() {
	Expects(!_finalized);

	if (_rows.empty() || _rows.back().type == RowType::PlainSeparator) {
		return;
	}
	_rows.push_back({ .type = RowType::PlainSeparator });
}

void SectionStack::addTextSeparator(
		rpl::producer<TextWithEntities> text,
		rpl::producer<bool> shown,
		Fn<void(not_null<Ui::FlatLabel*>)> setup) {
	Expects(!_finalized);

	_rows.push_back({
		.type = RowType::TextSeparator,
		.shown = std::move(shown),
		.text = std::move(text),
		.textSetup = std::move(setup),
	});
}

not_null<Ui::VerticalLayout*> SectionStack::layout() const {
	return _layout;
}

// Decides which separators are visible given the intrinsic visibility of
// every row. Blocks and text separators are shown by their own data; the
// computation only resolves plain separators, which exist purely to delimit
// content. A plain separator stays visible only when there is a visible block
// in the run immediately above and the run immediately below it, where each
// run is bounded by the nearest visible divider on that side. Scanning down a
// plain separator treats every later plain separator as a boundary so that, in
// a single gap, the lowest plain separator wins; scanning up it sees through
// plain separators that turned out hidden. Visible text separators bound runs
// on both sides, which keeps a plain separator from doubling a text divider.
std::vector<bool> SectionStack::ComputeVisibility(
		const std::vector<RowType> &kinds,
		const std::vector<bool> &intrinsic) {
	const auto count = int(kinds.size());
	auto result = std::vector<bool>(count, false);
	for (auto i = 0; i != count; ++i) {
		if (kinds[i] != RowType::PlainSeparator) {
			result[i] = intrinsic[i];
		}
	}
	const auto blockVisibleAbove = [&](int separator) {
		for (auto j = separator - 1; j >= 0; --j) {
			switch (kinds[j]) {
			case RowType::Block:
				if (intrinsic[j]) {
					return true;
				}
				break;
			case RowType::TextSeparator:
				if (intrinsic[j]) {
					return false;
				}
				break;
			case RowType::PlainSeparator:
				if (result[j]) {
					return false;
				}
				break;
			}
		}
		return false;
	};
	const auto blockVisibleBelow = [&](int separator) {
		for (auto j = separator + 1; j != count; ++j) {
			switch (kinds[j]) {
			case RowType::Block:
				if (intrinsic[j]) {
					return true;
				}
				break;
			case RowType::TextSeparator:
				if (intrinsic[j]) {
					return false;
				}
				break;
			case RowType::PlainSeparator:
				return false;
			}
		}
		return false;
	};
	for (auto i = 0; i != count; ++i) {
		if (kinds[i] == RowType::PlainSeparator) {
			result[i] = blockVisibleAbove(i) && blockVisibleBelow(i);
		}
	}
	return result;
}

void SectionStack::finalize() {
	Expects(!_finalized);

	_finalized = true;

	Ui::AddSkip(_layout, st::infoProfileCardSkip);

	const auto count = int(_rows.size());
	auto kinds = std::vector<RowType>();
	auto intrinsic = std::vector<rpl::producer<bool>>();
	auto separators = std::vector<Ui::SlideWrap<>*>(count, nullptr);
	kinds.reserve(count);
	intrinsic.reserve(count);
	const auto shownOrTrue = [](rpl::producer<bool> shown) {
		return shown
			? std::move(shown)
			: rpl::producer<bool>(rpl::single(true));
	};
	for (auto i = 0; i != count; ++i) {
		auto &row = _rows[i];
		kinds.push_back(row.type);
		switch (row.type) {
		case RowType::Block:
			// The card carries its inset from the sides; the gap that
			// separates it from the next one is added under it here.
			_layout->add(
				object_ptr<SectionCard>(_layout, std::move(row.widget)),
				QMargins(0, 0, 0, st::infoProfileCardSkip));
			intrinsic.push_back(shownOrTrue(std::move(row.shown)));
			break;
		case RowType::PlainSeparator:
			separators[i] = CreatePlainSeparator(_layout);
			intrinsic.push_back(rpl::single(true));
			break;
		case RowType::TextSeparator:
			separators[i] = CreateTextSeparator(
				_layout,
				std::move(row.text),
				std::move(row.textSetup));
			intrinsic.push_back(shownOrTrue(std::move(row.shown)));
			break;
		}
	}

	Ui::AddSkip(_layout, st::infoProfileCardSkip);

	if (intrinsic.empty()) {
		return;
	}
	const auto visible = _layout->lifetime().make_state<
		rpl::variable<std::vector<bool>>>(rpl::combine(
			std::move(intrinsic),
			[kinds = std::move(kinds)](const std::vector<bool> &values) {
				return SectionStack::ComputeVisibility(kinds, values);
			}));
	for (auto i = 0; i != count; ++i) {
		const auto wrap = separators[i];
		if (!wrap) {
			continue;
		}
		wrap->setDuration(st::infoSlideDuration)->toggleOn(
			visible->value() | rpl::map([=](const std::vector<bool> &v) {
				return (i < int(v.size())) && v[i];
			}) | rpl::distinct_until_changed());
	}
}

} // namespace Info::Profile
