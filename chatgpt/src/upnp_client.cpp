#include "upnp_client.h"

#include "feature_flags.h"
#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "psram_helpers.h"
#include "commands.h"   // sendVirtualMessage / INFO / WARN / ERROR

// ── Constants ─────────────────────────────────────────────────────────────────
static const char* kSsdpAddr      = "239.255.255.250";
static const uint16_t kSsdpPort   = 1900;
static const char* kSsdpST        = "urn:schemas-upnp-org:device:MediaServer:1";
static const uint32_t kDiscoverMs = 3000;   // wait for M-SEARCH replies
static const uint8_t  kMaxServers = 6;
static const uint16_t kBrowsePageSize = 20; // items per SOAP request

// ── State machine ─────────────────────────────────────────────────────────────
enum UpnpState : uint8_t {
    UPNP_IDLE = 0,
    UPNP_DISCOVERING,
    UPNP_DESCRIBING,    // fetching device description XML
    UPNP_READY,
    UPNP_BROWSING,      // transient: a SOAP Browse is in flight (web task)
    UPNP_ERROR
};
static volatile UpnpState s_state = UPNP_IDLE;

// ── Server descriptor (all strings in PSRAM via String on PSRAM heap) ─────────
struct UpnpServer {
    String uuid;
    String friendlyName;
    String location;        // URL of device description XML
    String controlUrl;      // ContentDirectory controlURL (absolute)
    bool   described;       // true once controlUrl is resolved
};

// PSRAM-allocated server list
static UpnpServer* s_servers = nullptr;
static uint8_t     s_serverCount = 0;
static uint8_t     s_describeIdx = 0;  // next server to describe

static WiFiUDP     s_udp;
static uint32_t    s_discoverStartMs = 0;
static bool        s_started = false;

