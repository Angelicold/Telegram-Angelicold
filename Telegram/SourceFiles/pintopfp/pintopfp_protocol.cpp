/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "pintopfp/pintopfp_protocol.h"

#include "base/assertion.h"

namespace Pintopfp {

Event ParseLine(const QString &line) {
	auto event = Event();
	if (line == u"@STATUS running"_q) {
		event.type = Event::Type::StatusRunning;
	} else if (line == u"@STATUS stopped"_q) {
		event.type = Event::Type::StatusStopped;
	} else if (line.startsWith(u"@STATS "_q)) {
		event.type = Event::Type::Stats;
		const auto parts = line.mid(7).split(' ', Qt::SkipEmptyParts);
		for (const auto &p : parts) {
			const auto eq = p.indexOf('=');
			if (eq < 0) {
				continue;
			}
			const auto key = p.left(eq);
			const auto value = p.mid(eq + 1);
			const auto opt = (value == u"-"_q)
				? std::optional<int>()
				: std::optional<int>(value.toInt());
			if (key == u"set24h"_q) {
				event.stats.set24h = value.toInt();
			} else if (key == u"set4h"_q) {
				event.stats.set4h = value.toInt();
			} else if (key == u"softLeft24h"_q) {
				event.stats.softLeft24h = opt;
			} else if (key == u"softLeft4h"_q) {
				event.stats.softLeft4h = opt;
			}
		}
	} else if (line.startsWith(u"@FLOOD seconds="_q)) {
		event.type = Event::Type::Flood;
		event.floodSeconds = line.mid(15).toInt();
	} else {
		event.type = Event::Type::Log;
		event.log = line;
	}
	return event;
}

#ifdef _DEBUG
void ProtocolSelfCheck() {
	{
		const auto event = ParseLine(u"@FLOOD seconds=1941"_q);
		Assert(event.type == Event::Type::Flood);
		Assert(event.floodSeconds == 1941);
	}
	{
		const auto event = ParseLine(u"@STATS set24h=3 set4h=1 softLeft24h=- softLeft4h=4"_q);
		Assert(event.type == Event::Type::Stats);
		Assert(event.stats.set24h == 3);
		Assert(event.stats.set4h == 1);
		Assert(!event.stats.softLeft24h.has_value());
		Assert(event.stats.softLeft4h == 4);
	}
	{
		const auto event = ParseLine(u"@STATUS running"_q);
		Assert(event.type == Event::Type::StatusRunning);
	}
	{
		const auto event = ParseLine(u"@STATUS stopped"_q);
		Assert(event.type == Event::Type::StatusStopped);
	}
	{
		const auto event = ParseLine(u"hello world"_q);
		Assert(event.type == Event::Type::Log);
		Assert(event.log == u"hello world"_q);
	}
}
#endif // _DEBUG

} // namespace Pintopfp
