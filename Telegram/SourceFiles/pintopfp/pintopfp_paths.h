/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>

namespace Pintopfp {

struct Paths {
	QString folder;
	QString python;
	QString script;
	QString env;
	[[nodiscard]] bool valid() const;
};

[[nodiscard]] Paths ResolvePaths();
void SetFolderOverride(const QString &folder);
[[nodiscard]] QString FolderOverride();

} // namespace Pintopfp
