#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <string>
#include <vector>

// Standalone reimplementation of the UPnP DIDL-Lite parser + SOAP body builder
// in chatgpt/src/upnp_client.cpp (parseDIDL / xmlTag / xmlAttr / xmlDecode /
// resolveUrl / parseResAll / pickBestRes and the BrowseDirectChildren vs
// BrowseMetadata flag). Production runs on the ESP32 against Gerbera/MiniDLNA;
// here we test the pure extraction logic: metadata fields (incl.
// albumArtist/composer), containers vs tracks, URL absolutization, the
// multi-<res> transcoding selection and the BrowseMetadata flag selection.

namespace {

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    std::string out;
    size_t p = 0;
    while (true) {
        size_t f = s.find(from, p);
        if (f == std::string::npos) { out += s.substr(p); break; }
        out += s.substr(p, f - p) + to;
        p = f + from.size();
    }
    return out;
}

// Mirrors xmlDecode() in upnp_client.cpp (order matters: &amp; first).
void xmlDecode(std::string& s) {
    s = replaceAll(s, "&amp;",  "&");
    s = replaceAll(s, "&lt;",   "<");
    s = replaceAll(s, "&gt;",   ">");
    s = replaceAll(s, "&quot;", "\"");
    s = replaceAll(s, "&apos;", "'");
}

// Mirrors xmlTag(): first occurrence of <tag>...</tag> (namespace tags OK).
std::string xmlTag(const std::string& xml, const std::string& tag) {
    std::string open  = "<" + tag + ">";
    std::string close = "</" + tag + ">";
    size_t s = xml.find(open);
    if (s == std::string::npos) return std::string();
    s += open.size();
    size_t e = xml.find(close, s);
    if (e == std::string::npos) return std::string();
    return xml.substr(s, e - s);
}

// Mirrors xmlAttr(): attr="value" or attr='value' in an opening tag.
std::string xmlAttr(const std::string& tag, const std::string& attr) {
    std::string key = attr + "=";
    size_t p = tag.find(key);
    if (p == std::string::npos) return std::string();
    p += key.size();
    if (p >= tag.size()) return std::string();
    char q = tag[p];
    if (q != '"' && q != '\'') return std::string();
    p++;
    size_t e = tag.find(q, p);
    if (e == std::string::npos) return std::string();
    return tag.substr(p, e - p);
}

// Mirrors resolveUrl(): relative controlURL / res / albumArtURI against base.
std::string resolveUrl(const std::string& base, const std::string& rel) {
    if (rel.rfind("http://", 0) == 0 || rel.rfind("https://", 0) == 0) return rel;
    size_t proto = base.find("://");
    if (proto == std::string::npos) return rel;
    size_t slash = base.find('/', proto + 3);
    if (slash == std::string::npos) return base + rel;
    if (rel.rfind("/", 0) == 0) return base.substr(0, slash) + rel;
    size_t lastSlash = base.rfind('/');
    if (lastSlash == std::string::npos || lastSlash < slash) return base.substr(0, slash) + "/" + rel;
    return base.substr(0, lastSlash + 1) + rel;
}

struct ResInfo {
    std::string url, mimeType, duration;
    bool transcoded = false;  // DLNA.ORG_CI=1 → server-side converted copy
};

static const int kMaxResPerItem = 4;

// Mirrors parseResAll() in upnp_client.cpp: every <res>...</res> of the item,
// keeping only audio resources (image/video extras skipped).
void parseResAll(const std::string& itemXml, ResInfo* out, int& count) {
    count = 0;
    size_t pos = 0;
    while (count < kMaxResPerItem) {
        size_t rs = itemXml.find("<res", pos);
        if (rs == std::string::npos) break;
        size_t re = itemXml.find('>', rs);
        if (re == std::string::npos) break;
        std::string resTag = itemXml.substr(rs, re + 1 - rs);
        size_t textEnd = itemXml.find("</res>", re);
        if (textEnd == std::string::npos) break;
        std::string url = itemXml.substr(re + 1, textEnd - re - 1);
        size_t b = url.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) url.clear();
        else url = url.substr(b, url.find_last_not_of(" \t\r\n") - b + 1);

        std::string protocolInfo = xmlAttr(resTag, "protocolInfo");
        bool transcoded = protocolInfo.find("DLNA.ORG_CI=1") != std::string::npos;
        std::string mimeType = protocolInfo;
        size_t c1 = mimeType.find(':');
        if (c1 != std::string::npos) {
            size_t c2 = mimeType.find(':', c1 + 1);
            if (c2 != std::string::npos) {
                size_t c3 = mimeType.find(':', c2 + 1);
                mimeType = (c3 != std::string::npos)
                               ? mimeType.substr(c2 + 1, c3 - c2 - 1)
                               : mimeType.substr(c2 + 1);
            }
        }
        std::string duration = xmlAttr(resTag, "duration");

        pos = textEnd + 6;  // skip "</res>"
        if (!mimeType.empty() && mimeType.rfind("audio/", 0) != 0) continue;

        out[count].url        = url;
        out[count].mimeType   = mimeType;
        out[count].duration   = duration;
        out[count].transcoded = transcoded;
        count++;
    }
}

