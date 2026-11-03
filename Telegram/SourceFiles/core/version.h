/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/const_string.h"

#define TDESKTOP_REQUESTED_ALPHA_VERSION (0ULL)

#ifdef TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION TDESKTOP_REQUESTED_ALPHA_VERSION
#else // TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION (0ULL)
#endif // TDESKTOP_ALLOW_CLOSED_ALPHA

// used in Updater.cpp and Setup.iss for Windows
constexpr auto AppId = "{8DB8830E-2B82-4B9B-8779-410C74849E42}"_cs;
constexpr auto AppNameOld = "HoursGram"_cs;
constexpr auto AppName = "HoursGram Desktop"_cs;
constexpr auto AppFile = "HoursGram"_cs;
constexpr auto AppVersion = 1000000;
constexpr auto AppVersionStr = "1.0.0";

// The version of Telegram Desktop this build is made from. It is what the
// servers are told, since they turn some features on by the version of the
// client, and our own 1.0.0 would look to them like a client from 2017.
constexpr auto BaseAppVersionStr = "7.3.4";
constexpr auto AppBetaVersion = false;
constexpr auto AppAlphaVersion = TDESKTOP_ALPHA_VERSION;
