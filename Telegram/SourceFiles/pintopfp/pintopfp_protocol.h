/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <optional>

class QString;

namespace Pintopfp {

struct Stats {
	int set24h = 0;
	int set4h = 0;
	std::optional<int> softLeft24h;
	std::optional<int> softLeft4h;
};

struct Event {
	enum class Type { Log, StatusRunning, StatusStopped, Stats, Flood };
	Type type = Type::Log;
	QString log;            // for Type::Log
	Stats stats;            // for Type::Stats
	int floodSeconds = 0;   // for Type::Flood
};

[[nodiscard]] Event ParseLine(const QString &line);

#ifdef _DEBUG
void ProtocolSelfCheck();
#endif // _DEBUG

} // namespace Pintopfp