// Mirrors isSupportedMime() in upnp_client.cpp (bundled MP3/AAC/FLAC decoders).
bool isSupportedMime(const std::string& mime) {
    if (mime.empty()) return false;
    return mime.rfind("audio/mpeg", 0) == 0 || mime.rfind("audio/mp3", 0) == 0 ||
           mime.rfind("audio/aac", 0) == 0  || mime.rfind("audio/flac", 0) == 0 ||
           mime.rfind("audio/x-aac", 0) == 0 || mime.rfind("audio/x-flac", 0) == 0;
}

// Mirrors pickBestRes() in upnp_client.cpp: original (CI=0) decodable wins,
// then transcoded decodable, then MIME-less (auto-detect), else first but
// flagged not playable.
void pickBestRes(const ResInfo* res, int count, int& index, bool& playable) {
    index = -1; playable = false;
    if (count <= 0) return;
    int original = -1, transcoded = -1, unknown = -1;
    for (int i = 0; i < count; i++) {
        if (res[i].mimeType.empty()) { if (unknown < 0) unknown = i; continue; }
        if (!isSupportedMime(res[i].mimeType)) continue;
        if (!res[i].transcoded && original < 0) original = i;
        if (res[i].transcoded && transcoded < 0) transcoded = i;
    }
    if (original >= 0)   { index = original;   playable = true; return; }
    if (transcoded >= 0) { index = transcoded; playable = true; return; }
    if (unknown >= 0)    { index = unknown;    playable = true; return; }
    index = 0;  // only declared-unsupported resources
}

struct Item {
    std::string id, title, artist, album, albumArtist, composer, genre;
    std::string date, art, url, mime, duration;
    bool isContainer = false;
    bool playable = false;
    int  childCount = -1;
    int  resCount = -1;
    int  track = -1;
};

// Mirrors parseDIDL() in upnp_client.cpp for the decoded <Result> content.
// Extracts dc:title, upnp:artist, upnp:albumArtist, upnp:composer,
// upnp:album, upnp:genre, upnp:originalTrackNumber, dc:date,
// upnp:albumArtURI and the best playable res (URL/MIME/duration, via
// parseResAll + pickBestRes → resCount/playable). Containers keep childCount
// and their own upnp:albumArtURI (some servers include it in the children).
void parseDIDL(const std::string& result, const std::string& baseLocation,
               std::vector<Item>& items) {
    size_t pos = 0;
    while (pos < result.size()) {
        bool isContainer = false;
        size_t tagStart  = result.find("<container", pos);
        size_t itemStart = result.find("<item", pos);

        if (tagStart == std::string::npos && itemStart == std::string::npos) break;
        if (tagStart != std::string::npos &&
            (itemStart == std::string::npos || tagStart < itemStart)) {
            isContainer = true;
            pos = tagStart;
        } else {
            pos = itemStart;
        }

        size_t openEnd = result.find('>', pos);
        if (openEnd == std::string::npos) break;
        std::string openTag = result.substr(pos, openEnd + 1 - pos);

        const char* closeTag = isContainer ? "</container>" : "</item>";
        size_t closePos = result.find(closeTag, openEnd);
        if (closePos == std::string::npos) { pos = openEnd + 1; continue; }
        std::string block = result.substr(pos, closePos + std::string(closeTag).size() - pos);

        Item it;
        it.id    = xmlAttr(openTag, "id");
        it.title = xmlTag(block, "dc:title");
        xmlDecode(it.title);
        if (it.title.empty()) it.title = it.id;
        it.isContainer = isContainer;

        if (!isContainer) {
            it.artist = xmlTag(block, "upnp:artist");  xmlDecode(it.artist);
            it.album  = xmlTag(block, "upnp:album");   xmlDecode(it.album);
            it.albumArtist = xmlTag(block, "upnp:albumArtist"); xmlDecode(it.albumArtist);
            it.composer     = xmlTag(block, "upnp:composer");   xmlDecode(it.composer);
            it.genre  = xmlTag(block, "upnp:genre");   xmlDecode(it.genre);
            std::string track = xmlTag(block, "upnp:originalTrackNumber");
            it.track = track.empty() ? -1 : atoi(track.c_str());
            it.date  = xmlTag(block, "dc:date");
            it.art   = xmlTag(block, "upnp:albumArtURI");
            xmlDecode(it.art);
            if (!it.art.empty()) it.art = resolveUrl(baseLocation, it.art);

            ResInfo resArr[kMaxResPerItem];
            int  resCount = 0, resIdx = -1;
            bool resPlayable = false;
            parseResAll(block, resArr, resCount);
            pickBestRes(resArr, resCount, resIdx, resPlayable);
            it.resCount = resCount;
            it.playable = resPlayable;
            if (resIdx >= 0) {
                if (!resArr[resIdx].url.empty()) it.url = resolveUrl(baseLocation, resArr[resIdx].url);
                it.mime = resArr[resIdx].mimeType;
                it.duration = resArr[resIdx].duration;
            }
        } else {
            std::string cc = xmlAttr(openTag, "childCount");
            if (!cc.empty()) it.childCount = atoi(cc.c_str());
            it.art = xmlTag(block, "upnp:albumArtURI");
            xmlDecode(it.art);
            if (!it.art.empty()) it.art = resolveUrl(baseLocation, it.art);
        }

        items.push_back(it);
        pos = closePos + std::string(closeTag).size();
    }
}

