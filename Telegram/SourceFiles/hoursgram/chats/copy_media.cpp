/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/chats/copy_media.h"

#include "base/timer.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "history/history.h"
#include "history/history_item.h"
#include "hoursgram/music/music_common.h"
#include "main/main_session.h"
#include "ui/image/image.h"
#include "ui/layers/show.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QMimeData>
#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace HoursGram::Chats {
namespace {

using HoursGram::Music::Tr;

constexpr auto kTimeout = crl::time(120 * 1000);
constexpr auto kMaxFiles = 50;

struct Entry {
	PhotoData *photo = nullptr;
	DocumentData *document = nullptr;
	std::shared_ptr<Data::PhotoMedia> photoView;
	std::shared_ptr<Data::DocumentMedia> documentView;
	Data::FileOrigin origin;
	QString path;
	bool requested = false;
};

struct Job {
	std::vector<Entry> entries;
	std::shared_ptr<Ui::Show> show;
	rpl::lifetime lifetime;
	bool finished = false;
};

[[nodiscard]] QString Folder() {
	return QDir::tempPath() + u"/HoursGram/clipboard/"_q;
}

// Names come from the senders of the files, so only a plain name is kept.
[[nodiscard]] QString SafeName(QString name) {
	for (const auto ch : { '/', '\\', ':', '*', '?', '"', '<', '>', '|' }) {
		name.replace(QChar(ch), QChar('_'));
	}
	name = name.trimmed();
	while (name.startsWith(QChar('.'))) {
		name.remove(0, 1);
	}
	return name;
}

[[nodiscard]] QString DocumentName(not_null<DocumentData*> document) {
	const auto name = SafeName(document->filename());
	if (!name.isEmpty()) {
		return name;
	}
	const auto extension = document->isVideoFile()
		? u"mp4"_q
		: document->isVoiceMessage()
		? u"ogg"_q
		: document->isAudioFile()
		? u"mp3"_q
		: u"bin"_q;
	return u"file_%1.%2"_q.arg(document->id).arg(extension);
}

// True once the entry has a file on the disk. Asks for what is missing the
// first time it is checked.
bool Resolve(Entry &entry) {
	if (!entry.path.isEmpty()) {
		return true;
	}
	if (const auto photo = entry.photo) {
		if (const auto image = entry.photoView->image(Data::PhotoSize::Large)) {
			QDir().mkpath(Folder());
			const auto path = Folder() + u"photo_%1.jpg"_q.arg(photo->id);
			if (QFile::exists(path)
				|| image->original().save(path, "JPG", 92)) {
				entry.path = path;
				return true;
			}
		} else if (!entry.requested) {
			entry.requested = true;
			entry.photoView->wanted(Data::PhotoSize::Large, entry.origin);
		}
		return false;
	}
	const auto document = entry.document;
	if (!document) {
		return true;
	}
	const auto local = document->filepath(true);
	if (!local.isEmpty()) {
		entry.path = local;
		return true;
	}
	const auto folder = Folder() + QString::number(document->id) + QChar('/');
	const auto path = folder + DocumentName(document);
	const auto bytes = entry.documentView->bytes();
	if (!bytes.isEmpty()) {
		QDir().mkpath(folder);
		auto file = QFile(path);
		if (file.open(QIODevice::WriteOnly)
			&& file.write(bytes) == bytes.size()) {
			entry.path = path;
			return true;
		}
	}
	if (!entry.requested) {
		entry.requested = true;
		QDir().mkpath(folder);
		document->save(entry.origin, path);
	}
	return false;
}

void Finish(const std::shared_ptr<Job> &job) {
	job->finished = true;
	auto urls = QList<QUrl>();
	auto missing = 0;
	for (const auto &entry : job->entries) {
		if (entry.path.isEmpty()) {
			++missing;
		} else {
			urls.push_back(QUrl::fromLocalFile(entry.path));
		}
	}
	if (urls.isEmpty()) {
		job->show->showToast(Tr(
			"Could not copy the media.",
			"Не удалось скопировать медиа."));
	} else {
		auto mime = std::make_unique<QMimeData>();
		mime->setUrls(urls);
		// A single photo also goes as an image, for the apps that do not
		// take files from the clipboard.
		if (urls.size() == 1 && job->entries.front().photo) {
			mime->setImageData(QImage(urls.front().toLocalFile()));
		}
		QGuiApplication::clipboard()->setMimeData(mime.release());
		const auto count = QString::number(urls.size());
		job->show->showToast(missing
			? Tr(
				"Copied %1, some could not be downloaded. Paste with Ctrl+V.",
				"Скопировано: %1, часть не скачалась. Вставьте через Ctrl+V."
			).arg(count)
			: Tr(
				"Copied %1. Paste into the message field with Ctrl+V.",
				"Скопировано: %1. Вставьте в поле сообщения через Ctrl+V."
			).arg(count));
	}
	// Not from inside the callbacks the lifetime itself holds.
	crl::on_main([job] {
		job->lifetime.destroy();
	});
}

} // namespace

bool HasCopyableMedia(not_null<HistoryItem*> item) {
	if (item->forbidsSaving()) {
		return false;
	}
	const auto media = item->media();
	return media && (media->photo() || media->document());
}

void CopyMediaToClipboard(
		std::shared_ptr<Ui::Show> show,
		std::vector<not_null<HistoryItem*>> items) {
	auto job = std::make_shared<Job>();
	job->show = std::move(show);
	auto session = (Main::Session*)nullptr;
	for (const auto &item : items) {
		if (!HasCopyableMedia(item)
			|| int(job->entries.size()) >= kMaxFiles) {
			continue;
		}
		session = &item->history()->session();
		const auto media = item->media();
		const auto origin = Data::FileOrigin(
			Data::FileOriginMessage(item->fullId()));
		if (const auto photo = media->photo()) {
			job->entries.push_back({
				.photo = photo,
				.photoView = photo->createMediaView(),
				.origin = origin,
			});
		} else if (const auto document = media->document()) {
			job->entries.push_back({
				.document = document,
				.documentView = document->createMediaView(),
				.origin = origin,
			});
		}
	}
	if (job->entries.empty() || !session) {
		job->show->showToast(Tr(
			"There is no media to copy here.",
			"Здесь нет медиа, которое можно скопировать."));
		return;
	}

	const auto check = [=] {
		if (job->finished) {
			return;
		}
		auto ready = true;
		for (auto &entry : job->entries) {
			if (!Resolve(entry)) {
				ready = false;
			}
		}
		if (ready) {
			Finish(job);
		}
	};
	session->downloaderTaskFinished(
	) | rpl::on_next(check, job->lifetime);
	const auto timer = job->lifetime.make_state<base::Timer>([=] {
		if (!job->finished) {
			Finish(job);
		}
	});
	timer->callOnce(kTimeout);

	check();
	if (!job->finished) {
		job->show->showToast(Tr(
			"Downloading the media to copy it...",
			"Скачиваю медиа, чтобы скопировать..."));
	}
}

} // namespace HoursGram::Chats
