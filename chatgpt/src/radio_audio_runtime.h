#pragma once

#include "feature_flags.h"

#if ENABLE_UDA1334A == 1
#include <Arduino.h>
#include <vector>

void radioAudioRuntimeBegin();
void radioAudioRuntimeLoop();

// Cancel the boot-time auto-resume of the last active source (used by the
// weekly schedule when a window ended while the device was powered off).
void radioAudioCancelResume();

// Deferred actions — safe to call from async_tcp (web handlers).
// Actual work happens in the loop task, avoiding cross-task use-after-free.
void radioAudioStopDeferred();
void radioAudioPlayUrlDeferred(const String& url);
void radioAudioPlayUrlMimeDeferred(const String& url, const String& mime);
void radioAudioPlaySdDeferred(const String& path);
void radioAudioPlayNextDeferred();
void radioAudioPlayRandomDeferred();
void radioAudioPlayFavoriteDeferred(int index);
void radioAudioTogglePlayDeferred();
void radioAudioPlayEntryDeferred(const String& entry);

bool radioAudioPlayUrl(const String& url, String& messageOut);
bool radioAudioPlaySdPath(const String& path, String& messageOut);
bool radioAudioPlayFolder(const String& folder, String& messageOut);
bool radioAudioPlayNext(String& messageOut);
bool radioAudioPlayRandom(String& messageOut);
bool radioAudioTogglePlay(String& messageOut);
void radioAudioStop();
void radioAudioSetVolume(uint8_t value0to100);
bool radioAudioSeekPosition(float fraction);

bool radioAudioSetQueue(const std::vector<String>& items, int startIndex, String& messageOut);
String radioAudioQueueJson();

bool radioAudioAddFavorite(const String& name, const String& url, String& messageOut, const String& favicon = String());
bool radioAudioRemoveFavorite(int index, String& messageOut);
bool radioAudioPlayFavorite(int index, String& messageOut);
String radioAudioFavoritesJson();

String radioAudioStatusJson();
bool radioAudioIsActive();

// Metadata for currently playing stream (title, artist, artwork)
const char* radioAudioGetCurrentTitle();
const char* radioAudioGetCurrentArtist();
const char* radioAudioGetCurrentArtwork();

// Cover art — extracts APIC from current SD file, stores in PSRAM
const uint8_t* radioAudioGetArtworkData(uint32_t& outSize);
const char* radioAudioGetArtworkMime();
void radioAudioClearArtwork();

// Station artwork — favicon of the currently playing internet stream.
// The web UI passes it when playing a discovered station; the browser shows
// it directly in the spinning disc (falls back to APIC art / musical emoji).
void radioAudioSetStationFavicon(const char* url);
void radioAudioSetStationName(const char* name);
const char* radioAudioGetStationFavicon();

// Search — recursive SD scan for matching filenames
String radioAudioSearch(const char* query);

// Library — music files in the auto-created /library folder
// absFolder: "" = root (lists sub-folders + files), or an absolute /library path.
String radioAudioListLibrary(const String& absFolder);
// All music (recursive) under an absolute /library folder — for folder play.
String radioAudioListFolderMusic(const String& absFolder);
// Loads a cover image file (cover.jpg/folder.jpg) from under /library into a
// PSRAM buffer. Caller owns the buffer (release with heap_caps_free). MIME is
// written into mimeOut (mimeCap bytes).
bool radioAudioLoadLibraryArt(const String& absPath, const uint8_t*& dataOut,
                              uint32_t& sizeOut, char* mimeOut, size_t mimeCap);

// Online station search — proxies radio-browser.info (name/genre/country).
// Returns a simplified JSON array of results in jsonOut. limit is clamped 1..50.
// hasMoreOut (optional) is set when the upstream returned a full page (raw
// count >= limit before playability filtering), so the UI can offer "Next".
bool radioAudioSearchStations(const String& name, const String& genre,
                              const String& countrycode, int offset, int limit,
                              String& jsonOut, String& errorOut,
                              bool* hasMoreOut = nullptr);

// Genre/tag and country dropdown lists (radio-browser), cached ~24 h in PSRAM.
bool radioAudioGetTags(String& jsonOut, String& errorOut);
bool radioAudioGetCountries(String& jsonOut, String& errorOut);

// Default stations from the built-in queue (80s80s/90s90s), deduplicated.
String radioAudioDefaultStationsJson();

// Playlists — M3U save/load/list/delete
bool radioAudioSavePlaylist(const char* name, String& msg);
bool radioAudioLoadPlaylist(const char* name, String& msg);
String radioAudioListPlaylists();
bool radioAudioDeletePlaylist(const char* name, String& msg);

// Shuffle / Repeat modes
bool radioAudioGetShuffle();
void radioAudioSetShuffle(bool on);
uint8_t radioAudioGetRepeatMode(); // 0=none, 1=one, 2=all
void radioAudioSetRepeatMode(uint8_t mode);

// Auto-resume after boot: master switch and target (first of playlist vs the
// source that was playing). Persisted in the radio state file.
bool radioAudioGetResume(bool& autoResume, bool& resumeFirst);
void radioAudioSetResume(bool autoResume, bool resumeFirst);

// Equalizer — 5 bands, -12..+12 dB
uint8_t radioAudioGetEqBands(); // returns 5
int8_t radioAudioGetEqBandGain(uint8_t band);
bool radioAudioSetEqBandGain(uint8_t band, int8_t gainDB);

// Sleep timer — backend-enforced, persists across page refresh and reboot.
// minutes <= 0 disables. radioAudioSleepRemainingSec(): 0 off/expired, -1
// unknown (clock not synced yet after reboot), else seconds remaining.
void radioAudioSetSleep(int minutes);
int radioAudioSleepRemainingSec();
int radioAudioSleepMinutes();
// Async web handlers flag a pending persist; the loop task performs the actual
// LittleFS writes so they never race ConfigManager saves.
void radioAudioRequestPersist();
// State-only variant for high-frequency sliders (volume/EQ/sleep): debounced,
// and only the small state file is rewritten — never the queue/favorites files.
void radioAudioRequestPersistState();

#endif
