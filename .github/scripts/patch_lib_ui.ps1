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
# layer over it cannot leave anything behind. The pen and the brush are put
# back afterwards, since a row may pick its text color before its background.
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
			New = "void SettingsButton::paintBg(Painter &p, const QRect &rect, bool over) const {`n`tp.fillRect(rect, _st.textBg);`n`tif (!over) {`n`t`treturn;`n`t}`n`tauto hq = PainterHighQualityEnabler(p);`n`tconst auto full = this->rect();`n`tconst auto scaled = style::ConvertScale(kSettingsButtonRadius);`n`tconst auto radius = (full.height() > 2 * scaled)`n`t`t? scaled`n`t`t: (full.height() / 2);`n`tp.save();`n`tp.setPen(Qt::NoPen);`n`tp.setBrush(_st.textBgOver);`n`tp.drawRoundedRect(full, radius, radius);`n`tp.restore();`n}`n";
		})

# Popup and drawer menu rows get the same rounded hover. The row background
# is painted over the whole item first, for the same reason as above. Here
# the pen matters most: "Mute forever" sets its red before the background,
# and without it the text vanished under the cursor.
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
			New = "void Action::paintBackground(QPainter &p, bool selected) {`n`tp.fillRect(0, 0, width(), _height, _st.itemBg);`n`tif (!selected) {`n`t`treturn;`n`t}`n`tauto hq = PainterHighQualityEnabler(p);`n`tconst auto margin = style::ConvertScale(kHoursGramMenuMargin);`n`tconst auto skip = (margin < (width() / 8)) ? margin : (width() / 8);`n`tconst auto scaled = style::ConvertScale(kHoursGramMenuRadius);`n`tconst auto radius = (scaled < (_height / 2)) ? scaled : (_height / 2);`n`tp.save();`n`tp.setPen(Qt::NoPen);`n`tp.setBrush(_st.itemBgOver);`n`tp.drawRoundedRect(`n`t`tQRect(skip, 0, width() - 2 * skip, _height),`n`t`tradius,`n`t`tradius);`n`tp.restore();`n}`n";
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

# Input fields painted windowBg under the text. Telegram's boxes are windowBg
# too, so nothing showed there, but ours are boxBg and every field in a box
# got a lighter block behind it - the host, the port and the key of a proxy,
# for one. The fields now take the color of whatever they sit on.
Edit-Text `
	(Join-Path $WorkspaceRoot 'Telegram/lib_ui/ui/widgets/widgets.style') `
	'HoursGram: see-through input fields' `
	@(
		@{
			Old = "defaultInputField: InputField {`n`ttextBg: windowBg;`n`ttextBgActive: windowBg;`n";
			New = "defaultInputField: InputField {`n`t// HoursGram: see-through input fields.`n`ttextBg: transparent;`n`ttextBgActive: transparent;`n";
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

# Direct3D 11 hands out its software device, WARP, when the GPU or its driver
# is not usable, and the probe took that as a GPU. Every frame of the window
# was then drawn by the processor through an emulated GPU, which is slower
# than the plain renderer: typing, sending and scrolling all lagged. The probe
# now turns RHI down for a software device, as upstream lib_ui does.
Edit-Text `
	(Join-Path $WorkspaceRoot 'Telegram/lib_ui/ui/gl/gl_detection.cpp') `
	'HoursGram: no RHI on a software device' `
	@(
		@{
			Old = "`tconst auto compute = rhi->isFeatureSupported(QRhi::Compute);`n`tLOG((`"RHI: Probe backend=%1 device=%2 compute=%3.`"`n`t`t).arg(rhi->backendName()`n`t`t).arg(rhi->driverInfo().deviceName`n`t`t).arg(compute ? `"yes`" : `"no`"));`n";
			New = "`t// HoursGram: no RHI on a software device, it is slower than none.`n`tconst auto info = rhi->driverInfo();`n`tconst auto software = (info.deviceType == QRhiDriverInfo::CpuDevice);`n`tconst auto compute = rhi->isFeatureSupported(QRhi::Compute);`n`tLOG((`"RHI: Probe backend=%1 device=%2 software=%3 compute=%4.`"`n`t`t).arg(rhi->backendName()`n`t`t).arg(info.deviceName`n`t`t).arg(software ? `"yes`" : `"no`"`n`t`t).arg(compute ? `"yes`" : `"no`"));`n`tif (software) {`n`t`tLOG((`"RHI: Not supported, software device.`"));`n`t`treturn {};`n`t}`n";
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

# Mini apps did not take a single click on Windows since the move to Qt 6.
# The webview sits in a native child window whose default format has alpha,
# so Qt made it a layered window, and an empty layered window is skipped by
# mouse hit-testing. Upstream lib_webview drops the alpha the same way.
Edit-Text `
	(Join-Path $WorkspaceRoot 'Telegram/lib_webview/webview/platform/win/webview_win.cpp') `
	'HoursGram: no alpha for the webview window' `
	@(
		@{
			Old = "`tauto result = base::make_unique_q<QWindow>();`n`tresult->setFlag(Qt::FramelessWindowHint);`n`treturn result;`n";
			New = "`tauto result = base::make_unique_q<QWindow>();`n`tresult->setFlag(Qt::FramelessWindowHint);`n`n`t// HoursGram: no alpha for the webview window, or Qt makes it a`n`t// layered window that mouse hit-testing skips.`n`tauto format = result->format();`n`tformat.setAlphaBufferSize(0);`n`tresult->setFormat(format);`n`treturn result;`n";
		})

Write-Output 'Applied HoursGram fonts, rounded hover states, see-through input fields, the GPU renderer and its software check, and the mini app input to the pinned libraries.'
