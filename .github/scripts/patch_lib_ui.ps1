param(
	[string]$WorkspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
)

$ErrorActionPreference = 'Stop'

# Applies a set of exact text replacements to a pinned submodule file.
# `Skip` is a marker that is already present once the file is patched, so a
# second run over the same checkout does nothing. Every fragment has to match
# exactly once, otherwise the build stops here instead of producing a client
# with half of the changes in it.
function Edit-Text(
		[string]$Path,
		[string]$Skip,
		[hashtable[]]$Replacements) {
	if (!(Test-Path -LiteralPath $Path)) {
		throw "The pinned lib_ui checkout is missing $Path."
	}
	$text = [System.IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
	if ($text.Contains($Skip)) {
		return
	}
	foreach ($replacement in $Replacements) {
		$old = $replacement.Old
		$new = $replacement.New
		$index = $text.IndexOf($old)
		if ($index -lt 0) {
			throw "Could not find the expected fragment in $Path."
		}
		if ($text.IndexOf($old, $index + 1) -ge 0) {
			throw "The expected fragment is ambiguous in $Path."
		}
		$text = $text.Remove($index, $old.Length).Insert($index, $new)
	}
	[System.IO.File]::WriteAllText(
		$Path,
		$text,
		[System.Text.UTF8Encoding]::new($false))
}

# Settings and profile rows: flat by default, a rounded pill under the
# cursor. The row still fills the whole damaged rect first, so closing a
# layer over it cannot leave anything behind.
Edit-Text `
	(Join-Path $WorkspaceRoot 'Telegram/lib_ui/ui/widgets/buttons.cpp') `
	'kSettingsButtonRadius' `
	@(
		@{
			Old = "namespace Ui {`nnamespace {`n";
			New = "namespace Ui {`nnamespace {`n`nconstexpr auto kSettingsButtonRadius = 14;`n";
		},
		@{
			Old = "void SettingsButton::paintBg(Painter &p, const QRect &rect, bool over) const {`n`tp.fillRect(rect, over ? _st.textBgOver : _st.textBg);`n}`n";
			New = "void SettingsButton::paintBg(Painter &p, const QRect &rect, bool over) const {`n`tp.fillRect(rect, _st.textBg);`n`tif (!over) {`n`t`treturn;`n`t}`n`tauto hq = PainterHighQualityEnabler(p);`n`tconst auto full = this->rect();`n`tconst auto scaled = style::ConvertScale(kSettingsButtonRadius);`n`tconst auto radius = (full.height() > 2 * scaled)`n`t`t? scaled`n`t`t: (full.height() / 2);`n`tp.setPen(Qt::NoPen);`n`tp.setBrush(_st.textBgOver);`n`tp.drawRoundedRect(full, radius, radius);`n}`n";
		})

# Popup and drawer menu rows get the same rounded hover. The row background
# is painted over the whole item first, for the same reason as above.
Edit-Text `
	(Join-Path $WorkspaceRoot 'Telegram/lib_ui/ui/widgets/menu/menu_action.cpp') `
	'kHoursGramMenuRadius' `
	@(
		@{
			Old = "namespace Ui::Menu {`nnamespace {`n";
			New = "namespace Ui::Menu {`nnamespace {`n`nconstexpr auto kHoursGramMenuRadius = 10;`nconstexpr auto kHoursGramMenuMargin = 6;`n";
		},
		@{
			Old = "void Action::paintBackground(QPainter &p, bool selected) {`n`tif (selected && _st.itemBgOver->c.alpha() < 255) {`n`t`tp.fillRect(0, 0, width(), _height, _st.itemBg);`n`t}`n`tp.fillRect(`n`t`tQRect(0, 0, width(), _height),`n`t`tselected ? _st.itemBgOver : _st.itemBg);`n}`n";
			New = "void Action::paintBackground(QPainter &p, bool selected) {`n`tp.fillRect(0, 0, width(), _height, _st.itemBg);`n`tif (!selected) {`n`t`treturn;`n`t}`n`tauto hq = PainterHighQualityEnabler(p);`n`tconst auto margin = style::ConvertScale(kHoursGramMenuMargin);`n`tconst auto skip = (margin < (width() / 8)) ? margin : (width() / 8);`n`tconst auto scaled = style::ConvertScale(kHoursGramMenuRadius);`n`tconst auto radius = (scaled < (_height / 2)) ? scaled : (_height / 2);`n`tp.setPen(Qt::NoPen);`n`tp.setBrush(_st.itemBgOver);`n`tp.drawRoundedRect(`n`t`tQRect(skip, 0, width() - 2 * skip, _height),`n`t`tradius,`n`t`tradius);`n}`n";
		})

