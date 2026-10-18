#pragma once
#include "../http_abstraction.h"
#include "../../feature_flags.h"

#if ENABLE_UDA1334A==1

void handleUda1334aConfigGet(HttpRequest& req, HttpResponse& res);
void handleUda1334aConfigPost(HttpRequest& req, HttpResponse& res);
void handleUda1334aTestPost(HttpRequest& req, HttpResponse& res);
void handleUda1334aTimingPost(HttpRequest& req, HttpResponse& res);
void handleI2sPocPost(HttpRequest& req, HttpResponse& res);
void handleMp3TestPost(HttpRequest& req, HttpResponse& res);

void handleRadioStatusGet(HttpRequest& req, HttpResponse& res);
void handleRadioQueueGet(HttpRequest& req, HttpResponse& res);
void handleRadioFavoritesGet(HttpRequest& req, HttpResponse& res);
void handleRadioStopPost(HttpRequest& req, HttpResponse& res);
void handleRadioNextPost(HttpRequest& req, HttpResponse& res);
void handleRadioRandomPost(HttpRequest& req, HttpResponse& res);
void handleRadioVolumePost(HttpRequest& req, HttpResponse& res);
void handleRadioSeekPost(HttpRequest& req, HttpResponse& res);
void handleRadioPlayUrlPost(HttpRequest& req, HttpResponse& res);
void handleRadioPlaySdPost(HttpRequest& req, HttpResponse& res);
void handleRadioQueuePost(HttpRequest& req, HttpResponse& res);
void handleRadioFavoritesAddPost(HttpRequest& req, HttpResponse& res);
void handleRadioFavoritesRemovePost(HttpRequest& req, HttpResponse& res);
void handleRadioFavoritesPlayPost(HttpRequest& req, HttpResponse& res);

// New feature handlers
void handleRadioArtworkGet(HttpRequest& req, HttpResponse& res);
void handleRadioSearchGet(HttpRequest& req, HttpResponse& res);
void handleRadioPlaylistsGet(HttpRequest& req, HttpResponse& res);
void handleRadioPlaylistSavePost(HttpRequest& req, HttpResponse& res);
void handleRadioPlaylistLoadPost(HttpRequest& req, HttpResponse& res);
void handleRadioPlaylistDeletePost(HttpRequest& req, HttpResponse& res);
void handleRadioShufflePost(HttpRequest& req, HttpResponse& res);
void handleRadioRepeatPost(HttpRequest& req, HttpResponse& res);
void handleRadioResumeGet(HttpRequest& req, HttpResponse& res);
void handleRadioResumePost(HttpRequest& req, HttpResponse& res);
void handleRadioSleepPost(HttpRequest& req, HttpResponse& res);
void handleRadioEqGet(HttpRequest& req, HttpResponse& res);
void handleRadioEqPost(HttpRequest& req, HttpResponse& res);
void handleRadioScheduleGet(HttpRequest& req, HttpResponse& res);
void handleRadioSchedulePost(HttpRequest& req, HttpResponse& res);

// Online station discovery (radio-browser.info proxy)
void handleRadioStationsSearchGet(HttpRequest& req, HttpResponse& res);
void handleRadioStationsTagsGet(HttpRequest& req, HttpResponse& res);
void handleRadioStationsCountriesGet(HttpRequest& req, HttpResponse& res);
void handleRadioStationsDefaultGet(HttpRequest& req, HttpResponse& res);

// Library folder listing
void handleRadioLibraryGet(HttpRequest& req, HttpResponse& res);
// Library folder cover image (GET /api/radio/library/art?path=...)
void handleRadioLibraryArtGet(HttpRequest& req, HttpResponse& res);

// Play/pause toggle for the currently selected source
void handleRadioTogglePost(HttpRequest& req, HttpResponse& res);

// UPnP / DLNA client
void handleUpnpServersGet(HttpRequest& req, HttpResponse& res);
void handleUpnpBrowseGet(HttpRequest& req, HttpResponse& res);
void handleUpnpDiscoverPost(HttpRequest& req, HttpResponse& res);

#endif