// ── PSRAM helpers ─────────────────────────────────────────────────────────────
static UpnpServer* psramAllocServers() {
    void* p = heap_caps_calloc(kMaxServers, sizeof(UpnpServer), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_calloc(kMaxServers, sizeof(UpnpServer), MALLOC_CAP_8BIT);
    if (!p) return nullptr;
    // placement-new each element so String members are properly constructed
    UpnpServer* arr = reinterpret_cast<UpnpServer*>(p);
    for (uint8_t i = 0; i < kMaxServers; i++) new (&arr[i]) UpnpServer();
    return arr;
}

static void psramFreeServers(UpnpServer* arr) {
    if (!arr) return;
    for (uint8_t i = 0; i < kMaxServers; i++) arr[i].~UpnpServer();
    heap_caps_free(arr);
}

// ── XML helpers (no heap allocation — scan the raw string) ────────────────────

// Extract the text content of the first occurrence of <tag>...</tag>.
// Works on both regular and namespace-prefixed tags (e.g. dc:title).
static String xmlTag(const String& xml, const char* tag) {
    String open  = String('<') + tag + '>';
    String close = String("</") + tag + '>';
    int s = xml.indexOf(open);
    if (s < 0) return String();
    s += open.length();
    int e = xml.indexOf(close, s);
    if (e < s) return String();
    return xml.substring(s, e);
}

// Extract an XML attribute value: attr="value" or attr='value'.
static String xmlAttr(const String& tag, const char* attr) {
    String key = String(attr) + '=';
    int p = tag.indexOf(key);
    if (p < 0) return String();
    p += key.length();
    if (p >= (int)tag.length()) return String();
    char q = tag[p];
    if (q != '"' && q != '\'') return String();
    p++;
    int e = tag.indexOf(q, p);
    if (e < p) return String();
    return tag.substring(p, e);
}

// Resolve a potentially relative URL against a base URL.
static String resolveUrl(const String& base, const String& rel) {
    if (rel.startsWith("http://") || rel.startsWith("https://")) return rel;
    // base = "http://host:port/path/desc.xml"
    int proto = base.indexOf("://");
    if (proto < 0) return rel;
    int slash = base.indexOf('/', proto + 3);
    if (slash < 0) return base + rel;
    if (rel.startsWith("/")) return base.substring(0, slash) + rel;
    int lastSlash = base.lastIndexOf('/');
    if (lastSlash < slash) return base.substring(0, slash) + '/' + rel;
    return base.substring(0, lastSlash + 1) + rel;
}

// Decode common XML entities in-place.
static void xmlDecode(String& s) {
    s.replace("&amp;",  "&");
    s.replace("&lt;",   "<");
    s.replace("&gt;",   ">");
    s.replace("&quot;", "\"");
    s.replace("&apos;", "'");
}

// ── SSDP ──────────────────────────────────────────────────────────────────────

static bool ciContains(const char* hay, int len, const char* needle) {
    int nl = (int)strlen(needle);
    if (nl == 0 || nl > len) return false;
    for (int i = 0; i + nl <= len; i++) {
        int j = 0;
        for (; j < nl; j++) {
            char a = hay[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
        }
        if (j == nl) return true;
    }
    return false;
}

static void ssdpSend() {
    s_udp.beginPacket(kSsdpAddr, kSsdpPort);
    s_udp.print(F("M-SEARCH * HTTP/1.1\r\n"
                  "HOST: 239.255.255.250:1900\r\n"
                  "MAN: \"ssdp:discover\"\r\n"
                  "MX: 3\r\n"
                  "ST: urn:schemas-upnp-org:device:MediaServer:1\r\n"
                  "\r\n"));
    s_udp.endPacket();
}

// Parse one SSDP response packet; add server if new.
static void ssdpParsePacket(const char* buf, int len) {
    if (len <= 0) return;
    // Must be a 200 OK or NOTIFY with the right ST/NT. Case-insensitive scan
    // over the raw buffer (no extra String/uppercase copy per packet).
    if (!ciContains(buf, len, "MEDIASERVER")) return;

    String orig(buf, len);
    String location, usn;
    int p = 0;
    while (p < (int)orig.length()) {
        int nl = orig.indexOf('\n', p);
        String line = (nl < 0) ? orig.substring(p) : orig.substring(p, nl);
        line.trim();
        String lup = line;
        lup.toUpperCase();
        if (lup.startsWith("LOCATION:"))  location = line.substring(9);
        else if (lup.startsWith("USN:"))  usn      = line.substring(4);
        p = (nl < 0) ? orig.length() : nl + 1;
    }
    location.trim(); usn.trim();
    if (!location.length()) return;

    // Extract UUID from USN: "uuid:xxxx::urn:..."
    String uuid;
    int ui = usn.indexOf("uuid:");
    if (ui >= 0) {
        ui += 5;
        int ue = usn.indexOf(':', ui);
        if (ue < 0) ue = usn.indexOf(';', ui);
        uuid = (ue > ui) ? usn.substring(ui, ue) : usn.substring(ui);
    }
    if (!uuid.length()) uuid = location; // fallback

    // Deduplicate
    for (uint8_t i = 0; i < s_serverCount; i++) {
        if (s_servers[i].uuid == uuid) return;
    }
    if (s_serverCount >= kMaxServers) return;

    s_servers[s_serverCount].uuid       = uuid;
    s_servers[s_serverCount].location   = location;
    s_servers[s_serverCount].described  = false;
    s_serverCount++;
    sendVirtualMessage(String(F("[UPnP] Found server: ")) + location, INFO);
}

// ── Device Description ────────────────────────────────────────────────────────

// Fetch the device description XML and extract:
//   - friendlyName
//   - ContentDirectory controlURL (made absolute)
static bool describeServer(UpnpServer& srv) {
    if (!srv.location.length()) return false;

    HTTPClient http;
    http.setTimeout(5000);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setRedirectLimit(3);
    http.begin(srv.location);
    int code = http.GET();
    if (code != 200) { http.end(); return false; }

    // Read into PSRAM buffer
    int bodyLen = http.getSize();
    if (bodyLen <= 0) bodyLen = 8192;
    if (bodyLen > 32768) bodyLen = 32768;
    uint8_t* buf = (uint8_t*)heap_caps_malloc(bodyLen + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (uint8_t*)malloc(bodyLen + 1);
    if (!buf) { http.end(); return false; }

    WiFiClient* stream = http.getStreamPtr();
    int got = 0;
    uint32_t t0 = millis();
    while (got < bodyLen && (millis() - t0) < 5000) {
        if (stream->available()) {
            int n = stream->read(buf + got, bodyLen - got);
            if (n > 0) got += n;
        } else { delay(1); }
    }
    buf[got] = '\0';
    http.end();

    String xml((const char*)buf, got);
    free(buf);

    // friendlyName
    srv.friendlyName = xmlTag(xml, "friendlyName");
    xmlDecode(srv.friendlyName);
    if (!srv.friendlyName.length()) srv.friendlyName = srv.uuid;

    // Find ContentDirectory service block and extract controlURL
    // We look for the serviceType tag, then the controlURL in the same <service> block.
    int searchFrom = 0;
    while (true) {
        int st = xml.indexOf("ContentDirectory", searchFrom);
        if (st < 0) break;
        // Find the enclosing <service> block (search backwards for <service>)
        int svcStart = xml.lastIndexOf("<service>", st);
        if (svcStart < 0) { searchFrom = st + 1; continue; }
        int svcEnd = xml.indexOf("</service>", svcStart);
        if (svcEnd < 0) svcEnd = xml.length();
        String svcBlock = xml.substring(svcStart, svcEnd);
        String ctrl = xmlTag(svcBlock, "controlURL");
        if (ctrl.length()) {
            srv.controlUrl = resolveUrl(srv.location, ctrl);
            break;
        }
        searchFrom = st + 1;
    }

    srv.described = srv.controlUrl.length() > 0;
    if (srv.described) {
        sendVirtualMessage(String(F("[UPnP] Described: ")) + srv.friendlyName +
                           F(" ctrl=") + srv.controlUrl, INFO);
    } else {
        sendVirtualMessage(String(F("[UPnP] No ContentDirectory in: ")) + srv.friendlyName, WARN);
    }
    return srv.described;
}

// ── SOAP Browse ───────────────────────────────────────────────────────────────

static String soapBrowse(const String& controlUrl, const String& objectId,
                          int startIndex, int requestedCount, bool metadata) {
    // BrowseMetadata describes a single object: starting index/count are 0.
    if (metadata) { startIndex = 0; requestedCount = 0; }
    String body = F("<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
        " s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:Browse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
        "<ObjectID>");
    body += objectId;
    body += F("</ObjectID><BrowseFlag>");
    body += metadata ? "BrowseMetadata" : "BrowseDirectChildren";
    body += F("</BrowseFlag><Filter>*</Filter><StartingIndex>");
    body += String(startIndex);
    body += F("</StartingIndex><RequestedCount>");
    body += String(requestedCount);
    body += F("</RequestedCount><SortCriteria></SortCriteria>"
        "</u:Browse></s:Body></s:Envelope>");

    HTTPClient http;
    http.setTimeout(8000);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setRedirectLimit(3);
    http.begin(controlUrl);
    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
    http.addHeader("SOAPAction",
        "\"urn:schemas-upnp-org:service:ContentDirectory:1#Browse\"");

    int code = http.POST(body);
    if (code != 200) {
        sendVirtualMessage(String(F("[UPnP] SOAP error ")) + String(code), WARN);
        http.end();
        return String();
    }

    int bodyLen = http.getSize();
    if (bodyLen <= 0) bodyLen = 65536;
    if (bodyLen > 131072) bodyLen = 131072;
    uint8_t* buf = (uint8_t*)heap_caps_malloc(bodyLen + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (uint8_t*)malloc(bodyLen + 1);
    if (!buf) { http.end(); return String(); }

    WiFiClient* stream = http.getStreamPtr();
    int got = 0;
    uint32_t t0 = millis();
    while (got < bodyLen && (millis() - t0) < 8000) {
        if (stream->available()) {
            int n = stream->read(buf + got, bodyLen - got);
            if (n > 0) got += n;
        } else { delay(1); }
    }
    buf[got] = '\0';
    http.end();

    String resp((const char*)buf, got);
    free(buf);
    return resp;
}

// ── DIDL-Lite parser ──────────────────────────────────────────────────────────
// Parses the DIDL-Lite XML embedded inside the SOAP response.
// Builds a JSON array directly into a SpiRamJsonDocument.

// A resource of an item. UPnP CDS allows several <res> per item: the original
// file plus, on transcoding servers, extra representations the server converts
// on-the-fly. We keep the attributes needed to pick the best playable one.
struct ResInfo {
    String url;
    String mimeType;
    String duration;
    bool   transcoded = false;   // DLNA.ORG_CI=1 → server-side converted copy
};

static const uint8_t kMaxResPerItem = 4;

// Collect every <res ...>URL</res> block in the item XML (max 4). Only audio
// resources are kept — cover-art/video extras are skipped. Non-audio <res> are
// still counted towards the cap via a continue, so a pathological server can't
// starve the audio entries.
static void parseResAll(const String& itemXml, ResInfo* out, int& count) {
    count = 0;
    int pos = 0;
    while (count < kMaxResPerItem) {
        int rs = itemXml.indexOf("<res", pos);
        if (rs < 0) break;
        int re = itemXml.indexOf('>', rs);
        if (re < 0) break;
        String resTag = itemXml.substring(rs, re + 1);
        int textEnd = itemXml.indexOf("</res>", re);
        if (textEnd < 0) break;
        String url = itemXml.substring(re + 1, textEnd);
        url.trim();

        String protocolInfo = xmlAttr(resTag, "protocolInfo");
        // DLNA.ORG_CI=1 marks a transcoded representation (0 = original).
        bool transcoded = protocolInfo.indexOf("DLNA.ORG_CI=1") >= 0;
        // protocolInfo = "http-get:*:audio/mpeg:DLNA.ORG_PN=MP3" → extract MIME
        String mimeType = protocolInfo;
        int c1 = mimeType.indexOf(':');
        if (c1 >= 0) {
            int c2 = mimeType.indexOf(':', c1 + 1);
            if (c2 >= 0) {
                int c3 = mimeType.indexOf(':', c2 + 1);
                mimeType = (c3 > c2) ? mimeType.substring(c2 + 1, c3)
                                     : mimeType.substring(c2 + 1);
            }
        }
        String duration = xmlAttr(resTag, "duration");

        pos = textEnd + 6;   // skip "</res>"
        if (mimeType.length() && !mimeType.startsWith("audio/")) continue;

        out[count].url        = url;
        out[count].mimeType   = mimeType;
        out[count].duration   = duration;
        out[count].transcoded = transcoded;
        count++;
    }
}

// MIME prefixes the bundled decoders (MP3/FLAC) can actually start.
static bool isSupportedMime(const String& mime) {
    if (!mime.length()) return false;
    return mime.startsWith("audio/mpeg") || mime.startsWith("audio/mp3") ||
           mime.startsWith("audio/flac") || mime.startsWith("audio/x-flac");
}

// Pick the <res> to play back:
//   1. an original (non-transcoded) resource we can decode → best quality,
//   2. otherwise a transcoded resource we can decode (server converted it),
//   3. otherwise a resource with no MIME declaration (decoder auto-detects),
//   4. otherwise the first resource, flagged not playable so the UI can grey it
//      out (e.g. a bare OGG/WMA with no transcoded copy offered by the server).
static void pickBestRes(const ResInfo* res, int count, int& index, bool& playable) {
    index = -1; playable = false;
    if (count <= 0) return;
    int original = -1, transcoded = -1, unknown = -1;
    for (int i = 0; i < count; i++) {
        if (!res[i].mimeType.length()) { if (unknown < 0) unknown = i; continue; }
        if (!isSupportedMime(res[i].mimeType)) continue;
        if (!res[i].transcoded && original < 0) original = i;
        if (res[i].transcoded && transcoded < 0) transcoded = i;
    }
    if (original >= 0)   { index = original;   playable = true; return; }
    if (transcoded >= 0) { index = transcoded; playable = true; return; }
    if (unknown >= 0)    { index = unknown;    playable = true; return; }
    index = 0;           // only declared-unsupported resources
}

// Parse DIDL-Lite XML and append items to a JSON array.
// Returns {total, returned} counts from the SOAP envelope.
static void parseDIDL(const String& soapResp, const String& baseLocation,
                      JsonArray& arr, int& totalMatches, int& numberReturned) {
    totalMatches   = 0;
    numberReturned = 0;

    // Extract counts from SOAP envelope
    String tm = xmlTag(soapResp, "TotalMatches");
    String nr = xmlTag(soapResp, "NumberReturned");
    if (!tm.length()) tm = xmlTag(soapResp, "ns0:TotalMatches");
    if (!nr.length()) nr = xmlTag(soapResp, "ns0:NumberReturned");
    totalMatches   = tm.toInt();
    numberReturned = nr.toInt();

    // Extract the DIDL-Lite Result string (XML-encoded inside SOAP)
    String result = xmlTag(soapResp, "Result");
    if (!result.length()) result = xmlTag(soapResp, "ns0:Result");
    if (!result.length()) return;

    // Decode XML entities in the Result string to get the inner DIDL XML
    xmlDecode(result);

    // Parse containers and items
    int pos = 0;
    while (pos < (int)result.length()) {
        bool isContainer = false;
        int tagStart = result.indexOf("<container", pos);
        int itemStart = result.indexOf("<item", pos);

        if (tagStart < 0 && itemStart < 0) break;
        if (tagStart >= 0 && (itemStart < 0 || tagStart < itemStart)) {
            isContainer = true;
            pos = tagStart;
        } else {
            pos = itemStart;
        }

        // Find end of opening tag
        int openEnd = result.indexOf('>', pos);
        if (openEnd < 0) break;
        String openTag = result.substring(pos, openEnd + 1);

        // Find closing tag
        const char* closeTag = isContainer ? "</container>" : "</item>";
        int closePos = result.indexOf(closeTag, openEnd);
        if (closePos < 0) { pos = openEnd + 1; continue; }
        String block = result.substring(pos, closePos + strlen(closeTag));

        String id    = xmlAttr(openTag, "id");
        String title = xmlTag(block, "dc:title");
        xmlDecode(title);
        if (!title.length()) title = id;

        JsonObject o = arr.createNestedObject();
        o["id"]          = id;
        o["title"]       = title;
        o["is_container"] = isContainer;

        if (!isContainer) {
            // Track metadata
            String artist = xmlTag(block, "upnp:artist");
            xmlDecode(artist);
            String album  = xmlTag(block, "upnp:album");
            xmlDecode(album);
            String albumArtist = xmlTag(block, "upnp:albumArtist");
            xmlDecode(albumArtist);
            String composer = xmlTag(block, "upnp:composer");
            xmlDecode(composer);
            String genre  = xmlTag(block, "upnp:genre");
            xmlDecode(genre);
            String track  = xmlTag(block, "upnp:originalTrackNumber");
            String date   = xmlTag(block, "dc:date");
            String artUri = xmlTag(block, "upnp:albumArtURI");
            xmlDecode(artUri);
            if (artUri.length()) artUri = resolveUrl(baseLocation, artUri);

            ResInfo resArr[kMaxResPerItem];
            int  resCount = 0;
            int  resIdx = -1;
            bool resPlayable = false;
            parseResAll(block, resArr, resCount);
            pickBestRes(resArr, resCount, resIdx, resPlayable);

            o["res_count"] = resCount;
            o["playable"]  = resPlayable;
            if (resIdx >= 0) {
                ResInfo& chosen = resArr[resIdx];
                if (chosen.url.length())      o["url"]      = resolveUrl(baseLocation, chosen.url);
                if (chosen.mimeType.length()) o["mime"]     = chosen.mimeType;
                if (chosen.duration.length()) o["duration"] = chosen.duration;
            }

            if (artist.length())   o["artist"]   = artist;
            if (album.length())    o["album"]    = album;
            if (albumArtist.length()) o["album_artist"] = albumArtist;
            if (composer.length())    o["composer"]     = composer;
            if (genre.length())    o["genre"]    = genre;
            if (track.length())    o["track"]    = track.toInt();
            if (date.length())     o["date"]     = date;
            if (artUri.length())   o["art"]      = artUri;
        } else {
            // Container: child count if available, plus album art when the
            // server exposes it in the container metadata (zero extra calls —
            // many servers include upnp:albumArtURI in BrowseDirectChildren).
            String cc = xmlAttr(openTag, "childCount");
            if (cc.length()) o["child_count"] = cc.toInt();
            String artUri = xmlTag(block, "upnp:albumArtURI");
            xmlDecode(artUri);
            if (artUri.length()) o["art"] = resolveUrl(baseLocation, artUri);
        }

        pos = closePos + strlen(closeTag);
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void upnpClientBegin() {
    if (s_started) return;
    s_servers = psramAllocServers();
    s_started = true;
    sendVirtualMessage(F("[UPnP] Client ready"), INFO);
}

void upnpClientDiscover() {
    if (!s_started) upnpClientBegin();
    if (WiFi.status() != WL_CONNECTED) return;

    // Reset server list
    if (s_servers) {
        for (uint8_t i = 0; i < kMaxServers; i++) {
            s_servers[i].uuid        = String();
            s_servers[i].friendlyName= String();
            s_servers[i].location    = String();
            s_servers[i].controlUrl  = String();
            s_servers[i].described   = false;
        }
    }
    s_serverCount  = 0;
    s_describeIdx  = 0;

    s_udp.stop();
    s_udp.begin(kSsdpPort);
    ssdpSend();
    s_discoverStartMs = millis();
    s_state = UPNP_DISCOVERING;
    sendVirtualMessage(F("[UPnP] Discovery started"), INFO);
}

void upnpClientLoop() {
    if (!s_started) return;
    if (WiFi.status() != WL_CONNECTED) return;

    switch (s_state) {

    case UPNP_IDLE:
        // Auto-discover once at boot (after WiFi is up)
        upnpClientDiscover();
        break;

    case UPNP_DISCOVERING: {
        // Drain all pending UDP packets this iteration
        int pktSize;
        while ((pktSize = s_udp.parsePacket()) > 0) {
            // Read into PSRAM
            uint8_t* buf = (uint8_t*)heap_caps_malloc(pktSize + 1,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!buf) buf = (uint8_t*)malloc(pktSize + 1);
            if (buf) {
                int n = s_udp.read(buf, pktSize);
                buf[n] = '\0';
                ssdpParsePacket((const char*)buf, n);
                free(buf);
            }
        }
        // Timeout → move to describing
        if ((millis() - s_discoverStartMs) >= kDiscoverMs) {
            s_udp.stop();
            s_describeIdx = 0;
            s_state = (s_serverCount > 0) ? UPNP_DESCRIBING : UPNP_READY;
            if (s_serverCount == 0)
                sendVirtualMessage(F("[UPnP] No servers found"), WARN);
        }
        break;
    }

    case UPNP_DESCRIBING:
        // Describe one server per loop call (each is a blocking HTTP GET ~1-2s)
        if (s_describeIdx < s_serverCount) {
            describeServer(s_servers[s_describeIdx]);
            s_describeIdx++;
        } else {
            s_state = UPNP_READY;
            sendVirtualMessage(String(F("[UPnP] Ready, ")) +
                               String(s_serverCount) + F(" server(s)"), INFO);
        }
        break;

    case UPNP_READY:
    case UPNP_ERROR:
        break;
    }
}

// ── JSON responses ────────────────────────────────────────────────────────────

String upnpServersJson() {
    SpiRamJsonDocument doc(4096);
    doc["success"] = true;
    doc["state"]   = (uint8_t)s_state;
    JsonArray arr  = doc.createNestedArray("servers");
    for (uint8_t i = 0; i < s_serverCount; i++) {
        JsonObject o = arr.createNestedObject();
        o["uuid"]        = s_servers[i].uuid;
        o["name"]        = s_servers[i].friendlyName.length()
                               ? s_servers[i].friendlyName
                               : s_servers[i].uuid;
        o["described"]   = s_servers[i].described;
        o["location"]    = s_servers[i].location;
    }
    String out;
    serializeJson(doc, out);
    return out;
}

String upnpBrowseJson(const String& serverUuid, const String& objectId,
                      int startIndex, int requestedCount, bool metadata) {
    if (requestedCount <= 0)  requestedCount = kBrowsePageSize;
    if (requestedCount > 100) requestedCount = 100;
    if (startIndex < 0)       startIndex = 0;
    if (metadata) { startIndex = 0; requestedCount = 0; }

    // Find server
    UpnpServer* srv = nullptr;
    for (uint8_t i = 0; i < s_serverCount; i++) {
        if (s_servers[i].uuid == serverUuid) { srv = &s_servers[i]; break; }
    }
    if (!srv) {
        return F("{\"success\":false,\"message\":\"Server not found\"}");
    }
    if (!srv->described || !srv->controlUrl.length()) {
        // Try to describe on demand
        if (!describeServer(*srv)) {
            return F("{\"success\":false,\"message\":\"Server not described\"}");
        }
    }

    UpnpState prevState = s_state;
    s_state = UPNP_BROWSING;   // pauses the loop state machine while SOAP runs
    String soapResp = soapBrowse(srv->controlUrl,
                                 objectId.length() ? objectId : "0",
                                 startIndex, requestedCount, metadata);
    if (!soapResp.length()) {
        s_state = prevState;
        return F("{\"success\":false,\"message\":\"SOAP request failed\"}");
    }

    SpiRamJsonDocument doc(65536);
    doc["success"]      = true;
    doc["server_uuid"]  = serverUuid;
    doc["server_name"]  = srv->friendlyName;
    doc["object_id"]    = objectId.length() ? objectId : "0";
    doc["start"]        = startIndex;

    JsonArray arr = doc.createNestedArray("items");
    int totalMatches = 0, numberReturned = 0;
    parseDIDL(soapResp, srv->location, arr, totalMatches, numberReturned);

    doc["total"]    = totalMatches;
    doc["returned"] = numberReturned;
    doc["has_more"] = (startIndex + numberReturned) < totalMatches;
    doc["metadata"] = metadata;

    String out;
    serializeJson(doc, out);
    s_state = prevState;
    return out;
}

#endif // PACK_RADIO
#endif // ENABLE_UDA1334A
