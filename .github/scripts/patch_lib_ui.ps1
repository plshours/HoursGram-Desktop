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

# AyuGram keeps its changes to lib_ui and codegen in forks of its own. We
# build from the upstream libraries that Telegram Desktop pins and lay those
# changes over them here, so that following Telegram does not wait for the
# forks. The patch goes through the index, where the text is LF whatever the
# checkout does with line endings, and nothing happens when it is in already.
function Apply-Patch(
		[string]$Repository,
		[string]$Patch) {
	if (!(Test-Path -LiteralPath $Repository)) {
		throw "The pinned checkout $Repository is missing."
	}
	# A failed check writes to stderr, which must not stop the script here.
	$previous = $ErrorActionPreference
	$ErrorActionPreference = 'Continue'
	& git -C $Repository apply --cached --check --reverse $Patch 2>&1 | Out-Null
	$applied = ($LASTEXITCODE -eq 0)
	$ErrorActionPreference = $previous
	if ($applied) {
		return
	}
	& git -C $Repository apply --cached $Patch
	if ($LASTEXITCODE -ne 0) {
		throw "Could not apply $Patch to $Repository."
	}
	& git -C $Repository checkout -- .
	if ($LASTEXITCODE -ne 0) {
		throw "Could not check out the patched $Repository."
	}
}

Apply-Patch `
	(Join-Path $WorkspaceRoot 'Telegram/lib_ui') `
	(Join-Path $WorkspaceRoot '.github/patches/lib_ui-ayugram.patch')
Apply-Patch `
	(Join-Path $WorkspaceRoot 'Telegram/codegen') `
	(Join-Path $WorkspaceRoot '.github/patches/codegen-ayugram.patch')

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

Write-Output 'Applied the AyuGram changes, rounded hover states and see-through input fields to the pinned libraries.'