# Rounder boxes and popup menus.
Edit-Text `
	(Join-Path $WorkspaceRoot 'Telegram/lib_ui/ui/layers/layers.style') `
	'boxRadius: 14px;' `
	@(
		@{
			Old = "boxRadius: 6px;`n";
			New = "boxRadius: 14px;`n";
		})

Edit-Text `
	(Join-Path $WorkspaceRoot 'Telegram/lib_ui/ui/widgets/widgets.style') `
	'radius: 12px;' `
	@(
		@{
			Old = "`tradius: 6px;`n`tduration: 150;`n";
			New = "`tradius: 12px;`n`tduration: 150;`n";
		})

# The main window is drawn on the GPU through Qt RHI (Direct3D 11) on Windows
# x64 too, as Telegram itself does now. The pinned lib_ui kept it to ARM64,
# so x64 drew the whole window on the CPU and scrolled at a low frame rate.
# A failed start still falls back to the old renderer through the crash check.
Edit-Text `
	(Join-Path $WorkspaceRoot 'Telegram/lib_ui/ui/gl/gl_detection.cpp') `
	'HoursGram: RHI on every Windows' `
	@(
		@{
			Old = "`t.scope = [] {`n`t`treturn (!Platform::IsWindows() || Platform::IsWindowsARM64())`n`t`t`t&& QLibraryInfo::version() >= QVersionNumber(6, 7);`n`t},`n";
			New = "`t.scope = [] {`n`t`t// HoursGram: RHI on every Windows, x64 included.`n`t`treturn QLibraryInfo::version() >= QVersionNumber(6, 7);`n`t},`n";
		})

# The swap chain of that renderer goes through DirectComposition, which
# composites it only when it has alpha. Telegram added this together with
# turning RHI on for Windows x64.
Edit-Text `
	(Join-Path $WorkspaceRoot 'Telegram/lib_ui/ui/rp_widget.cpp') `
	'DirectComposition composites swap chain only with alpha' `
	@(
		@{
			Old = "`t`tformat.setSwapInterval(::Platform::MetalSupported() ? 1 : 0);`n#ifdef DESKTOP_APP_USE_ANGLE`n";
			New = "`t`tformat.setSwapInterval(::Platform::MetalSupported() ? 1 : 0);`n#ifdef Q_OS_WIN`n`t`t// DirectComposition composites swap chain only with alpha in it.`n`t`tformat.setAlphaBufferSize(8);`n#endif // Q_OS_WIN`n#ifdef DESKTOP_APP_USE_ANGLE`n";
		})

