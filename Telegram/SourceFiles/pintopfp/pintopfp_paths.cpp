/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "pintopfp/pintopfp_paths.h"

#include "settings.h"

#include <QtCore/QFile>

namespace Pintopfp {
namespace {

const auto kDefaultFolder = u"C:/Users/angelicold/Desktop/pintopfp"_q;

QString OverrideFilePath() {
	return cWorkingDir() + u"tdata/pintopfp_folder.txt"_q;
}

} // namespace

bool Paths::valid() const {
	return QFile::exists(python) && QFile::exists(script);
}

Paths ResolvePaths() {
	const auto over = FolderOverride();
	const auto folder = over.isEmpty() ? kDefaultFolder : over;
	return Paths{
		.folder = folder,
		.python = folder + u"/.venv/Scripts/pythonw.exe"_q,
		.script = folder + u"/main.py"_q,
		.env = folder + u"/.env"_q,
	};
}

void SetFolderOverride(const QString &folder) {
	auto file = QFile(OverrideFilePath());
	if (folder.isEmpty()) {
		file.remove();
		return;
	}
	if (file.open(QIODevice::WriteOnly)) {
		file.write(folder.toUtf8());
	}
}

QString FolderOverride() {
	auto file = QFile(OverrideFilePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return QString();
	}
	return QString::fromUtf8(file.readAll()).trimmed();
}

} // namespace Pintopfp
