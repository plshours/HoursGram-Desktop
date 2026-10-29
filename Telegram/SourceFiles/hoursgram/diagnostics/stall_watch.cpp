/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/diagnostics/stall_watch.h"

#if defined Q_OS_WIN && defined _M_X64

#include "logs.h"

#include <QtCore/QString>

#include <atomic>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX
#include <windows.h>

namespace HoursGram::Diagnostics {
namespace {

constexpr auto kStallMs = 250;
constexpr auto kPollMs = 25;
constexpr auto kSamplesPerStall = 4;
constexpr auto kMaxReports = 60;
constexpr auto kMaxFrames = 40;

struct Sample {
	DWORD64 frames[kMaxFrames] = {};
	int count = 0;
};

struct State {
	std::atomic<qint64> started = 0;
	HANDLE mainThread = nullptr;
	bool running = false;
	int depth = 0;
};

[[nodiscard]] State &Current() {
	static auto result = State();
	return result;
}

[[nodiscard]] qint64 Now() {
	return qint64(GetTickCount64());
}

// Nothing here may allocate or take a lock the main thread could be holding,
// because the main thread is stopped while this runs.
[[nodiscard]] bool Capture(HANDLE thread, Sample &to) {
	if (SuspendThread(thread) == DWORD(-1)) {
		return false;
	}
	auto context = CONTEXT();
	context.ContextFlags = CONTEXT_FULL;
	if (GetThreadContext(thread, &context)) {
		while (to.count < kMaxFrames && context.Rip) {
			to.frames[to.count++] = context.Rip;
			auto imageBase = DWORD64(0);
			const auto entry = RtlLookupFunctionEntry(
				context.Rip,
				&imageBase,
				nullptr);
			if (!entry) {
				break;
			}
			PVOID handlerData = nullptr;
			auto establisher = DWORD64(0);
			RtlVirtualUnwind(
				UNW_FLAG_NHANDLER,
				imageBase,
				context.Rip,
				entry,
				&context,
				&handlerData,
				&establisher,
				nullptr);
		}
	}
	ResumeThread(thread);
	return (to.count > 0);
}

// The module a frame belongs to and the offset in it, which is what a .pdb
// is read by. Names are not looked up here: the symbols of this executable
// are too large to load into a running client.
[[nodiscard]] QString Location(DWORD64 address) {
	auto module = HMODULE();
	if (!GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
				| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(address),
			&module)) {
		return QString("?");
	}
	wchar_t name[MAX_PATH] = {};
	GetModuleFileNameW(module, name, MAX_PATH);
	const auto full = QString::fromWCharArray(name);
	const auto base = full.mid(full.lastIndexOf(QChar('\\')) + 1);
	return base + QString("+0x%1").arg(
		address - reinterpret_cast<DWORD64>(module),
		0,
		16);
}

void Watch() {
	auto &state = Current();
	auto reports = 0;
	auto reportedFor = qint64(0);
	auto samples = 0;
	while (true) {
		Sleep(kPollMs);
		const auto started = state.started.load();
		if (!started || reports >= kMaxReports) {
			continue;
		}
		if (started != reportedFor) {
			reportedFor = started;
			samples = 0;
		}
		const auto elapsed = Now() - started;
		if (samples >= kSamplesPerStall
			|| elapsed < qint64(kStallMs) * (samples + 1)) {
			continue;
		}
		auto sample = Sample();
		if (!Capture(state.mainThread, sample)) {
			continue;
		}
		++samples;
		++reports;
		auto text = QString();
		for (auto i = 0; i != sample.count; ++i) {
			text += QString("\n    ") + Location(sample.frames[i]);
		}
		LOG(("Stall: main thread busy for %1 ms, sample %2:%3")
			.arg(elapsed)
			.arg(samples)
			.arg(text));
	}
}

} // namespace

void EventStarted() {
	auto &state = Current();
	if (!state.running) {
		state.running = true;
		DuplicateHandle(
			GetCurrentProcess(),
			GetCurrentThread(),
			GetCurrentProcess(),
			&state.mainThread,
			THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT,
			FALSE,
			0);
		if (state.mainThread) {
			std::thread(Watch).detach();
		}
	}
	if (state.depth++ == 0) {
		state.started = Now();
	}
}

void EventFinished() {
	auto &state = Current();
	if (--state.depth == 0) {
		state.started = 0;
	}
}

} // namespace HoursGram::Diagnostics

#else // Q_OS_WIN && _M_X64

namespace HoursGram::Diagnostics {

void EventStarted() {
}

void EventFinished() {
}

} // namespace HoursGram::Diagnostics

#endif // Q_OS_WIN && _M_X64