# Emoji statuses and custom emoji are drawn by FrameGenerator, which took the
# size and the frame count an animation states on trust. A broken or hostile
# one made rlottie throw std::length_error on its render thread while the chat
# list scrolled, and the uncaught exception closed the client. The sticker
# path's bounds apply here as well, as upstream lib_lottie does now, and what
# rlottie still throws stays on the frame instead of ending the process.
Edit-Text `
	(Join-Path $WorkspaceRoot 'Telegram/lib_lottie/lottie/lottie_frame_generator.cpp') `
	'HoursGram: bounded FrameGenerator' `
	@(
		@{
			Old = "#include `"lottie/lottie_common.h`"`n#include `"lottie/lottie_wrap.h`"`n";
			New = "#include `"lottie/details/lottie_frame_renderer.h`"`n#include `"lottie/lottie_common.h`"`n#include `"lottie/lottie_wrap.h`"`n";
		},
		@{
			Old = "`tif (!_framesCount || !_frameDuration || _size.isEmpty()) {`n";
			New = "`t// HoursGram: bounded FrameGenerator, as the sticker path is.`n`tif (!_framesCount`n`t`t|| _framesCount > kMaxFramesCount`n`t`t|| !_frameDuration`n`t`t|| _size.isEmpty()`n`t`t|| _size.width() > kMaxSize`n`t`t|| _size.height() > kMaxSize) {`n";
		},
		@{
			Old = "#include `"ui/image/image_prepare.h`"`n";
			New = "#include `"ui/image/image_prepare.h`"`n`n#include <QtGui/QPainter>`n";
		},
		@{
			Old = "`tconst auto scaled = _size.scaled(size, mode);`n`tconst auto render = QSize(`n`t`tstd::max(scaled.width(), size.width()),`n`t`tstd::max(scaled.height(), size.height()));`n`tconst auto xskip = (size.width() - render.width()) / 2;`n`tconst auto yskip = (size.height() - render.height());`n`tconst auto skip = (yskip * storage.bytesPerLine() / 4) + xskip;`n`tauto surface = rlottie::Surface(`n`t`treinterpret_cast<uint32_t*>(storage.bits()) + skip,`n`t`trender.width(),`n`t`trender.height(),`n`t`tstorage.bytesPerLine());`n`t_rlottie->renderSync(index * _multiplier, std::move(surface));`n";
			New = "`tauto scaled = _size.scaled(size, mode);`n`tif (scaled.width() > kMaxSize || scaled.height() > kMaxSize) {`n`t`t// An extreme aspect ratio expanded to cover the box would need a`n`t`t// surface far larger than any frame, so it is fitted instead.`n`t`tscaled = _size.scaled(size, Qt::KeepAspectRatio);`n`t}`n`tconst auto render = QSize(`n`t`tstd::max(scaled.width(), size.width()),`n`t`tstd::max(scaled.height(), size.height()));`n`tconst auto draw = [&](QImage &into) {`n`t`tauto surface = rlottie::Surface(`n`t`t`treinterpret_cast<uint32_t*>(into.bits()),`n`t`t`trender.width(),`n`t`t`trender.height(),`n`t`t`tinto.bytesPerLine());`n`t`ttry {`n`t`t`t_rlottie->renderSync(index * _multiplier, std::move(surface));`n`t`t} catch (const std::exception &) {`n`t`t`tinto.fill(Qt::transparent);`n`t`t}`n`t};`n`tif (render == size) {`n`t`tdraw(storage);`n`t} else {`n`t`t// Larger than the box: the surface used to start before the storage`n`t`t// and run past it, so rlottie wrote outside of the image. The frame`n`t`t// is rendered whole and cropped instead, as upstream does now.`n`t`tauto expanded = CreateFrameStorage(render);`n`t`texpanded.fill(Qt::transparent);`n`t`tdraw(expanded);`n`t`tauto p = QPainter(&storage);`n`t`tp.setCompositionMode(QPainter::CompositionMode_Source);`n`t`tp.drawImage(`n`t`t`tQPoint(`n`t`t`t`t(size.width() - render.width()) / 2,`n`t`t`t`tsize.height() - render.height()),`n`t`t`texpanded);`n`t}`n";
		})