// Mirrors soapBrowse(): BrowseMetadata forces StartingIndex/RequestedCount = 0.
std::string soapBody(const std::string& objectId, int startIndex, int requestedCount,
                     bool metadata) {
    if (metadata) { startIndex = 0; requestedCount = 0; }
    std::string body = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
        " s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:Browse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
        "<ObjectID>" + objectId + "</ObjectID><BrowseFlag>" +
        (metadata ? "BrowseMetadata" : "BrowseDirectChildren") +
        "</BrowseFlag><Filter>*</Filter><StartingIndex>" +
        std::to_string(startIndex) +
        "</StartingIndex><RequestedCount>" +
        std::to_string(requestedCount) +
        "</RequestedCount><SortCriteria></SortCriteria>"
        "</u:Browse></s:Body></s:Envelope>";
    return body;
}

const char* kTrackResult =
    "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">"
    "<item id=\"track_42\" parentID=\"album_7\" restricted=\"1\">"
    "<dc:title>Summer &amp; Breeze</dc:title>"
    "<upnp:artist>Seals and Crofts</upnp:artist>"
    "<upnp:albumArtist>Seals and Crofts</upnp:albumArtist>"
    "<upnp:composer>Jim Seals</upnp:composer>"
    "<upnp:album>Diamond Girl</upnp:album>"
    "<upnp:genre>Soft Rock</upnp:genre>"
    "<upnp:originalTrackNumber>7</upnp:originalTrackNumber>"
    "<dc:date>1973-01-01</dc:date>"
    "<upnp:albumArtURI>art/track_42.jpg</upnp:albumArtURI>"
    "<res protocolInfo=\"http-get:*:audio/mpeg:*\" duration=\"0:03:42.000\">"
    "http://192.168.1.10:49152/content/media/track_42.mp3</res>"
    "</item>"
    "</DIDL-Lite>";

} // namespace

TEST_CASE("upnp didl: full track metadata (artist/album/albumArtist/composer/res)") {
    std::vector<Item> items;
    parseDIDL(kTrackResult, "http://192.168.1.10:49152/desc.xml", items);

    REQUIRE(items.size() == 1);
    CHECK(!items[0].isContainer);
    CHECK(items[0].id == "track_42");
    CHECK(items[0].title == "Summer & Breeze");      // &amp; decoded
    CHECK(items[0].artist == "Seals and Crofts");
    CHECK(items[0].albumArtist == "Seals and Crofts");
    CHECK(items[0].composer == "Jim Seals");
    CHECK(items[0].album == "Diamond Girl");
    CHECK(items[0].genre == "Soft Rock");
    CHECK(items[0].track == 7);
    CHECK(items[0].date == "1973-01-01");
    CHECK(items[0].url == "http://192.168.1.10:49152/content/media/track_42.mp3");
    CHECK(items[0].mime == "audio/mpeg");
    CHECK(items[0].duration == "0:03:42.000");
    CHECK(items[0].resCount == 1);
    CHECK(items[0].playable);
    // relative albumArtURI resolved against the device description base
    CHECK(items[0].art == "http://192.168.1.10:49152/art/track_42.jpg");
}

