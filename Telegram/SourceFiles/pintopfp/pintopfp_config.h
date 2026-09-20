/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"

#include <QtCore/QString>

#include <vector>

namespace Pintopfp {

class Config {
public:
	[[nodiscard]] QString value(const QString &key) const;
	void set(const QString &key, const QString &value);

private:
	friend Config ParseEnvText(const QString &text);
	friend QString SerializeEnv(const Config &config);

	struct Line {
		bool isKey = false;
		QString key; // Valid when isKey is true.
		QString raw; // Valid when isKey is false, verbatim text.
	};

	std::vector<Line> _lines;
	base::flat_map<QString, QString> _values;

};

[[nodiscard]] Config ReadEnv(const QString &path);
[[nodiscard]] bool WriteEnv(const QString &path, const Config &config);

#ifdef _DEBUG
void ConfigSelfCheck();
#endif // _DEBUG

} // namespace Pintopfp