# HorusGram Graphite: a light top edge under the cursor for buttons and a
# ring that main buttons keep. Only drawn while Ui::HoursGramGraphite() is on.
Edit-Text `
	(Join-Path $WorkspaceRoot 'Telegram/lib_ui/ui/widgets/buttons.cpp') `
	'PaintHoursGramEdge' `
	@(
		@{
			Old = "#include `"ui/round_rect.h`"`n";
			New = "#include `"ui/round_rect.h`"`n#include `"styles/palette.h`"`n";
		},
		@{
			Old = "} // namespace`n`nLinkButton::LinkButton(`n";
			New = "} // namespace`n`nnamespace {`n`nbool HoursGramGraphiteValue = false;`n`n// The edge of HORUSGRAM_DESIGN.md: under the cursor a button gets a thin`n// line around it, light along the top, as if lit from above.`nvoid PaintHoursGramEdge(`n`t`tQPainter &p,`n`t`tQRectF rect,`n`t`tfloat64 radius,`n`t`tfloat64 opacity) {`n`tif (opacity <= 0.) {`n`t`treturn;`n`t}`n`tauto hq = PainterHighQualityEnabler(p);`n`tconst auto line = style::ConvertScaleExact(1.5);`n`tconst auto half = line / 2.;`n`tconst auto inner = rect.marginsRemoved({ half, half, half, half });`n`tconst auto r = std::max(radius - half, 0.);`n`tp.save();`n`tp.setOpacity(p.opacity() * opacity);`n`tp.setBrush(Qt::NoBrush);`n`tp.setPen(QPen(st::windowBgOver->c, line));`n`tp.drawRoundedRect(inner, r, r);`n`tp.setClipRect(`n`t`tQRectF(rect.x(), rect.y(), rect.width(), rect.height() * 0.4),`n`t`tQt::IntersectClip);`n`tp.setPen(QPen(st::windowSubTextFg->c, line));`n`tp.drawRoundedRect(inner, r, r);`n`tp.restore();`n}`n`n// A main button keeps its ring at all times: a dark line set in from the`n// edge of the fill, which reads as a ring with a gap around the button and`n// never needs room outside it. Under the cursor the gap grows.`nvoid PaintHoursGramRing(QPainter &p, QRectF rect, float64 radius, bool over) {`n`tauto hq = PainterHighQualityEnabler(p);`n`tconst auto line = style::ConvertScaleExact(1.5);`n`tconst auto gap = style::ConvertScaleExact(over ? 3. : 2.) + line / 2.;`n`tconst auto inner = rect.marginsRemoved({ gap, gap, gap, gap });`n`tconst auto r = std::max(radius - gap, 0.);`n`tp.save();`n`tp.setBrush(Qt::NoBrush);`n`tp.setPen(QPen(st::activeButtonFg->c, line));`n`tp.drawRoundedRect(inner, r, r);`n`tp.restore();`n}`n`n} // namespace`n`nvoid SetHoursGramGraphite(bool enabled) {`n`tHoursGramGraphiteValue = enabled;`n}`n`nbool HoursGramGraphite() {`n`treturn HoursGramGraphiteValue;`n}`n`nLinkButton::LinkButton(`n";
		},
		@{
			Old = "`tpaintRipple(p, _st.rippleAreaPosition, _rippleColorOverride ? &(*_rippleColorOverride)->c : nullptr);`n`n`tconst auto overIconOpacity = iconOverOpacity();`n";
			New = "`tpaintRipple(p, _st.rippleAreaPosition, _rippleColorOverride ? &(*_rippleColorOverride)->c : nullptr);`n`n`tconst auto overIconOpacity = iconOverOpacity();`n`tif (HoursGramGraphite() && _st.rippleAreaSize > 0) {`n`t`tconst auto size = float64(_st.rippleAreaSize);`n`t`tPaintHoursGramEdge(`n`t`t`tp,`n`t`t`tQRectF(QPointF(_st.rippleAreaPosition), QSizeF(size, size)),`n`t`t`tsize / 2.,`n`t`t`toverIconOpacity);`n`t}`n";
		},
		@{
			Old = "`tauto over = isOver();`n`tauto down = isDown();`n`tif (!_brushOverride && (over || down)) {`n`t`tdrawRect(_roundRectOver);`n`t}`n";
			New = "`tauto over = isOver();`n`tauto down = isDown();`n`tif (!_brushOverride && (over || down)) {`n`t`tdrawRect(_roundRectOver);`n`t}`n`tif (HoursGramGraphite() && !_cornerRadii) {`n`t`tconst auto fill = QRectF(myrtlrect(rounded));`n`t`tconst auto radius = _fullRadius`n`t`t`t? (rounded.height() / 2.)`n`t`t`t: float64(_st.radius ? _st.radius : st::buttonRadius);`n`t`tif (_st.textBg->c == st::activeButtonBg->c) {`n`t`t`tPaintHoursGramRing(p, fill, radius, over || down);`n`t`t} else if (over || down) {`n`t`t`tPaintHoursGramEdge(p, fill, radius, 1.);`n`t`t}`n`t}`n";
		})

Edit-Text `
	(Join-Path $WorkspaceRoot 'Telegram/lib_ui/ui/widgets/buttons.h') `
	'SetHoursGramGraphite' `
	@(
		@{
			Old = "namespace Ui {`n`nclass RippleAnimation;`n";
			New = "namespace Ui {`n`n// HorusGram: while the Graphite theme is on, buttons are drawn with the`n// edges and rings of HORUSGRAM_DESIGN.md.`nvoid SetHoursGramGraphite(bool enabled);`n[[nodiscard]] bool HoursGramGraphite();`n`nclass RippleAnimation;`n";
		})

Write-Output 'Applied HoursGram fonts, rounded hover states, the Graphite button edges, the GPU renderer and the Lottie bounds to the pinned libraries.'

