#pragma once

#include "app_config.hpp"

namespace settings_store {

// Resolves "/private/id_rsa", "sd:/private/id_rsa", or "id_rsa" to POSIX VFS "/sdcard/...".
String resolveSdKeyPath(const String& rawPath);

// Serializes AppConfig to JSON and parses JSON back into AppConfig.
String serializeConfigJson(const AppConfig& config);
bool deserializeConfigJson(AppConfig& config, const String& jsonText);

// Parses Console / Tab5-Terminal / Tab5-Shell SD artifacts and appends unique SSH profiles.
// Returns the number of newly added profiles.
int importTerminalJsonText(AppConfig& config, const String& jsonText);
int importShellProfilesText(AppConfig& config, const String& text);

// Normalizes profile order/indices cleanly.
void clampSelection(AppConfig& config);

// Adds or updates a profile matching name or user@host:port; returns index or -1 if invalid.
int upsertSshProfile(AppConfig& config, const SshProfile& profile, bool selectAsActive = true);

// Removes a stale SSH profile by 0-based index, profile name, user@host, or host.
bool removeSshProfile(AppConfig& config, const String& selector);
void clearSshProfiles(AppConfig& config);

#ifdef ESP_PLATFORM
bool loadConfig(AppConfig& config, String& sourceNote);
bool saveConfig(const AppConfig& config, String& error);
int syncSdSshProfiles(AppConfig& config, bool replaceExisting = false);
#endif

}  // namespace settings_store
