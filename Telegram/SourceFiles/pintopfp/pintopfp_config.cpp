/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "pintopfp/pintopfp_config.h"

#include "base/assertion.h"

#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtCore/QStringList>

namespace Pintopfp {

QString Config::value(const QString &key) const {
	const auto i = _values.find(key);
	return (i != _values.end()) ? i->second : QString();
}

void Config::set(const QString &key, const QString &value) {
	const auto isNew = (_values.find(key) == _values.end());
	_values[key] = value;
	if (isNew) {
		_lines.push_back(Line{ .isKey = true, .key = key });
	}
}

Config ParseEnvText(const QString &text) {
	static const auto kPattern = QRegularExpression(
		u"^([A-Z_][A-Z0-9_]*)=(.*)$"_q);

	auto config = Config();
	const auto lines = text.split(u'\n');
	for (const auto &line : lines) {
		const auto match = kPattern.match(line);
		if (match.hasMatch()) {
			const auto key = match.captured(1);
			config._values[key] = match.captured(2);
			config._lines.push_back(Config::Line{ .isKey = true, .key = key });
		} else {
			config._lines.push_back(Config::Line{ .isKey = false, .raw = line });
		}
	}
	return config;
}

QString SerializeEnv(const Config &config) {
	auto lines = QStringList();
	lines.reserve(int(config._lines.size()));
	for (const auto &line : config._lines) {
		if (line.isKey) {
			const auto i = config._values.find(line.key);
			const auto value = (i != config._values.end()) ? i->second : QString();
			lines.push_back(line.key + u'=' + value);
		} else {
			lines.push_back(line.raw);
		}
	}
	return lines.join(u'\n');
}

Config ReadEnv(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return Config();
	}
	return ParseEnvText(QString::fromUtf8(file.readAll()));
}

bool WriteEnv(const QString &path, const Config &config) {
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	const auto bytes = SerializeEnv(config).toUtf8();
	return file.write(bytes) == bytes.size();
}

#ifdef _DEBUG
void ConfigSelfCheck() {
	const auto sample = u"# pintopfp config\nAPI_ID=123\n\nPOLL_INTERVAL=180\n"_q;
	{
		const auto config = ParseEnvText(sample);
		Assert(SerializeEnv(config) == sample);
	}
	{
		auto config = ParseEnvText(sample);
		config.set(u"POLL_INTERVAL"_q, u"90"_q);
		const auto serialized = SerializeEnv(config);
		Assert(serialized
			== u"# pintopfp config\nAPI_ID=123\n\nPOLL_INTERVAL=90\n"_q);
		Assert(config.value(u"API_ID"_q) == u"123"_q);
	}
	{
		auto config = ParseEnvText(sample);
		config.set(u"NEW_KEY"_q, u"value"_q);
		const auto serialized = SerializeEnv(config);
		Assert(serialized
			== (sample + u"\nNEW_KEY=value"_q));
		Assert(config.value(u"NEW_KEY"_q) == u"value"_q);
	}
}
#endif // _DEBUG

} // namespace Pintopfp