# HorusGram Graphite: settings rows and side bar buttons animate under the
# cursor: the pill and its edge fade in, side bar icons spring up.
Edit-Text `
	(Join-Path $WorkspaceRoot 'Telegram/lib_ui/ui/widgets/buttons.cpp') `
	'_hoursgramOver.start' `
	@(
		@{
			Old = "void SettingsButton::paintEvent(QPaintEvent *e) {`n`tPainter p(this);`n`n`tconst auto paintOver = (isOver() || isDown()) && !isDisabled();`n`tpaintBg(p, e->rect(), paintOver);`n";
			New = "void SettingsButton::paintEvent(QPaintEvent *e) {`n`tPainter p(this);`n`n`tconst auto paintOver = (isOver() || isDown()) && !isDisabled();`n`tif (HoursGramGraphite()) {`n`t`t// The pill and its edge fade in under the cursor instead of`n`t`t// appearing at once.`n`t`tconst auto shown = std::clamp(`n`t`t`t_hoursgramOver.value(paintOver ? 1. : 0.),`n`t`t`t0.,`n`t`t`t1.);`n`t`tpaintBg(p, e->rect(), false);`n`t`tif (shown > 0.) {`n`t`t`tconst auto full = QRectF(rect());`n`t`t`tconst auto scaled = float64(`n`t`t`t`tstyle::ConvertScale(kSettingsButtonRadius));`n`t`t`tconst auto radius = (full.height() > 2 * scaled)`n`t`t`t`t? scaled`n`t`t`t`t: (full.height() / 2.);`n`t`t`t{`n`t`t`t`tauto hq = PainterHighQualityEnabler(p);`n`t`t`t`tp.setOpacity(shown);`n`t`t`t`tp.setPen(Qt::NoPen);`n`t`t`t`tp.setBrush(_st.textBgOver);`n`t`t`t`tp.drawRoundedRect(full, radius, radius);`n`t`t`t`tp.setOpacity(1.);`n`t`t`t}`n`t`t`tPaintHoursGramEdge(p, full, radius, shown);`n`t`t}`n`t} else {`n`t`tpaintBg(p, e->rect(), paintOver);`n`t}`n";
		},
		@{
			Old = "`tconst auto wasDisabled = !!(was & StateFlag::Disabled);`n`tconst auto nowDisabled = isDisabled();`n`tif (!nowDisabled || !isDown()) {`n`t`tRippleButton::onStateChanged(was, source);`n`t}`n";
			New = "`tconst auto wasDisabled = !!(was & StateFlag::Disabled);`n`tconst auto nowDisabled = isDisabled();`n`tif (!nowDisabled || !isDown()) {`n`t`tRippleButton::onStateChanged(was, source);`n`t}`n`tif (HoursGramGraphite()) {`n`t`tconst auto over = (isOver() || isDown()) && !nowDisabled;`n`t`tconst auto wasOver = !wasDisabled`n`t`t`t&& (!!(was & StateFlag::Over) || !!(was & StateFlag::Down));`n`t`tif (over != wasOver) {`n`t`t`t_hoursgramOver.start(`n`t`t`t`t[=] { update(); },`n`t`t`t`tover ? 0. : 1.,`n`t`t`t`tover ? 1. : 0.,`n`t`t`t`tover ? 180 : 260,`n`t`t`t`tanim::easeOutCirc);`n`t`t}`n`t}`n";
		})