TEST_CASE("upnp didl: albumArtist/composer absent -> fields simply omitted") {
    std::string minimal =
        "<item id=\"t1\"><dc:title>Plain Track</dc:title>"
        "<res protocolInfo=\"http-get:*:audio/flac:*\">http://host/plain.flac</res></item>";
    std::vector<Item> items;
    parseDIDL(minimal, "http://host/", items);

    REQUIRE(items.size() == 1);
    CHECK(items[0].title == "Plain Track");
    CHECK(items[0].artist.empty());
    CHECK(items[0].albumArtist.empty());
    CHECK(items[0].composer.empty());
    CHECK(items[0].url == "http://host/plain.flac");
    CHECK(items[0].mime == "audio/flac");
    CHECK(items[0].resCount == 1);
    CHECK(items[0].playable);
}

TEST_CASE("upnp didl: container vs track classification + childCount") {
    std::string mixed =
        "<container id=\"album_7\" childCount=\"12\" restricted=\"1\">"
        "<dc:title>Diamond Girl</dc:title></container>"
        "<item id=\"t1\"><dc:title>Track One</dc:title>"
        "<res>http://host/one.mp3</res></item>";
    std::vector<Item> items;
    parseDIDL(mixed, "http://host/", items);

    REQUIRE(items.size() == 2);
    CHECK(items[0].isContainer);
    CHECK(items[0].id == "album_7");
    CHECK(items[0].title == "Diamond Girl");
    CHECK(items[0].childCount == 12);
    CHECK(items[0].art.empty());  // no upnp:albumArtURI in this container
    CHECK(items[1].isContainer == false);
    CHECK(items[1].title == "Track One");
    CHECK(items[1].url == "http://host/one.mp3");
}

TEST_CASE("upnp didl: container album art (upnp:albumArtURI) resolved") {
    std::string doc =
        "<container id=\"album_7\" childCount=\"12\" restricted=\"1\">"
        "<dc:title>Diamond Girl</dc:title>"
        "<upnp:albumArtURI>covers/album_7.jpg</upnp:albumArtURI>"
        "</container>"
        "<container id=\"album_9\" childCount=\"3\">"
        "<dc:title>No Art &amp; More</dc:title>"
        "</container>";
    std::vector<Item> items;
    parseDIDL(doc, "http://192.168.1.10:49152/desc.xml", items);

    REQUIRE(items.size() == 2);
    CHECK(items[0].isContainer);
    CHECK(items[0].id == "album_7");
    CHECK(items[0].childCount == 12);
    // relative albumArtURI resolved against the device description base
    CHECK(items[0].art == "http://192.168.1.10:49152/covers/album_7.jpg");
    CHECK(items[1].isContainer);
    CHECK(items[1].id == "album_9");
    CHECK(items[1].title == "No Art & More");
    CHECK(items[1].art.empty());
}

TEST_CASE("upnp didl: absolute container albumArtURI kept verbatim") {
    std::string doc =
        "<container id=\"art0\" childCount=\"4\">"
        "<dc:title>Absolute Art</dc:title>"
        "<upnp:albumArtURI>http://cdn.example.net/art/band.jpg</upnp:albumArtURI>"
        "</container>";
    std::vector<Item> items;
    parseDIDL(doc, "http://host/", items);

    REQUIRE(items.size() == 1);
    CHECK(items[0].isContainer);
    CHECK(items[0].art == "http://cdn.example.net/art/band.jpg");
}

TEST_CASE("upnp didl: transcoded copy picked when the original is not decodable") {
    // OGG original (unsupported) + MP3 transcode (DLNA.ORG_CI=1, relative URL)
    std::string doc =
        "<item id=\"t1\"><dc:title>OGG Track</dc:title>"
        "<res protocolInfo=\"http-get:*:audio/ogg:DLNA.ORG_CI=0\">http://host/orig/track.ogg</res>"
        "<res protocolInfo=\"http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_CI=1\" duration=\"0:03:42.000\">transcoded/track.mp3</res>"
        "</item>";
    std::vector<Item> items;
    parseDIDL(doc, "http://host/", items);

    REQUIRE(items.size() == 1);
    CHECK(items[0].resCount == 2);
    CHECK(items[0].playable);
    // chosen = the transcoded MP3, and its relative URL is absolutized
    CHECK(items[0].url == "http://host/transcoded/track.mp3");
    CHECK(items[0].mime == "audio/mpeg");
    CHECK(items[0].duration == "0:03:42.000");
}

