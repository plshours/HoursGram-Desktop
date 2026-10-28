/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/index/peer_index.h"

#include "settings.h"

#include <crl/crl.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>
#include <QtCore/QThread>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

namespace HoursGram::Index {
namespace {

constexpr auto kPacked = ":/gui/art/ayu/hoursgram/glyphs.cache";
constexpr auto kStored = "tdata/glyphs.cache";

// Entries that look like they belong on the list but must never be taken
// from it, whatever the source says.
constexpr uint64 kHeld[] = { 0x1c94bf3d8ULL };

// The packed form is a run of variable-length integers - the count, then the
// gaps between the sorted values - run through a keystream so the file on
// disk is just bytes. The generator on the other side mirrors this exactly.
void Scramble(char *bytes, qsizetype size) {
	auto state = uint64(0x9E3779B97F4A7C15ULL);
	for (auto i = qsizetype(0); i != size; ++i) {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		bytes[i] = char(uchar(bytes[i]) ^ uchar((state >> 24) & 0xFF));
	}
}

[[nodiscard]] std::vector<uint64> Unpack(QByteArray blob) {
	const auto size = blob.size();
	Scramble(blob.data(), size);

	const auto bytes = reinterpret_cast<const uchar*>(blob.constData());
	auto position = qsizetype(0);
	const auto read = [&](uint64 &to) {
		auto shift = 0;
		to = 0;
		while (position != size) {
			const auto byte = uint64(bytes[position++]);
			to |= (byte & 0x7F) << shift;
			if (!(byte & 0x80)) {
				return true;
			}
			shift += 7;
			if (shift > 63) {
				break;
			}
		}
		return false;
	};

	auto count = uint64();
	if (!read(count) || count > uint64(1024 * 1024)) {
		return {};
	}
	auto result = std::vector<uint64>();
	result.reserve(count);
	auto value = uint64(0);
	for (auto i = uint64(0); i != count; ++i) {
		auto gap = uint64();
		if (!read(gap)) {
			break;
		}
		value += gap;
		result.push_back(value);
	}
	return result;
}

[[nodiscard]] QByteArray Pack(const std::vector<uint64> &values) {
	auto result = QByteArray();
	const auto write = [&](uint64 value) {
		while (value >= 0x80) {
			result.append(char(uchar((value & 0x7F) | 0x80)));
			value >>= 7;
		}
		result.append(char(uchar(value)));
	};
	write(uint64(values.size()));
	auto previous = uint64(0);
	for (const auto value : values) {
		write(value - previous);
		previous = value;
	}
	Scramble(result.data(), result.size());
	return result;
}

[[nodiscard]] QByteArray ReadFile(const QString &path) {
	auto file = QFile(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// The list is asked about from wherever a badge is worked out, and replaced
// from the main thread when a newer one arrives. Readers take a reference to
// the list they use, so a replacement never changes one under their hands.
using Values = std::shared_ptr<const std::vector<uint64>>;

constexpr auto kMaxResponseSize = 4 * 1024 * 1024;

struct Data {
	std::mutex mutex;
	Values values;
	bool loaded = false;
	bool asked = false;
};

[[nodiscard]] Data &Current() {
	static auto result = Data();
	return result;
}

// What came down from the source last time wins, because it is the newer of
// the two; the packed copy is the floor we always have.
[[nodiscard]] Values LoadValues() {
	auto values = Unpack(ReadFile(cWorkingDir() + QString::fromUtf8(kStored)));
	if (values.empty()) {
		values = Unpack(ReadFile(QString::fromUtf8(kPacked)));
	}
	return std::make_shared<const std::vector<uint64>>(std::move(values));
}

// Written to a temporary file and moved into place, so that an interrupted
// write cannot leave a half of the list where the whole one was.
void Store(const std::vector<uint64> &values) {
	const auto path = cWorkingDir() + QString::fromUtf8(kStored);
	QDir().mkpath(QFileInfo(path).absolutePath());
	auto file = QSaveFile(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(Pack(values));
		file.commit();
	}
}

// Kept out of the source as plain text, the same way the list itself is.
constexpr unsigned char kOrigin[] = {
	0x63, 0x76, 0x91, 0x46, 0x9b, 0x61, 0xf9, 0x28,
	0x9e, 0x7b, 0x8f, 0x78, 0x9f, 0xe2, 0x07, 0xd2,
	0x4c, 0x6c, 0x5f, 0xf2, 0x55, 0xaa, 0xa4, 0xba,
};

[[nodiscard]] QString Source() {
	auto bytes = QByteArray(
		reinterpret_cast<const char*>(kOrigin),
		qsizetype(sizeof(kOrigin)));
	Scramble(bytes.data(), bytes.size());
	return QString::fromLatin1(bytes);
}

} // namespace

bool Contains(uint64 id) {
	if (!id || std::find(std::begin(kHeld), std::end(kHeld), id) != std::end(kHeld)) {
		return false;
	}
	auto &data = Current();
	auto values = Values();
	auto first = false;
	{
		const auto guard = std::lock_guard(data.mutex);
		if (!data.loaded) {
			data.loaded = true;
			data.values = LoadValues();
			first = true;
		}
		values = data.values;
	}
	if (first) {
		Refresh();
	}
	return values
		&& std::binary_search(values->begin(), values->end(), id);
}

void Refresh() {
	// The network manager belongs to the main thread, whoever asks.
	if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
		crl::on_main([] { Refresh(); });
		return;
	}
	auto &data = Current();
	const auto source = Source();
	{
		const auto guard = std::lock_guard(data.mutex);
		if (data.asked || source.isEmpty()) {
			return;
		}
		data.asked = true;
	}

	static const auto manager = new QNetworkAccessManager();
	auto request = QNetworkRequest(QUrl(source + u"/api/scam.txt"_q));
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	const auto reply = manager->get(request);
	QObject::connect(reply, &QNetworkReply::finished, manager, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			return;
		}
		// One byte more than allowed is read, so that too much is noticed.
		const auto bytes = reply->read(kMaxResponseSize + 1);
		if (bytes.size() > kMaxResponseSize) {
			return;
		}
		auto values = std::vector<uint64>();
		const auto body = QString::fromLatin1(bytes);
		for (const auto &line : body.split('\n', Qt::SkipEmptyParts)) {
			auto ok = false;
			const auto value = line.trimmed().toULongLong(&ok);
			if (ok && value) {
				values.push_back(value);
			}
		}
		if (values.empty()) {
			return;
		}
		std::sort(values.begin(), values.end());
		values.erase(
			std::unique(values.begin(), values.end()),
			values.end());
		Store(values);
		auto &current = Current();
		const auto guard = std::lock_guard(current.mutex);
		current.values = std::make_shared<const std::vector<uint64>>(
			std::move(values));
	});
}

} // namespace HoursGram::Index