Edit-Text `
	(Join-Path $WorkspaceRoot 'Telegram/lib_ui/ui/widgets/buttons.h') `
	'_hoursgramOver' `
	@(
		@{
			Old = "`tstd::optional<QColor> _textColorOverride;`n`tText::MarkedContext _context;`n`n};`n";
			New = "`tstd::optional<QColor> _textColorOverride;`n`tText::MarkedContext _context;`n`tUi::Animations::Simple _hoursgramOver;`n`n};`n";
		})

Edit-Text `
	(Join-Path $WorkspaceRoot 'Telegram/lib_ui/ui/widgets/side_bar_button.cpp') `
	'_hoursgramOver' `
	@(
		@{
			Old = "#include `"ui/effects/ripple_animation.h`"`n";
			New = "#include `"ui/effects/ripple_animation.h`"`n#include `"styles/palette.h`"`n";
		},
		@{
			Old = "`tRippleButton::paintRipple(p, 0, 0);`n`n`tif (_lock.locked) {`n`t`tp.setOpacity(kPremiumLockedOpacity);`n`t}`n`n`tif (_showIcon) {`n`t`tconst auto &icon = computeIcon();`n`t`tconst auto x = iconLeft();`n`t`tconst auto y = iconTop();`n";
			New = "`tauto hoursgramLift = 0;`n`tif (HoursGramGraphite()) {`n`t`t// A pill with a light top edge fades in under the cursor and the`n`t`t// icon springs up a little; the active folder keeps the pill.`n`t`tconst auto value = _hoursgramOver.value(isOver() ? 1. : 0.);`n`t`tconst auto shown = _active ? 1. : std::clamp(value, 0., 1.);`n`t`tif (shown > 0.) {`n`t`t`tauto hq = PainterHighQualityEnabler(p);`n`t`t`tconst auto side = float64(style::ConvertScale(7));`n`t`t`tconst auto top = float64(style::ConvertScale(2));`n`t`t`tconst auto pill = QRectF(rect()).marginsRemoved(`n`t`t`t`t{ side, top, side, top });`n`t`t`tconst auto radius = std::min(`n`t`t`t`tfloat64(style::ConvertScale(16)),`n`t`t`t`tpill.height() / 2.);`n`t`t`tconst auto line = style::ConvertScaleExact(1.5);`n`t`t`tconst auto half = line / 2.;`n`t`t`tconst auto inner = pill.marginsRemoved({ half, half, half, half });`n`t`t`tp.save();`n`t`t`tp.setOpacity(shown);`n`t`t`tp.setPen(Qt::NoPen);`n`t`t`tp.setBrush(st::windowBgOver);`n`t`t`tp.drawRoundedRect(pill, radius, radius);`n`t`t`tp.setBrush(Qt::NoBrush);`n`t`t`tp.setPen(QPen(st::windowBgRipple->c, line));`n`t`t`tp.drawRoundedRect(inner, radius - half, radius - half);`n`t`t`tp.setClipRect(`n`t`t`t`tQRectF(pill.x(), pill.y(), pill.width(), pill.height() * 0.4),`n`t`t`t`tQt::IntersectClip);`n`t`t`tp.setPen(QPen(st::windowSubTextFg->c, line));`n`t`t`tp.drawRoundedRect(inner, radius - half, radius - half);`n`t`t`tp.restore();`n`t`t}`n`t`tif (!_active) {`n`t`t`thoursgramLift = -int(std::round(style::ConvertScale(2) * value));`n`t`t}`n`t}`n`n`tRippleButton::paintRipple(p, 0, 0);`n`n`tif (_lock.locked) {`n`t`tp.setOpacity(kPremiumLockedOpacity);`n`t}`n`n`tif (_showIcon) {`n`t`tconst auto &icon = computeIcon();`n`t`tconst auto x = iconLeft();`n`t`tconst auto y = iconTop() + hoursgramLift;`n";
		},
		@{
			Old = "const style::icon &SideBarButton::computeIcon() const {`n";
			New = "void SideBarButton::onStateChanged(State was, StateChangeSource source) {`n`tRippleButton::onStateChanged(was, source);`n`tconst auto over = isOver();`n`tif (HoursGramGraphite() && (over != !!(was & StateFlag::Over))) {`n`t`t_hoursgramOver.start(`n`t`t`t[=] { update(); },`n`t`t`tover ? 0. : 1.,`n`t`t`tover ? 1. : 0.,`n`t`t`tover ? 320 : 220,`n`t`t`tover ? anim::easeOutBack : anim::easeOutCirc);`n`t}`n}`n`nconst style::icon &SideBarButton::computeIcon() const {`n";
		})

Edit-Text `
	(Join-Path $WorkspaceRoot 'Telegram/lib_ui/ui/widgets/side_bar_button.h') `
	'_hoursgramOver' `
	@(
		@{
			Old = "private:`n`tvoid paintEvent(QPaintEvent *e) override;`n`n`t[[nodiscard]] const style::icon &computeIcon() const;`n";
			New = "private:`n`tvoid paintEvent(QPaintEvent *e) override;`n`tvoid onStateChanged(State was, StateChangeSource source) override;`n`n`t[[nodiscard]] const style::icon &computeIcon() const;`n";
		},
		@{
			Old = "`tbool _showIcon = true;`n`tbool _showText = true;`n";
			New = "`tbool _showIcon = true;`n`tbool _showText = true;`n`tUi::Animations::Simple _hoursgramOver;`n";
		})