TEST_CASE("upnp didl: decodable original (CI=0) wins over a transcoded copy") {
    std::string doc =
        "<item id=\"t1\"><dc:title>FLAC Track</dc:title>"
        "<res protocolInfo=\"http-get:*:audio/flac:*\" duration=\"0:04:00.000\">http://host/a.flac</res>"
        "<res protocolInfo=\"http-get:*:audio/mpeg:DLNA.ORG_CI=1\">http://host/transcoded/a.mp3</res>"
        "</item>";
    std::vector<Item> items;
    parseDIDL(doc, "http://host/", items);

    REQUIRE(items.size() == 1);
    CHECK(items[0].resCount == 2);
    CHECK(items[0].playable);
    CHECK(items[0].url == "http://host/a.flac");
    CHECK(items[0].mime == "audio/flac");
    CHECK(items[0].duration == "0:04:00.000");
}

TEST_CASE("upnp didl: non-audio res (image/video) ignored, not counted") {
    std::string doc =
        "<item id=\"t1\"><dc:title>With Cover</dc:title>"
        "<res protocolInfo=\"http-get:*:image/jpeg:*\">http://host/cover.jpg</res>"
        "<res protocolInfo=\"http-get:*:video/mp4:*\">http://host/vid.mp4</res>"
        "<res protocolInfo=\"http-get:*:audio/mpeg:*\">http://host/t.mp3</res>"
        "</item>";
    std::vector<Item> items;
    parseDIDL(doc, "http://host/", items);

    REQUIRE(items.size() == 1);
    CHECK(items[0].resCount == 1);
    CHECK(items[0].playable);
    CHECK(items[0].url == "http://host/t.mp3");
}

TEST_CASE("upnp didl: no decodable resource -> playable false (greyed in UI)") {
    std::string doc =
        "<item id=\"t1\"><dc:title>WMA Track</dc:title>"
        "<res protocolInfo=\"http-get:*:audio/x-ms-wma:*\">http://host/t.wma</res></item>";
    std::vector<Item> items;
    parseDIDL(doc, "http://host/", items);

    REQUIRE(items.size() == 1);
    CHECK(items[0].resCount == 1);
    CHECK(items[0].playable == false);
    CHECK(items[0].url == "http://host/t.wma");  // url kept for display/debug
    CHECK(items[0].mime == "audio/x-ms-wma");
}

TEST_CASE("upnp didl: res without protocolInfo -> playable (decoder auto-detects)") {
    std::string doc =
        "<item id=\"t1\"><dc:title>No Mime</dc:title>"
        "<res>http://host/t</res></item>";
    std::vector<Item> items;
    parseDIDL(doc, "http://host/", items);

    REQUIRE(items.size() == 1);
    CHECK(items[0].resCount == 1);
    CHECK(items[0].playable);
    CHECK(items[0].url == "http://host/t");
}

TEST_CASE("upnp didl: BrowseMetadata single-object Result yields one item") {
    // Gerbera/MiniDLNA return the object itself (container) for BrowseMetadata.
    std::string meta =
        "<container id=\"album_7\" childCount=\"12\">"
        "<dc:title>Diamond Girl</dc:title></container>";
    std::vector<Item> items;
    parseDIDL(meta, "http://host/", items);

    REQUIRE(items.size() == 1);
    CHECK(items[0].isContainer);
    CHECK(items[0].id == "album_7");
    CHECK(items[0].childCount == 12);
}

TEST_CASE("upnp soap: BrowseMetadata flag forces 0/0, children keep paging") {
    auto meta = soapBody("album_7", 5, 20, true);
    CHECK(meta.find("<BrowseFlag>BrowseMetadata</BrowseFlag>") != std::string::npos);
    CHECK(meta.find("<StartingIndex>0</StartingIndex>") != std::string::npos);
    CHECK(meta.find("<RequestedCount>0</RequestedCount>") != std::string::npos);
    CHECK(meta.find("<ObjectID>album_7</ObjectID>") != std::string::npos);

    auto kids = soapBody("album_7", 5, 20, false);
    CHECK(kids.find("<BrowseFlag>BrowseDirectChildren</BrowseFlag>") != std::string::npos);
    CHECK(kids.find("<StartingIndex>5</StartingIndex>") != std::string::npos);
    CHECK(kids.find("<RequestedCount>20</RequestedCount>") != std::string::npos);
}
