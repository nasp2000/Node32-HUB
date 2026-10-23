#include "feature_flags.h"

#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1

#include "runtime_email.h"

#include <stdarg.h>

#include "config_manager_runtime.h"
#include "email_body_archive.h"
#include "email_day_dedupe.h" // duplicate-send guard (CMD-7): same (folder, subject) <3min refused
#include "sdcard.h"
#include "littlefs_manager.h"
#if ENABLE_SDCARD==1
#include "sd_lib.h"
#endif
#if ENABLE_FILESYSTEM==1
#include <LittleFS.h>
#endif
#include "psram_vfs.h"
#include "commands.h"
#include "day_report_fallback.h"
#include "email_flash_rpc.h" // stack-safe LittleFS (email tasks run PSRAM stacks)
#include "email_transport.h"
#include "log.h"
#include "observability.h"
#include "priority_manager.h"
#include "runtime_environment.h"
#include "structures.h"

#if ENABLE_GOOGLE_EMAIL==1
#include "google_email.h"
#endif

#if ENABLE_PROXIMITY==1
#include "sensors/proximity_manager.h"
#endif

#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

extern bool sdcardPrepareForEmaildayBlocking(unsigned long waitMs);
extern bool flushSdcardNowBlocking(const char *reason, unsigned long waitMs);
extern bool acquireSDAccess(const char* caller, unsigned long timeoutMs);
extern void releaseSDAccess();
extern void endEmaildayFreezeCleanup();
extern volatile StorageMode currentStorageMode;
extern void processSdEmailQueue();
extern void processRamEmailQueue();
extern void logFlushIfDue();
extern String collectMatchedRulesFromContent(const String &content);
extern void markMatchedRulesPending(const String &matchedRules);
extern void finalizeMatchedRulesResult(const String &matchedRules, bool success);
extern bool enviarEmailComAnexosSD(const String &folderPath, const String &subject, const String &bodyText, size_t maxBytesPerEmail, const String &shiftEmails = "");
extern void safeEnviarMensagemVirtual(const char* fmt, int level, ...);
extern bool lastTriggerFromDb25;
extern bool holdBusyDuringEmail;
extern bool emulatorShouldBeBusy;
extern void setBusyPinState(bool state);
extern bool smtpSendEmail(const String &from,
                          const String &to,
                          const String &subject,
                          const String &body,
                          const String &server,
                          int port,
                          const String &user,
                          const String &pass,
                          bool tlsInsecure,
                          bool isHtml);
// Last SMTP transport failure (smtp_core_impl.h, single TU): persisted as the
// EMAIL_PIPELINE_FAIL `err=` on SMTP failures. GAS failures pass nullptr.
extern const char *smtpLastErrorText();

EmailSendingState currentEmailSendingState = EMAIL_IDLE;
String emailContentToSend = "";
String emailMatchedRulesToFinalize = "";
String emailRawDataCache = "";
String emailTablesDataCache = "";
unsigned long lastEmailActionTime = 0;
int emailAttemptCount = 0;
int emailCycleCount = 0;
unsigned long busyDeactivationStartTime = 0;
std::vector<std::tuple<String, String, String>> foundRecipientsForEmail;
std::vector<String> foundNumericLabels;
EmailTriggerSource lastEmailTriggerSource = TRIGGER_NONE;
bool emailProcessInitiatedThisCycle = false;
bool isEmailSendingActive = false;

// T24 — .txt body file of the emails, T8b standard (turno_data).
// Path /{sys}/<type>_<YYYY-MM-DD>_<HH-MM>.txt with send time:
// SD when (sdMode && mounted), otherwise the same path in LittleFS.
// Writes on EVERY call (local record; the email is best-effort). Never touches
// Wi-Fi nor email configuration.
// NOTE: defined BEFORE the #if ENABLE_SMTP==1 block to stay available
// also in google-only builds (SMTP=0), where the Unikon direct points
// call this function.
//
// Owner 2026-10-21: on the board only the SHIFT email keeps a .txt. The shift
// record has its own path (turno{N}_<date>_<time>.txt + turno_data.txt, written
// by _proxWriteShiftDataTxtAt() in proximity_manager.cpp) and is NOT affected
// here. The generic email bodies (ALARM_*, INACTIVITY_*, TEST_*, EMAILDAY_*,
// SENSORDAY_*, LOGDAY_*, STREAM_*, MOTION_*) were just noise on the card — they
// are no longer archived. UNIKON/BRICON keep theirs (their own products, with
// their own local record).
static inline bool emailArchiveTipoAllowed(const String &tipo) {
    return tipo == "UNIKON" || tipo == "BRICON";
}

bool emailArchiveSaveBody(const String &tipo, const String &body) {
    if (body.isEmpty() || tipo.isEmpty()) return false;
    if (!emailArchiveTipoAllowed(tipo)) return false;   // owner: only the shift .txt
    std::string tipoS(tipo.c_str());
    String sysName = configManager.getSdcardName();
    std::string sysS(sysName.c_str());
    char dateL[20];
    char timeL[20];
    time_t now = time(nullptr);
    if (now > 1000000000L) {
        struct tm ti;
        localtime_r(&now, &ti);
        snprintf(dateL, sizeof(dateL), "%04d-%02d-%02d",
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
        snprintf(timeL, sizeof(timeL), "%02d-%02d", ti.tm_hour, ti.tm_min);
    } else {
        snprintf(dateL, sizeof(dateL), "nodate");
        snprintf(timeL, sizeof(timeL), "%lu", (unsigned long)millis());
    }
    std::string pathS = emailArchivePath(sysS, tipoS, dateL, timeL);
    String path(pathS.c_str());
    if (path.isEmpty()) return false;
#if ENABLE_SDCARD==1
    if (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted()) {
        extern bool sdMutexLock(TickType_t timeoutTicks);
        extern void sdMutexUnlock();
        if (!sdMutexLock(pdMS_TO_TICKS(1500))) {
            sendVirtualMessage(F("EmailArchive: skipped, SD busy"), WARN);
            return false;
        }
        String rootDir = String("/") + (sysName.isEmpty() ? String("SD_LOG") : sysName);
        if (!SD_MMC.exists(rootDir.c_str())) SD_MMC.mkdir(rootDir.c_str());
        File f = SD_MMC.open(path.c_str(), FILE_WRITE);
        bool ok = false;
        size_t w = 0;
        if (f) { w = f.print(body); f.close(); ok = (w == body.length()); }
        sdMutexUnlock();
        if (ok) {
            storNoteSdWrite(w);
            sendVirtualMessage(String(F("EmailArchive: saved -> ")) + path, INFO);
            return true;
        }
        sendVirtualMessage(String(F("EmailArchive: SD write FAILED: ")) + path, WARN, false, true);
        return false;
    }
#endif
#if ENABLE_FILESYSTEM==1
    // Write through the stack-safe flash API (emailFlash*): each op locks and
    // is dispatched direct/RPC per the caller's stack, so archiving from a
    // PSRAM-stacked email task is served by the loop instead of panicking.
    // No littlefsBegin() gate here (that call can itself touch flash, and the
    // API executor runs it on a safe stack); failure is reported below.
    String dir = path.substring(0, path.lastIndexOf('/'));
    if (dir.length() > 1 && !emailFlashExists(dir)) {
        emailFlashMkdir(dir);
    }
    bool lok = emailFlashWriteFile(path, (const uint8_t*)body.c_str(), body.length());
    if (lok) {
        storNoteLfsWrite(body.length());
        sendVirtualMessage(String(F("EmailArchive: saved -> LittleFS:")) + path, INFO);
        return true;
    }
    sendVirtualMessage(String(F("EmailArchive: LittleFS write FAILED: ")) + path, WARN, false, true);
    return false;
#else
    sendVirtualMessage(F("EmailArchive: skipped, no storage backend"), WARN);
    return false;
#endif
}

// Infere o tipo T24 a partir do subject; fallback fixo por ponto de envio.
static String emailArchiveInferTipo(const String &subject, const char *fallback) {
    std::string t = emailArchiveTipoFromSubjectOr(
        std::string(subject.c_str()), fallback ? fallback : "EMAIL");
    return String(t.c_str());
}

#if ENABLE_SMTP==1
TaskHandle_t emailTestTaskHandle = NULL;
volatile bool emailTestInProgress = false;
String emailTestSubject = "";
String emailTestBody = "";
String emailTestCustomRecipient = "";
bool emailTestForceBodyOnlyOnce = false;
SemaphoreHandle_t smtpMutex = NULL;
SemaphoreHandle_t emailQueueMutex = NULL;
SemaphoreHandle_t ramEmailQueueMutex = NULL;
SemaphoreHandle_t emailQueueProcessorMutex = NULL;

TaskHandle_t inactivityAlertTaskHandle = NULL;
volatile bool inactivityAlertInProgress = false;
String inactivityAlertSubject = "";
String inactivityAlertBody = "";
String inactivityAlertRecipients = "";

static TaskHandle_t emailTaskHandle = NULL;
static StackType_t* emailTaskStack = nullptr;
static StaticTask_t* emailTaskTCB = nullptr;
static TaskHandle_t emailQueueProcessorTaskHandle = NULL;
static StackType_t* emailQueueProcessorTaskStack = nullptr;
static StaticTask_t* emailQueueProcessorTaskTCB = nullptr;

static constexpr uint32_t EMAIL_TASK_STACK_SIZE_BYTES = 16384;
// 32KB: a task corre TLS handshake (mbedTLS) + base64 streaming + Strings;
// 24KB rebentava em stack overflow -> reset PANIC no SENSORDAY-folder.
static constexpr uint32_t EMAIL_QUEUE_TASK_STACK_SIZE = 32768;
static constexpr size_t STREAM_EMAIL_QUEUE_SIZE = 48;
static constexpr size_t MIN_HEAP_RECOVERY_PERCENT = 65;

static TaskHandle_t smtpMutexOwnerTask = NULL;
static volatile uint32_t smtpMutexBadUnlockCount = 0;
static unsigned long lastStreamEmailSentTime = 0;
static unsigned long heapBeforeLastEmail = 0;

void inactivityAlertTask(void *parameter);

enum StreamEmailType {
    EMAIL_TYPE_STREAMING = 0,
    EMAIL_TYPE_EMAILDAY = 1
};

struct PendingStreamEmail {
    StreamEmailType emailType;
    std::vector<char, PsramAllocatorStd<char>> content;
    String matchedRules;
    String folderPath;
    String subject;
    String bodyText;
    String shiftEmails;  // Extra recipients for SHIFTDAY emails (empty for other types)
    unsigned long timestamp;
    size_t sizeBytes;
    bool occupied;
};

EXT_RAM_BSS_ATTR static PendingStreamEmail streamEmailQueue[STREAM_EMAIL_QUEUE_SIZE];
static size_t streamEmailQueueHead = 0;
static size_t streamEmailQueueTail = 0;
static size_t streamEmailQueueCount = 0;

static String runtimeEmailBuildMessage(const char* format, ...) {
    char msgBuffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(msgBuffer, sizeof(msgBuffer), format, args);
    va_end(args);
    return String(msgBuffer);
}

static String runtimeEmailNormalizeRecipientToken(const String &tokenRaw) {
    String token = tokenRaw;
    token.trim();
    if (token.length() == 0) return String();

    int lt = token.lastIndexOf('<');
    int gt = token.lastIndexOf('>');
    if (lt >= 0 && gt > lt) {
        String emailOnly = token.substring(lt + 1, gt);
        emailOnly.trim();
        return emailOnly;
    }

    if (token.startsWith("<") && token.endsWith(">") && token.length() > 2) {
        token = token.substring(1, token.length() - 1);
        token.trim();
    }

    return token;
}

void runtimeEmailCollectUniqueRecipients(const String &listRaw, std::vector<String> &addedLower, std::vector<String> &addedEmails) {
    int pos = 0;
    while (pos < (int)listRaw.length()) {
        int sepSemicolon = listRaw.indexOf(';', pos);
        int sepComma = listRaw.indexOf(',', pos);
        int sep = -1;
        if (sepSemicolon < 0) sep = sepComma;
        else if (sepComma < 0) sep = sepSemicolon;
        else sep = min(sepSemicolon, sepComma);
        if (sep < 0) sep = listRaw.length();

        String piece = listRaw.substring(pos, sep);
        pos = sep + 1;

        String emailOnly = runtimeEmailNormalizeRecipientToken(piece);
        if (emailOnly.length() == 0 || emailOnly.indexOf('@') <= 0) continue;

        String lower = emailOnly;
        lower.toLowerCase();
        bool seen = false;
        for (const auto &existing : addedLower) {
            if (existing == lower) {
                seen = true;
                break;
            }
        }
        if (seen) continue;

        addedLower.push_back(lower);
        addedEmails.push_back(emailOnly);
    }
}

String runtimeEmailBuildRecipientsLog(const std::vector<String> &emails) {
    const size_t maxLen = 256;
    String out;
    out.reserve(min((size_t)64, maxLen));
    for (size_t i = 0; i < emails.size(); ++i) {
        if (emails[i].length() == 0) continue;
        if (out.length() > 0) out += ", ";
        if (out.length() + emails[i].length() > maxLen) {
            out += "...";
            break;
        }
        out += emails[i];
    }
    return out;
}

static bool isHeapRecoveredForNextEmail() {
    if (heapBeforeLastEmail == 0) return true;

    unsigned long timeSinceLastEmail = millis() - lastStreamEmailSentTime;
    if (timeSinceLastEmail > 10000) return true;

    size_t currentHeap = ESP.getFreeHeap();
    size_t heapRecovered = (currentHeap * 100) / heapBeforeLastEmail;
    return heapRecovered >= MIN_HEAP_RECOVERY_PERCENT;
}

static bool dequeueStreamingEmail(StreamEmailType &type,
                                  String &content,
                                  String &matchedRules,
                                  String &folder,
                                  String &subject,
                                  String &body,
                                  String &shiftEmails) {
    if (streamEmailQueueCount == 0) return false;

    PendingStreamEmail &email = streamEmailQueue[streamEmailQueueTail];
    if (!email.occupied) return false;

    type = email.emailType;

    if (type == EMAIL_TYPE_STREAMING) {
        content = String(email.content.data(), email.content.size());
        matchedRules = String(email.matchedRules.c_str());
        folder = "";
        subject = "";
        body = "";
        shiftEmails = "";
    } else {
        content = "";
        matchedRules = "";
        folder = String(email.folderPath.c_str());
        subject = String(email.subject.c_str());
        body = String(email.bodyText.c_str());
        shiftEmails = String(email.shiftEmails.c_str());
    }

    email.content.clear();
    email.content.shrink_to_fit();
    email.matchedRules = "";
    email.folderPath = "";
    email.subject = "";
    email.bodyText = "";
    email.shiftEmails = "";
    email.occupied = false;

    streamEmailQueueTail = (streamEmailQueueTail + 1) % STREAM_EMAIL_QUEUE_SIZE;
    streamEmailQueueCount--;
    return true;
}

bool runtimeEmailStateMachineIdle() {
    return currentEmailSendingState == EMAIL_IDLE;
}

void startEmailSending(const String &content, EmailTriggerSource triggerSource, const String &matchedRules) {
    safeEnviarMensagemVirtual("DBG: startEmail pre st=%d init=%s", DEBUG, (int)currentEmailSendingState, (emailProcessInitiatedThisCycle ? "1" : "0"));

    // ── Global rate-limit: minimum 3 minutes between any automatic emails ──
    // Prevents email storms (e.g. recipient rules firing every minute).
    const unsigned long MIN_EMAIL_INTERVAL_MS = 180000UL; // 3 minutes
    if (lastStreamEmailSentTime > 0 && (millis() - lastStreamEmailSentTime) < MIN_EMAIL_INTERVAL_MS) {
        safeEnviarMensagemVirtual("Email: rate-limited (min 3min between emails). Skipping.", WARN);
        return;
    }

    if (currentEmailSendingState == EMAIL_IDLE) {
        emailContentToSend = content;
        emailMatchedRulesToFinalize = matchedRules;
        lastEmailTriggerSource = triggerSource;
        currentEmailSendingState = EMAIL_START_SENDING;
        emailCycleCount = 1;
        emailAttemptCount = 0;
        lastEmailActionTime = millis();
        emailNoRecipientFlag = false;
        // T24: body .txt file (STREAM, send time), SD or LittleFS.
        emailArchiveSaveBody(F("STREAM"), content);
        safeEnviarMensagemVirtual("Email: start NB.", INFO);
        safeEnviarMensagemVirtual("DBG: heap=%u", DEBUG, (unsigned)ESP.getFreeHeap());
        safeEnviarMensagemVirtual("ST: -> EMAIL_START_SENDING", DEBUG);

        if (lastTriggerFromDb25) {
            holdBusyDuringEmail = true;
            emulatorShouldBeBusy = true;
            setBusyPinState(true);
        }
    } else {
        safeEnviarMensagemVirtual("Email: ja a enviar (ignorado)", WARN);
    }
}

bool smtpLock(TickType_t timeoutTicks) {
    if (smtpMutex == NULL) return false;
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (smtpMutexOwnerTask == self) return true;
    if (xSemaphoreTakeRecursive(smtpMutex, timeoutTicks) != pdTRUE) return false;
    smtpMutexOwnerTask = self;
    return true;
}

void smtpUnlock() {
    if (smtpMutex == NULL) return;
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (smtpMutexOwnerTask != self) {
        smtpMutexBadUnlockCount++;
        return;
    }
    smtpMutexOwnerTask = NULL;
    xSemaphoreGiveRecursive(smtpMutex);
}

size_t runtimeEmailGetStreamQueueCount() {
    return streamEmailQueueCount;
}

unsigned long runtimeEmailGetLastStreamEmailSentTime() {
    return lastStreamEmailSentTime;
}

void enqueueStreamingEmail(const String &content, const String &matchedRules) {
    if (!configManager.getSdcardEnabled() || !configManager.getSdcardEmailSend()) {
        sendVirtualMessage(F("⚠️ Queue disabled (SD/Email off). Sending streaming email directly via RAM"), WARN);
        String resolvedRules = matchedRules.length() ? matchedRules : collectMatchedRulesFromContent(content);
        if (resolvedRules.length() > 0) markMatchedRulesPending(resolvedRules);
        startEmailSending(content, TRIGGER_AUTO_LINE_LIMIT, resolvedRules);
        return;
    }

    String resolvedRules = matchedRules.length() ? matchedRules : collectMatchedRulesFromContent(content);

    if (streamEmailQueueCount >= STREAM_EMAIL_QUEUE_SIZE) {
        PendingStreamEmail &oldEmail = streamEmailQueue[streamEmailQueueTail];
        if (oldEmail.matchedRules.length() > 0) finalizeMatchedRulesResult(oldEmail.matchedRules, false);
        oldEmail.content.clear();
        oldEmail.content.shrink_to_fit();
        oldEmail.matchedRules = "";
        oldEmail.folderPath = "";
        oldEmail.subject = "";
        oldEmail.bodyText = "";
        oldEmail.occupied = false;
        streamEmailQueueTail = (streamEmailQueueTail + 1) % STREAM_EMAIL_QUEUE_SIZE;
        streamEmailQueueCount--;
        sendVirtualMessage(F("⚠️ Queue FULL (16 emails). Dropping oldest to make space"), WARN);
    }

    PendingStreamEmail &email = streamEmailQueue[streamEmailQueueHead];
    email.emailType = EMAIL_TYPE_STREAMING;
    email.content.clear();
    email.content.resize(content.length());
    memcpy(email.content.data(), content.c_str(), content.length());
    email.matchedRules = resolvedRules;
    email.folderPath = "";
    email.subject = "";
    email.bodyText = "";
    email.timestamp = millis();
    email.sizeBytes = content.length();
    email.occupied = true;

    streamEmailQueueHead = (streamEmailQueueHead + 1) % STREAM_EMAIL_QUEUE_SIZE;
    streamEmailQueueCount++;

    sendVirtualMessage(runtimeEmailBuildMessage("✅ STREAM EMAIL QUEUED: %u bytes, queue=%u/16",
                                                  (unsigned)content.length(),
                                                  (unsigned)streamEmailQueueCount), INFO);

    if (resolvedRules.length() > 0) markMatchedRulesPending(resolvedRules);
}

void enqueueEmaildayEmail(const String &folderPath, const String &subject, const String &bodyText, const String &shiftEmails) {
    if (!configManager.getSdcardEnabled() || !configManager.getSdcardEmailSend()) {
        sendVirtualMessage(F("⚠️ Queue disabled (SD/Email off). Sending EMAILDAY directly (bypass queue)"), WARN);
        return;
    }

    if (streamEmailQueueCount >= STREAM_EMAIL_QUEUE_SIZE) {
        PendingStreamEmail &oldEmail = streamEmailQueue[streamEmailQueueTail];
        oldEmail.content.clear();
        oldEmail.content.shrink_to_fit();
        oldEmail.folderPath = "";
        oldEmail.subject = "";
        oldEmail.bodyText = "";
        oldEmail.occupied = false;
        streamEmailQueueTail = (streamEmailQueueTail + 1) % STREAM_EMAIL_QUEUE_SIZE;
        streamEmailQueueCount--;
        sendVirtualMessage(F("⚠️ Queue FULL (16 emails). Dropping oldest for EMAILDAY"), WARN);
    }

    PendingStreamEmail &email = streamEmailQueue[streamEmailQueueHead];
    email.emailType = EMAIL_TYPE_EMAILDAY;
    email.content.clear();
    email.folderPath = folderPath;
    email.subject = subject;
    email.bodyText = bodyText;
    email.shiftEmails = shiftEmails;
    email.timestamp = millis();
    email.sizeBytes = 0;
    email.occupied = true;

    streamEmailQueueHead = (streamEmailQueueHead + 1) % STREAM_EMAIL_QUEUE_SIZE;
    streamEmailQueueCount++;

    sendVirtualMessage(runtimeEmailBuildMessage("✅ EMAILDAY QUEUED: folder=%s, queue=%u/16",
                                                  folderPath.c_str(),
                                                  (unsigned)streamEmailQueueCount), INFO);
}

void processEmailQueue() {
    if (emailQueueProcessorMutex == NULL) return;
    if (xSemaphoreTake(emailQueueProcessorMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    if (streamEmailQueueCount == 0) {
        xSemaphoreGive(emailQueueProcessorMutex);
        return;
    }
    if (!isHeapRecoveredForNextEmail()) {
        xSemaphoreGive(emailQueueProcessorMutex);
        return;
    }

    PendingStreamEmail &nextItem = streamEmailQueue[streamEmailQueueTail];
    if (!nextItem.occupied) {
        xSemaphoreGive(emailQueueProcessorMutex);
        return;
    }
    StreamEmailType nextType = nextItem.emailType;

    if (nextType == EMAIL_TYPE_STREAMING && currentEmailSendingState != EMAIL_IDLE) {
        xSemaphoreGive(emailQueueProcessorMutex);
        return;
    }

    if (nextType == EMAIL_TYPE_EMAILDAY && !priorityManager().canStartEmail()) {
        sendVirtualMessage(F("⏸️ Stream queue: P3 busy, EMAILDAY/LOGDAY/SENSORDAY kept in queue, retry in 5s"), DEBUG);
        xSemaphoreGive(emailQueueProcessorMutex);
        return;
    }

    heapBeforeLastEmail = ESP.getFreeHeap();

    StreamEmailType emailType;
    String content, matchedRules, folder, subject, body, shiftEmails;
    if (!dequeueStreamingEmail(emailType, content, matchedRules, folder, subject, body, shiftEmails)) {
        xSemaphoreGive(emailQueueProcessorMutex);
        return;
    }

    bool success = false;

    if (emailType == EMAIL_TYPE_STREAMING) {
        if (content.length() == 0) {
            sendVirtualMessage(F("❌ STREAM QUEUE: Empty content, skipping"), ERROR);
            xSemaphoreGive(emailQueueProcessorMutex);
            return;
        }
        sendVirtualMessage(runtimeEmailBuildMessage("📤 STREAM QUEUE: Sending email (%u bytes), %u remaining",
                                                      (unsigned)content.length(),
                                                      (unsigned)streamEmailQueueCount), INFO);
        startEmailSending(content, TRIGGER_AUTO_LINE_LIMIT, matchedRules);
        success = true;
    } else if (emailType == EMAIL_TYPE_EMAILDAY) {
        if (folder.length() == 0) {
            sendVirtualMessage(F("❌ EMAILDAY QUEUE: Empty folder path, skipping"), ERROR);
            xSemaphoreGive(emailQueueProcessorMutex);
            return;
        }
        sendVirtualMessage(runtimeEmailBuildMessage("📤 EMAILDAY QUEUE: Sending folder=%s, %u remaining",
                                                      folder.c_str(),
                                                      (unsigned)streamEmailQueueCount), INFO);

        unsigned long maxBytesEmailday = (4UL * 1024UL * 1024UL * 110UL) / 100UL;
        bool usesPsram = (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled());
        bool mutexAcquired = false;

        if (!usesPsram) {
            if (!acquireSDAccess("EMAILDAY_QUEUE", 60000)) {
                sendVirtualMessage(F("❌ EMAILDAY QUEUE: Failed to acquire SD mutex (timeout)"), ERROR);
                success = false;
            } else {
                sendVirtualMessage(F("✅ EMAILDAY QUEUE: SD mutex acquired"), DEBUG);
                mutexAcquired = true;
            }
        } else {
            sendVirtualMessage(F("EMAILDAY QUEUE: PSRAM mode - no SD mutex needed"), INFO);
            mutexAcquired = true;
        }

        if (mutexAcquired) {
            String emailMethod = configManager.getEmailMethod();
            bool sent = false;

            if (emailMethod == "smtp") {
#if ENABLE_SMTP==1
                sendVirtualMessage(F("📧 EMAILDAY QUEUE: Using SMTP (configured)"), DEBUG);
                sent = enviarEmailComAnexosSD(folder, subject, body, maxBytesEmailday, shiftEmails);
#else
                sendVirtualMessage(F("❌ EMAILDAY QUEUE: SMTP not compiled"), ERROR);
#endif
            } else if (emailMethod == "google") {
#if ENABLE_GOOGLE_EMAIL==1
                if (googleEmailIsAvailable()) {
                    sendVirtualMessage(F("📧 EMAILDAY QUEUE: Using Google Apps Script (configured)"), DEBUG);
                    sent = googleEmailSendEmailDay(folder, subject, body, maxBytesEmailday, shiftEmails);
                } else {
                    sendVirtualMessage(F("❌ EMAILDAY QUEUE: Google Script not configured"), ERROR);
                }
#else
                sendVirtualMessage(F("❌ EMAILDAY QUEUE: Google Email not compiled"), ERROR);
#endif
            } else {
                sendVirtualMessage(runtimeEmailBuildMessage("❌ EMAILDAY QUEUE: Invalid email method '%s'", emailMethod.c_str()), ERROR);
            }

            success = sent;

            if (sent) {
                const char* motivo = (subject.indexOf(F("LogDay")) >= 0) ? "LOGDAY" :
                                     (subject.indexOf(F("SensorDay")) >= 0) ? "SENSORDAY" : "EMAILDAY";
                const char* tipo = (emailMethod == "google") ? "GAS" : "SMTP";
                logEmailSent(tipo, motivo, subject, configManager.getEmailDestination());
                observabilityLogEmailPipeline("emailday-queue", tipo, subject.c_str(), configManager.getEmailDestination().c_str(), true, 1);
#if ENABLE_SCHEDULER==1
                dayReportEndAttempt(true);
#endif
            } else {
                observabilityLogEmailPipeline("emailday-queue", (emailMethod == "google") ? "GAS" : "SMTP", subject.c_str(), configManager.getEmailDestination().c_str(), false, 1,
                                              (emailMethod == "google") ? nullptr : smtpLastErrorText());
#if ENABLE_SCHEDULER==1
                dayReportEndAttempt(false);
#endif
            }

            if (!usesPsram) {
                sendVirtualMessage(F("✅ EMAILDAY QUEUE: Releasing SD mutex"), DEBUG);
                releaseSDAccess();
            } else {
                sendVirtualMessage(F("EMAILDAY QUEUE: PSRAM mode - no mutex to release"), DEBUG);
                // Release the PSRAM snapshot now that we have finished sending (or failed).
                if (psramVFSIsEnabled() && psramVFSIsSnapshotActive()) {
                    psramVFSDisableSnapshot();
                    sendVirtualMessage(F("EMAILDAY QUEUE: PSRAM snapshot released after send"), INFO);
                }
            }
        }

        if (!success) {
            sendVirtualMessage(F("❌ EMAILDAY QUEUE: Send failed"), ERROR);
        }
    }

    lastStreamEmailSentTime = millis();
    xSemaphoreGive(emailQueueProcessorMutex);
}

static void emailTaskFunction(void *pvParameters) {
    (void)pvParameters;

    sendVirtualMessage(F("✅ emailTaskFunction: Background task STARTED (30s cycle)"), INFO);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        // NOTE: logFlushIfDue() moved to the loop task (loop_impl.h). This task
        // has a PSRAM stack and logFlushIfDue() may touch LittleFS, which is not
        // safe from PSRAM (HISTORIC/HomeE32 (8)); the loop task flushes instead.
        processSdEmailQueue();
        processRamEmailQueue();
        taskYIELD();
    }
}

static void emailQueueProcessorTask(void *pvParameters) {
    (void)pvParameters;
    while (true) {
        processEmailQueue();
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void runtimeEmailInitBackgroundTasks() {
    emailQueueMutex = xSemaphoreCreateRecursiveMutex();
    ramEmailQueueMutex = xSemaphoreCreateRecursiveMutex();
    emailQueueProcessorMutex = xSemaphoreCreateMutex();

    if (emailQueueProcessorMutex == NULL) {
        sendVirtualMessage(F("❌ Failed to create email queue processor mutex"), ERROR);
    }

    smtpMutex = xSemaphoreCreateRecursiveMutex();

    if (emailQueueMutex == NULL) {
        sendVirtualMessage(F("❌ Failed to create email mutex"), ERROR);
    }

    // HEAP RECOVERY (2026-10-16): both email tasks keep PSRAM stacks — the
    // internal-stack fix for the SENSORDAY panic cost 48KB of the internal heap
    // (EMAIL_TASK_STACK_SIZE_BYTES + EMAIL_QUEUE_TASK_STACK_SIZE) and the user
    // needs it back ("LittleFS SMTP must work like SD without that heap cost").
    // Flash access is now stack-safe instead of stack-restricted: every flash
    // touch in the email path goes through emailFlash*() (email_flash_rpc),
    // which runs the op directly when the caller's stack is internal and
    // otherwise publishes it to emailFlashServe() on the loop task (internal
    // stack). Historic panic proof: HISTORIC/HomeE32 (8) 2026-10-15
    // (emailQueueProc -> esp_partition_read -> spi_flash_disable_interrupts_
    // caches_and_other_cpu -> assert esp_task_stack_is_sane_cache_disabled).
    emailFlashRpcInit(); // request mutex must exist before the tasks start

    emailTaskTCB = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    emailTaskStack = (StackType_t*)heap_caps_malloc(EMAIL_TASK_STACK_SIZE_BYTES, MALLOC_CAP_SPIRAM);
    if (!emailTaskStack) {
        // PSRAM unavailable: fall back to internal RAM (flash I/O stays safe
        // either way — dispatch follows the actual stack, so an internal stack
        // just runs the ops directly).
        emailTaskStack = (StackType_t*)heap_caps_malloc(EMAIL_TASK_STACK_SIZE_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        sendVirtualMessage(F("⚠️ emailTask stack fell back to internal RAM (PSRAM tight)"), WARN);
    }

    if (emailTaskTCB && emailTaskStack) {
        // NOTE: depth is in WORDS, not bytes (same bug class as ftpMon OEE-69).
        emailTaskHandle = xTaskCreateStaticPinnedToCore(
            emailTaskFunction,
            "emailTask",
            EMAIL_TASK_STACK_SIZE_BYTES / sizeof(StackType_t),
            nullptr,
            1,
            emailTaskStack,
            emailTaskTCB,
            0
        );
    } else {
        emailTaskHandle = NULL;
    }

    if (emailTaskHandle == NULL) {
        sendVirtualMessage(F("Failed to create email task"), ERROR);
    } else {
        sendVirtualMessage(F("Email task created on Core 0"), INFO);
    }

    emailQueueProcessorTaskTCB = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    // PSRAM stack (heap recovery, see the emailTask stack note above): flash
    // access from this task goes through emailFlash*() and is served by the
    // loop task, so a PSRAM stack is safe now — no 32KB internal-RAM cost.
    emailQueueProcessorTaskStack = (StackType_t*)heap_caps_malloc(EMAIL_QUEUE_TASK_STACK_SIZE, MALLOC_CAP_SPIRAM);
    if (!emailQueueProcessorTaskStack) {
        // PSRAM tight: fall back to internal RAM (flash I/O stays safe — the
        // dispatch follows the actual stack, so internal just runs ops direct).
        emailQueueProcessorTaskStack = (StackType_t*)heap_caps_malloc(EMAIL_QUEUE_TASK_STACK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        sendVirtualMessage(F("⚠️ emailQueueProc stack fell back to internal RAM (PSRAM tight)"), WARN);
    }

    if (emailQueueProcessorTaskTCB && emailQueueProcessorTaskStack) {
        // NOTE: depth is in WORDS, not bytes (same bug class as ftpMon OEE-69).
        emailQueueProcessorTaskHandle = xTaskCreateStaticPinnedToCore(
            emailQueueProcessorTask,
            "emailQueueProc",
            EMAIL_QUEUE_TASK_STACK_SIZE / sizeof(StackType_t),
            nullptr,
            1,
            emailQueueProcessorTaskStack,
            emailQueueProcessorTaskTCB,
            0
        );
    } else {
        emailQueueProcessorTaskHandle = NULL;
    }

    if (emailQueueProcessorTaskHandle == NULL) {
        sendVirtualMessage(F("❌ Failed to create email queue processor task"), ERROR);
    } else {
        sendVirtualMessage(F("✅ Email queue processor task created on Core 0 (32KB PSRAM stack)"), INFO);
    }
}

void emailTestTask(void *parameter) {
    (void)parameter;
    sendVirtualMessage(F("\n========================================\n✉️ [EMAIL_TEST] Task started\n========================================"), INFO, false, false);

    emailTestInProgress = true;
    esp_task_wdt_add(NULL);
    esp_task_wdt_reset();

    String localSubject = emailTestSubject;
    String localBody = emailTestBody;
    emailTestSubject = "";
    emailTestBody = "";

    sendVirtualMessage(runtimeEmailBuildMessage("✉️ [EMAIL_TEST] Subject: '%s', Body length: %u chars",
                                                  localSubject.c_str(),
                                                  (unsigned)localBody.length()), INFO, false, false);

    String dest = configManager.getEmailDestination();
    if (dest.length() == 0) {
        sendVirtualMessage(F("❌ [EMAIL_TEST] No recipient configured"), ERROR, false, false);
        emailTestInProgress = false;
        esp_task_wdt_delete(NULL);
        vTaskDelete(NULL);
        return;
    }

    String emailMethod = configManager.getEmailMethod();
    sendVirtualMessage(runtimeEmailBuildMessage("📧 [EMAIL_TEST] Configured method: '%s'", emailMethod.c_str()), INFO, false, false);

#if ENABLE_GOOGLE_EMAIL==1
    bool googleAvailable = googleEmailIsAvailable();
#else
    bool googleAvailable = false;
#endif

    bool smtpAvailable = true;
    bool useGoogle = false;
    bool useSmtp = false;

    if (emailMethod == "google") {
        if (googleAvailable) {
            useGoogle = true;
            sendVirtualMessage(F("✅ [EMAIL_TEST] Using Google Apps Script"), INFO, false, false);
        } else {
            sendVirtualMessage(F("❌ [EMAIL_TEST] Google Script not configured"), ERROR, false, false);
            emailTestInProgress = false;
            esp_task_wdt_delete(NULL);
            vTaskDelete(NULL);
            return;
        }
    } else if (emailMethod == "smtp") {
        if (smtpAvailable) {
            useSmtp = true;
            sendVirtualMessage(F("✅ [EMAIL_TEST] Using SMTP"), INFO, false, false);
        } else {
            sendVirtualMessage(F("❌ [EMAIL_TEST] SMTP not compiled"), ERROR, false, false);
            emailTestInProgress = false;
            esp_task_wdt_delete(NULL);
            vTaskDelete(NULL);
            return;
        }
    } else {
        sendVirtualMessage(runtimeEmailBuildMessage("❌ [EMAIL_TEST] Invalid email method '%s'", emailMethod.c_str()), ERROR, false, false);
        emailTestInProgress = false;
        esp_task_wdt_delete(NULL);
        vTaskDelete(NULL);
        return;
    }

    bool success = false;
    const int maxAttempts = 3;
    const unsigned long retryDelaysMs[] = {10000, 30000, 60000};

    {
        unsigned long waitStart = millis();
        while (!priorityManager().canStartEmail()) {
            if ((millis() - waitStart) > 30000UL) {
                sendVirtualMessage(F("❌ [EMAIL_TEST] Priority lock timeout (another email active)"), ERROR, false, false);
                emailTestInProgress = false;
                esp_task_wdt_delete(NULL);
                vTaskDelete(NULL);
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_task_wdt_reset();
        }
        priorityManager().startEmail();
        sendVirtualMessage(F("✅ [EMAIL_TEST] P3 priority lock acquired"), DEBUG, false, false);
    }

    for (int attempt = 1; attempt <= maxAttempts && !success; attempt++) {
        sendVirtualMessage(runtimeEmailBuildMessage("📤 [EMAIL_TEST] Attempt %d/%d", attempt, maxAttempts), INFO, false, false);
        esp_task_wdt_reset();

        if (useGoogle) {
#if ENABLE_GOOGLE_EMAIL==1
            vTaskDelay(pdMS_TO_TICKS(150));
            success = googleEmailSendSimple(dest, localSubject, localBody);
            vTaskDelay(pdMS_TO_TICKS(100));
            attempt = maxAttempts;
#endif
        } else if (useSmtp) {
            sendVirtualMessage(F("🔒 [EMAIL_TEST] Acquiring SMTP mutex..."), INFO, false, false);
            if (!smtpLock(pdMS_TO_TICKS(60000))) {
                sendVirtualMessage(F("❌ [EMAIL_TEST] SMTP mutex timeout"), ERROR, false, false);
                break;
            }
            sendVirtualMessage(F("✅ [EMAIL_TEST] SMTP mutex acquired"), INFO, false, false);

            vTaskDelay(pdMS_TO_TICKS(150));
            esp_task_wdt_reset();

            String server = configManager.getSmtpServer();
            int port = configManager.getSmtpPort();
            String user = configManager.getSmtpUsername();
            String pass = configManager.getSmtpPassword();
            bool tlsInsecure = configManager.getSmtpTlsInsecure();
            String from = user;

            success = smtpSendEmail(from, dest, localSubject, localBody, server, port, user, pass, tlsInsecure, false);

            vTaskDelay(pdMS_TO_TICKS(100));
            esp_task_wdt_reset();
            smtpUnlock();
            sendVirtualMessage(F("🔓 [EMAIL_TEST] SMTP mutex released"), INFO, false, false);
        }

        if (success) {
            sendVirtualMessage(F("✅ [EMAIL_TEST] Email sent successfully!"), INFO, false, false);
        } else {
            sendVirtualMessage(runtimeEmailBuildMessage("⚠️ [EMAIL_TEST] Attempt %d failed", attempt), WARN, false, false);
            if (attempt < maxAttempts) {
                unsigned long retryDelay = retryDelaysMs[attempt - 1];
                sendVirtualMessage(runtimeEmailBuildMessage("⏳ [EMAIL_TEST] Retrying in %lu seconds...", retryDelay / 1000), INFO, false, false);
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(retryDelay));
                esp_task_wdt_reset();
            }
        }
    }

    if (success) {
        sendVirtualMessage(F("\n✅ [EMAIL_TEST] TEST EMAIL SENT SUCCESSFULLY\n"), INFO, false, false);
        logEmailSent(useGoogle ? "GAS" : "SMTP", "TEST", localSubject, dest);
        observabilityLogEmailPipeline("test", useGoogle ? "GAS" : "SMTP", localSubject.c_str(), dest.c_str(), true, 0);
    } else {
        sendVirtualMessage(F("\n❌ [EMAIL_TEST] TEST EMAIL FAILED AFTER 3 ATTEMPTS\n"), ERROR, false, false);
        observabilityLogEmailPipeline("test", useGoogle ? "GAS" : "SMTP", localSubject.c_str(), dest.c_str(), false, 0,
                                      useGoogle ? nullptr : smtpLastErrorText());
    }

    priorityManager().endEmail();
    emailTestInProgress = false;
    emailTestTaskHandle = NULL;  // must be cleared before vTaskDelete so the guard works on next call
    esp_task_wdt_delete(NULL);
    sendVirtualMessage(F("========================================\n🏁 [EMAIL_TEST] Task terminating\n========================================\n"), INFO, false, false);
    vTaskDelete(NULL);
}

bool startEmailTestAsync(const String &subject, const String &body) {
    sendVirtualMessage(F("\n========================================\n✉️ [START_EMAIL_TEST] Function called\n========================================"), INFO, false, false);

    if (subject.length() == 0) {
        sendVirtualMessage(F("❌ [START_EMAIL_TEST] Empty subject"), ERROR, false, false);
        return false;
    }

    if (emailTestInProgress || emailTestTaskHandle != NULL) {
        sendVirtualMessage(F("⚠️ [START_EMAIL_TEST] Email test already in progress"), WARN, false, false);
        return false;
    }

    if (!emailTransportCanSendEmail()) {
        sendVirtualMessage(String(F("❌ [START_EMAIL_TEST] ")) + emailTransportUnavailableReason(), ERROR, false, false);
        return false;
    }

    size_t freeHeap = ESP.getFreeHeap();
    const size_t minHeap = 15 * 1024;
    if (freeHeap < minHeap) {
        sendVirtualMessage(runtimeEmailBuildMessage("❌ [START_EMAIL_TEST] Insufficient heap: %uKB < %uKB required",
                                                      (unsigned)(freeHeap / 1024),
                                                      (unsigned)(minHeap / 1024)), ERROR, false, false);
        return false;
    }

    emailTestSubject = subject;
    emailTestBody = body;

    // T24: body .txt file (TEST, send time), SD or LittleFS.
    emailArchiveSaveBody(emailArchiveInferTipo(subject, "TEST"), body);

    sendVirtualMessage(runtimeEmailBuildMessage("✉️ [START_EMAIL_TEST] Creating task (Subject: '%s', Body: %u chars, Heap: %uKB)",
                                                  subject.c_str(),
                                                  (unsigned)body.length(),
                                                  (unsigned)(freeHeap / 1024)), INFO, false, false);

    BaseType_t result = xTaskCreatePinnedToCore(
        emailTestTask,
        "EmailTest",
        8192,
        NULL,
        1,
        &emailTestTaskHandle,
        0
    );

    if (result != pdPASS) {
        sendVirtualMessage(runtimeEmailBuildMessage("❌ [START_EMAIL_TEST] Task creation failed (result=%d)", (int)result), ERROR, false, false);
        emailTestTaskHandle = NULL;
        emailTestSubject = "";
        emailTestBody = "";
        return false;
    }

    sendVirtualMessage(F("✅ [START_EMAIL_TEST] Email test task created successfully!"), INFO, false, false);
    sendVirtualMessage(F("========================================\n"), INFO, false, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    return true;
}

// --- Unikon pigeon-arrival email ---
// Dedicated async task so Unikon arrival notifications do not share the test-email slot.
// Structure mirrors emailTestTask; handle is cleared before vTaskDelete so the guard
// in startUnikonEmailAsync works correctly on every subsequent poll.

static volatile bool unikonEmailInProgress = false;
static TaskHandle_t  unikonEmailTaskHandle = NULL;
static String        unikonEmailSubject    = "";
static String        unikonEmailBody       = "";

// The Rambox reports are plain-text tables padded with spaces: they only line up in a
// monospace font. Clients that render text/plain with a proportional font (Gmail web,
// phones) break the columns, so the body is sent as HTML inside a <pre> block.
static String ramboxEmailToHtml(const String &text) {
    String h;
    h.reserve(text.length() + 192);
    h += F("<!DOCTYPE html><html><head><meta charset='UTF-8'></head>"
           "<body style='margin:0;padding:6px;background:#ffffff'>"
           "<pre style=\"font-family:Consolas,'Courier New',monospace;font-size:13px;"
           "line-height:1.35;white-space:pre\">");
    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        if      (c == '&') h += F("&amp;");
        else if (c == '<') h += F("&lt;");
        else if (c == '>') h += F("&gt;");
        else               h += c;
    }
    h += F("</pre></body></html>");
    return h;
}

static void unikonEmailTask(void* /*parameter*/) {
    unikonEmailInProgress = true;
    esp_task_wdt_add(NULL);
    esp_task_wdt_reset();

    String localSubject = unikonEmailSubject;
    String localBody    = unikonEmailBody;
    unikonEmailSubject  = "";
    unikonEmailBody     = "";

    String dest = configManager.getEmailDestination();
    if (dest.length() == 0) {
        sendVirtualMessage(F("[UNIKON_EMAIL] No recipient configured"), ERROR, false, false);
        unikonEmailInProgress = false;
        unikonEmailTaskHandle = NULL;
        esp_task_wdt_delete(NULL);
        vTaskDelete(NULL);
        return;
    }

    String emailMethod = configManager.getEmailMethod();
#if ENABLE_GOOGLE_EMAIL == 1
    bool googleAvailable = googleEmailIsAvailable();
#else
    bool googleAvailable = false;
#endif
    bool useGoogle = (emailMethod == "google" && googleAvailable);
    bool useSmtp   = (emailMethod == "smtp");

    if (!useGoogle && !useSmtp) {
        sendVirtualMessage(runtimeEmailBuildMessage("[UNIKON_EMAIL] Invalid method '%s'", emailMethod.c_str()), ERROR, false, false);
        unikonEmailInProgress = false;
        unikonEmailTaskHandle = NULL;
        esp_task_wdt_delete(NULL);
        vTaskDelete(NULL);
        return;
    }

    // Acquire P3 email priority lock (same level as test email).
    {
        unsigned long waitStart = millis();
        while (!priorityManager().canStartEmail()) {
            if ((millis() - waitStart) > 30000UL) {
                sendVirtualMessage(F("[UNIKON_EMAIL] Priority lock timeout"), ERROR, false, false);
                unikonEmailInProgress = false;
                unikonEmailTaskHandle = NULL;
                esp_task_wdt_delete(NULL);
                vTaskDelete(NULL);
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_task_wdt_reset();
        }
        priorityManager().startEmail();
    }

    bool success = false;
    const int maxAttempts = 3;
    const unsigned long retryDelaysMs[] = {10000, 30000, 60000};

    for (int attempt = 1; attempt <= maxAttempts && !success; attempt++) {
        esp_task_wdt_reset();
        if (useGoogle) {
#if ENABLE_GOOGLE_EMAIL == 1
            vTaskDelay(pdMS_TO_TICKS(150));
            success = googleEmailSendSimple(dest, localSubject, localBody);
            vTaskDelay(pdMS_TO_TICKS(100));
            attempt = maxAttempts;
#endif
        } else if (useSmtp) {
            if (!smtpLock(pdMS_TO_TICKS(60000))) {
                sendVirtualMessage(F("[UNIKON_EMAIL] SMTP mutex timeout"), ERROR, false, false);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(150));
            esp_task_wdt_reset();
            String server = configManager.getSmtpServer();
            int    port   = configManager.getSmtpPort();
            String user   = configManager.getSmtpUsername();
            String pass   = configManager.getSmtpPassword();
            bool insecure = configManager.getSmtpTlsInsecure();
            // Rambox report -> HTML (monospace <pre>) so the ASCII tables align everywhere.
            bool asHtml = (localBody.length() <= 40000);
            String mailBody = asHtml ? ramboxEmailToHtml(localBody) : localBody;
            success = smtpSendEmail(user, dest, localSubject, mailBody,
                                    server, port, user, pass, insecure, asHtml);
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_task_wdt_reset();
            smtpUnlock();
        }
        if (!success && attempt < maxAttempts) {
            vTaskDelay(pdMS_TO_TICKS(retryDelaysMs[attempt - 1]));
            esp_task_wdt_reset();
        }
    }

    if (success) {
        sendVirtualMessage(F("✅ [UNIKON_EMAIL] Sent successfully"), INFO, false, false);
        logEmailSent(useGoogle ? "GAS" : "SMTP", "UNIKON", localSubject, dest);
        observabilityLogEmailPipeline("unikon", useGoogle ? "GAS" : "SMTP",
                                      localSubject.c_str(), dest.c_str(), true, 0);
    } else {
        sendVirtualMessage(F("❌ [UNIKON_EMAIL] Failed after retries"), ERROR, false, false);
        observabilityLogEmailPipeline("unikon", useGoogle ? "GAS" : "SMTP",
                                      localSubject.c_str(), dest.c_str(), false, 0,
                                      useGoogle ? nullptr : smtpLastErrorText());
    }

    priorityManager().endEmail();
    unikonEmailInProgress = false;
    unikonEmailTaskHandle = NULL;  // cleared before vTaskDelete so guard works on next poll
    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

bool startUnikonEmailAsync(const String &subject, const String &body) {
    if (unikonEmailInProgress || unikonEmailTaskHandle != NULL) {
        sendVirtualMessage(F("[UNIKON_EMAIL] Already in progress"), WARN, false, false);
        return false;
    }

    if (!emailTransportCanSendEmail()) {
        sendVirtualMessage(String(F("[UNIKON_EMAIL] ")) + emailTransportUnavailableReason(),
                              ERROR, false, false);
        return false;
    }

    if (ESP.getFreeHeap() < 15 * 1024) {
        sendVirtualMessage(F("[UNIKON_EMAIL] Insufficient heap"), ERROR, false, false);
        return false;
    }

    unikonEmailSubject = subject;
    unikonEmailBody    = body;

    // T24: body .txt file (UNIKON, send time), SD or LittleFS.
    emailArchiveSaveBody(emailArchiveInferTipo(subject, "UNIKON"), body);

    BaseType_t result = xTaskCreatePinnedToCore(
        unikonEmailTask,
        "UnikonEmail",
        8192,
        NULL,
        1,
        &unikonEmailTaskHandle,
        0
    );

    if (result != pdPASS) {
        unikonEmailTaskHandle = NULL;
        unikonEmailSubject    = "";
        unikonEmailBody       = "";
        sendVirtualMessage(F("[UNIKON_EMAIL] Task creation failed"), ERROR, false, false);
        return false;
    }

    sendVirtualMessage(F("✅ [UNIKON_EMAIL] Task created, sending in background"), INFO, false, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    return true;
}

bool startInactivityAlertAsync(const String &recipients, const String &subject, const String &body) {
    if (inactivityAlertInProgress || inactivityAlertTaskHandle != NULL) {
        sendVirtualMessage(F("⚠️ [INACTIVITY] Alert already in progress"), WARN, false, false);
        return false;
    }

    if (recipients.length() == 0) {
        sendVirtualMessage(F("❌ [INACTIVITY] No recipients configured"), ERROR, false, false);
        return false;
    }

    if (!isEmailConfiguredAndWifiConnected()) {
        sendVirtualMessage(F("❌ [INACTIVITY] Email not configured or WiFi disconnected"), ERROR, false, false);
        return false;
    }

    inactivityAlertSubject = subject;
    inactivityAlertBody = body;
    inactivityAlertRecipients = recipients;
    inactivityAlertInProgress = true;

    // T24: body .txt file (type from subject: SENSORDAY-RAM/INACTIVITY/...),
    // send time, SD or LittleFS.
    emailArchiveSaveBody(emailArchiveInferTipo(subject, "INACTIVITY"), body);

    BaseType_t result = xTaskCreatePinnedToCore(
        inactivityAlertTask,
        "InactivityAlert",
        8192,
        NULL,
        1,
        &inactivityAlertTaskHandle,
        0
    );

    if (result != pdPASS) {
        sendVirtualMessage(F("❌ [INACTIVITY] Failed to create alert task"), ERROR, false, false);
        inactivityAlertTaskHandle = NULL;
        inactivityAlertInProgress = false;
        inactivityAlertSubject = "";
        inactivityAlertBody = "";
        inactivityAlertRecipients = "";
        return false;
    }

    sendVirtualMessage(F("📧 [INACTIVITY] Alert task created, sending in background"), INFO, false, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    return true;
}

void inactivityAlertTask(void *parameter) {
    (void)parameter;
    sendVirtualMessage(F("[INACTIVITY] Alert task started in background"), INFO, false, false);

    esp_task_wdt_add(NULL);
    esp_task_wdt_reset();

    String localSubject = inactivityAlertSubject;
    String localBody = inactivityAlertBody;
    String localRecipients = inactivityAlertRecipients;
    inactivityAlertSubject = "";
    inactivityAlertBody = "";
    inactivityAlertRecipients = "";

    vTaskDelay(pdMS_TO_TICKS(100));

    {
        unsigned long waitStart = millis();
        while (!priorityManager().canStartEmail()) {
            if ((millis() - waitStart) > 30000UL) {
                sendVirtualMessage(F("❌ [INACTIVITY] Priority lock timeout (another email active)"), ERROR, false, false);
                inactivityAlertInProgress = false;
                inactivityAlertTaskHandle = NULL;
                esp_task_wdt_delete(NULL);
                vTaskDelete(NULL);
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_task_wdt_reset();
        }
        priorityManager().startEmail();
        sendVirtualMessage(F("✅ [INACTIVITY] P3 priority lock acquired"), DEBUG, false, false);
    }

    bool finalSuccess = false;
    const int maxAttempts = 3;

    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        esp_task_wdt_reset();
        sendVirtualMessage(runtimeEmailBuildMessage("[INACTIVITY] Sending alert (attempt %d/%d)", attempt, maxAttempts), INFO, false, false);

        if (!smtpLock(pdMS_TO_TICKS(60000))) {
            sendVirtualMessage(F("❌ [INACTIVITY] Timeout obtaining SMTP mutex"), ERROR, false, false);
            if (attempt < maxAttempts) {
                sendVirtualMessage(F("[INACTIVITY] Retrying after backoff..."), WARN, false, false);
                vTaskDelay(pdMS_TO_TICKS(10000));
                continue;
            }
            break;
        }

        sendVirtualMessage(F("🔒 [INACTIVITY] SMTP mutex acquired"), DEBUG, false, false);

        bool sendSuccess = false;
#if ENABLE_GOOGLE_EMAIL==1
        if (googleEmailIsAvailable()) {
            sendVirtualMessage(F("[INACTIVITY] Using Google Apps Script Email System"), INFO, false, false);
            sendSuccess = googleEmailSendSimple(localRecipients, localSubject, localBody);
        } else
#endif
        {
            sendVirtualMessage(F("[INACTIVITY] Using traditional SMTP"), INFO, false, false);
            String smtpServer = configManager.getSmtpServer();
            int smtpPort = configManager.getSmtpPort();
            String smtpUser = configManager.getSmtpUsername();
            String smtpPass = configManager.getSmtpPassword();
            String smtpFrom = smtpUser;
            bool smtpTlsInsecure = configManager.getSmtpTlsInsecure();

            sendSuccess = smtpSendEmail(smtpFrom, localRecipients, localSubject, localBody,
                                        smtpServer, smtpPort, smtpUser, smtpPass, smtpTlsInsecure, false);
        }

        smtpUnlock();
        sendVirtualMessage(F("🔓 [INACTIVITY] SMTP mutex released"), DEBUG, false, false);

        const char* channel =
#if ENABLE_GOOGLE_EMAIL==1
            googleEmailIsAvailable() ? "GAS" :
#endif
            "SMTP";

        if (sendSuccess) {
            sendVirtualMessage(F("✅ [INACTIVITY] Alert email sent successfully!"), INFO, false, false);
            logEmailSent(channel, "INACTIVITY", localSubject, localRecipients);
            observabilityLogEmailPipeline("inactivity", channel, localSubject.c_str(), localRecipients.c_str(), true, 0);
            finalSuccess = true;
            break;
        }

        sendVirtualMessage(runtimeEmailBuildMessage("❌ [INACTIVITY] Send failed (attempt %d/%d)", attempt, maxAttempts), ERROR, false, false);
        observabilityLogEmailPipeline("inactivity", channel, localSubject.c_str(), localRecipients.c_str(), false, 0,
                                       (channel[0] == 'S') ? smtpLastErrorText() : nullptr); // 'S'MTP only; GAS has no transport err
        if (attempt < maxAttempts) {
            unsigned long backoffMs = (attempt <= 1) ? 10000UL : (attempt <= 2) ? 30000UL : 60000UL;
            sendVirtualMessage(runtimeEmailBuildMessage("[INACTIVITY] Waiting %lus before retry...", backoffMs / 1000), WARN, false, false);

            unsigned long backoffStart = millis();
            while (millis() - backoffStart < backoffMs) {
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
    }

    if (!finalSuccess) {
        sendVirtualMessage(F("❌ [INACTIVITY] Alert failed after all retries"), ERROR, false, false);
    }

    priorityManager().endEmail();
    esp_task_wdt_delete(NULL);
    inactivityAlertInProgress = false;
    inactivityAlertTaskHandle = NULL;
    sendVirtualMessage(F("[INACTIVITY] Alert task completed"), INFO, false, false);
    vTaskDelete(NULL);
}
#endif

// emailDayTask compiles whenever SMTP is enabled.
// When ENABLE_SDCARD=0, sdcardFreezeWrites and the freeze-seq stubs are provided
// by sdcard.cpp so the extern declarations below resolve without errors.
#if ENABLE_SMTP==1
extern volatile bool sdcardFreezeWrites;
extern volatile uint32_t sdcardFreezeCutoffSeq_RS232;
extern volatile uint32_t sdcardFreezeCutoffSeq_LPT;

TaskHandle_t emailDayTaskHandle = NULL;
volatile bool emailDayInProgress = false;
volatile unsigned long emailDayStartMs = 0;   // set when a send starts (watchdog + auto-recovery)
String emailDayFolderPath = "";
String emailDaySubject = "";
String emailDayBody = "";
String emailDayShiftEmails = "";  // Extra recipients for SHIFTDAY (set by startEmailDayAsync)
String emailDayCmdExtra = "";    // Per-command extra recipients (EMAILDAY/LOGDAY/SENSORDAY); set by caller before startEmailDayAsync

// Safe cleanup for a stuck/dead EMAILDAY send. Never vTaskDeletes (a live task
// holding mutexes must not be killed — mutexes have timeouts, so a fresh send
// recovers by itself). Only clears software state + idempotent cleanups.
void emailDayForceCleanup(const char* reason) {
    emailDayInProgress = false;
    emailDayTaskHandle = NULL;
    emailDayFolderPath = "";
    emailDaySubject = "";
    emailDayBody = "";
    emailDayShiftEmails = "";
    priorityManager().endEmailday();
    endEmaildayFreezeCleanup();
    if (psramVFSIsEnabled() && psramVFSIsSnapshotActive()) psramVFSDisableSnapshot();
    sendVirtualMessage(String(F("EMAILDAY: force cleanup (")) + reason + F(") — new sends unblocked"), ERROR);
}

// Watchdog: call from the main loop. If a send runs past EMAILDAY_STUCK_MS it
// can never complete normally (SMTP timeouts bound every phase), so release it.
void emailDayWatchdogTick() {
    if (!emailDayInProgress) return;
    if (emailDayStartMs == 0) return;
    if (millis() - emailDayStartMs >= EMAILDAY_STUCK_MS) {
        emailDayForceCleanup("watchdog 10min");
    }
}

void emailDayTask(void *parameter) {
    (void)parameter;
    sendVirtualMessage(F("EMAILDAY: started in background"), INFO);

    esp_task_wdt_add(NULL);
    esp_task_wdt_reset();

    UBaseType_t stackFree = uxTaskGetStackHighWaterMark(NULL);
    sendVirtualMessage(runtimeEmailBuildMessage("EMAILDAY: stack free = %u words", (unsigned)stackFree), DEBUG);

    String localFolderPath = emailDayFolderPath;
    String localSubject = emailDaySubject;
    String localBody = emailDayBody;
    String localShiftEmails = emailDayShiftEmails;
    bool emailSendActive = false;

    emailDayFolderPath = "";
    emailDaySubject = "";
    emailDayBody = "";
    emailDayShiftEmails = "";

    vTaskDelay(pdMS_TO_TICKS(100));

    sendVirtualMessage(F("EMAILDAY: waiting for downloads to complete..."), INFO);
    unsigned long waitStartMs = millis();
    while (!priorityManager().canStartEmailday()) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (millis() - waitStartMs > 300000UL) {
            sendVirtualMessage(F("❌ EMAILDAY: download wait timeout (5 min), aborting"), ERROR);
            emailDayInProgress = false;
            esp_task_wdt_delete(NULL);
            vTaskDelete(NULL);
            return;
        }
    }
    sendVirtualMessage(F("? EMAILDAY: downloads clear, starting EMAILDAY operations"), INFO);
    priorityManager().startEmailday();

    bool finalSuccess = false;
    const int maxAttempts = 3;
    bool usesPsram = (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled());

    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        esp_task_wdt_reset();

        usesPsram = (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled());

        if (!usesPsram) {
            sendVirtualMessage(runtimeEmailBuildMessage("EMAILDAY: attempt %d/%d - preparing stable SD snapshot", attempt, maxAttempts), INFO);

            bool prepared = sdcardPrepareForEmaildayBlocking(15000);
            if (!prepared) {
                sendVirtualMessage(F("? EMAILDAY: preparation timed out (flush failed)"), ERROR);
            } else {
                sendVirtualMessage(F("? EMAILDAY: SD snapshot ready, files stable"), DEBUG);
            }

            sendVirtualMessage(F("EMAILDAY: waiting 5s for SD operations to settle..."), DEBUG);
            // Chunked settle wait with TWDT feed: a single delay(5000) never
            // feeds the task watchdog (Arduino delay() yields but no reset).
            for (int settle = 0; settle < 10; ++settle) {
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            sendVirtualMessage(F("? EMAILDAY: SD operations settled"), DEBUG);

            esp_task_wdt_reset();
        } else {
            sendVirtualMessage(runtimeEmailBuildMessage("EMAILDAY: attempt %d/%d - preparing PSRAM snapshot", attempt, maxAttempts), INFO);

            sendVirtualMessage(F("📤 EMAILDAY: Flushing activeBuffers to PSRAM..."), INFO);

            size_t psramUsedBefore = psramVFSGetUsedBytes();
            bool flushed = flushSdcardNowBlocking("EMAILDAY_PSRAM_PREPARE", 15000);
            if (!flushed) {
                sendVirtualMessage(F("⚠️ EMAILDAY: Flush timeout (PSRAM), but proceeding..."), WARN);
            } else {
                size_t psramUsedAfter = psramVFSGetUsedBytes();
                sendVirtualMessage(runtimeEmailBuildMessage("✅ EMAILDAY: Flushed to PSRAM (before=%u KB, after=%u KB)",
                                                              (unsigned)(psramUsedBefore / 1024),
                                                              (unsigned)(psramUsedAfter / 1024)), DEBUG);
            }

            sendVirtualMessage(F("⏳ EMAILDAY: Waiting 800ms for PSRAM operations to stabilize..."), DEBUG);
            for (int settle = 0; settle < 2; ++settle) {
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(400));
            }
            esp_task_wdt_reset();

            sendVirtualMessage(F("📸 EMAILDAY: Enabling PSRAM snapshot (double buffering)"), INFO);
            psramVFSEnableSnapshot();

            size_t fileCount = psramVFSGetTotalFileCount();
            size_t totalBytes = psramVFSGetUsedBytes();
            sendVirtualMessage(runtimeEmailBuildMessage("✅ EMAILDAY: PSRAM snapshot ready (%u files, %u KB stable)",
                                                          (unsigned)fileCount,
                                                          (unsigned)(totalBytes / 1024)), INFO);
        }

        sendVirtualMessage(F("EMAILDAY: sending daily files"), INFO);

        bool mutexAcquired = false;
        if (!usesPsram) {
            if (!acquireSDAccess("EMAILDAY", 60000)) {
                sendVirtualMessage(F("? EMAILDAY: Failed to acquire SD mutex (timeout)"), ERROR);
                finalSuccess = false;
            } else {
                sendVirtualMessage(F("? EMAILDAY: SD mutex acquired, starting email send"), DEBUG);
                mutexAcquired = true;
            }
        } else {
            sendVirtualMessage(F("📧 EMAILDAY: PSRAM mode - no SD mutex needed"), INFO);
            mutexAcquired = true;
        }

        if (mutexAcquired) {
            String emailMethod = configManager.getEmailMethod();
            bool useGoogle = false;
            bool useSmtp = false;

#if ENABLE_GOOGLE_EMAIL==1
            bool googleAvailable = googleEmailIsAvailable();
#else
            bool googleAvailable = false;
#endif

#if ENABLE_SMTP==1
            bool smtpAvailable = true;
#else
            bool smtpAvailable = false;
#endif

            if (emailMethod == "google") {
                if (googleAvailable) {
                    useGoogle = true;
                    sendVirtualMessage(F("📧 EMAILDAY: Using Google Apps Script (configured)"), INFO);
                } else {
                    sendVirtualMessage(F("❌ EMAILDAY: Google Script not configured"), ERROR);
                    finalSuccess = false;
                }
            } else if (emailMethod == "smtp") {
                if (smtpAvailable) {
                    useSmtp = true;
                    sendVirtualMessage(F("📧 EMAILDAY: Using SMTP (configured)"), INFO);
                } else {
                    sendVirtualMessage(F("❌ EMAILDAY: SMTP not compiled"), ERROR);
                    finalSuccess = false;
                }
            } else {
                sendVirtualMessage(runtimeEmailBuildMessage("❌ EMAILDAY: Invalid email method '%s' (use 'smtp' or 'google')", emailMethod.c_str()), ERROR);
                finalSuccess = false;
            }

            emailSendActive = configManager.getSdcardEmailSend();

            if (emailSendActive) {
                sendVirtualMessage(F("📧 EMAILDAY: Email active - adding to circular queue"), INFO);
                if (!usesPsram) {
                    releaseSDAccess();
                    sendVirtualMessage(F("✅ EMAILDAY: SD mutex released (queue will re-acquire)"), DEBUG);
                } else {
                    sendVirtualMessage(F("🔒 EMAILDAY: PSRAM snapshot kept ACTIVE until queue processes"), INFO);
                }

                enqueueEmaildayEmail(localFolderPath, localSubject, localBody, localShiftEmails);
                finalSuccess = true;
            } else {
                sendVirtualMessage(F("📧 EMAILDAY: Email disabled - sending IMMEDIATELY (bypass queue)"), WARN);
                if (useGoogle) {
#if ENABLE_GOOGLE_EMAIL==1
                    finalSuccess = googleEmailSendEmailDay(localFolderPath, localSubject, localBody, (4 * 1024 * 1024 * 110) / 100, localShiftEmails);
#endif
                } else if (useSmtp) {
#if ENABLE_SMTP==1
                    extern bool enviarEmailComAnexosSD(const String &folderPath, const String &subject, const String &bodyText, size_t maxBytesPerEmail, const String &shiftEmails);
                    finalSuccess = enviarEmailComAnexosSD(localFolderPath, localSubject, localBody, (4 * 1024 * 1024 * 110) / 100, localShiftEmails);
#endif
                }

                if (!usesPsram) {
                    sendVirtualMessage(F("EMAILDAY: Direct send complete, releasing SD mutex"), DEBUG);
                    releaseSDAccess();
                    sendVirtualMessage(F("✅ EMAILDAY: SD mutex released"), DEBUG);
                }
            }
        }

        if (!usesPsram) {
            endEmaildayFreezeCleanup();
            esp_task_wdt_reset();

            bool flushed = false;
            for (int retryFlush = 0; retryFlush < 3 && !flushed; retryFlush++) {
                if (retryFlush > 0) {
                    sendVirtualMessage(String(F("EMAILDAY: Retry flush #")) + String(retryFlush), WARN);
                }
                flushed = flushSdcardNowBlocking("EMAILDAY_ATTEMPT_END", 60000);
                if (!flushed) {
                    esp_task_wdt_reset();
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
            }
            if (!flushed) {
                sendVirtualMessage(F("❌ EMAILDAY: Final flush failed after 3 retries"), ERROR);
            }
            esp_task_wdt_reset();
        } else if (!emailSendActive) {
            sendVirtualMessage(F("EMAILDAY: PSRAM direct send complete - disabling snapshot"), INFO);

            size_t pendingCount = 0;
            size_t pendingBytes = 0;
            psramVFSGetSnapshotStats(pendingCount, pendingBytes);
            sendVirtualMessage(runtimeEmailBuildMessage("📦 Pending writes: %u items, %u KB",
                                                          (unsigned)pendingCount,
                                                          (unsigned)(pendingBytes / 1024)), INFO);

            psramVFSDisableSnapshot();
            sendVirtualMessage(F("✅ EMAILDAY: PSRAM snapshot disabled, pending writes flushed"), DEBUG);
            esp_task_wdt_reset();
        }

        if (finalSuccess) {
            sendVirtualMessage(F("EMAILDAY: send completed successfully"), INFO);
            break;
        }

        sendVirtualMessage(F("EMAILDAY: send failed; will retry"), WARN);
        unsigned long backoffMs = (attempt <= 2) ? 10000UL : (attempt <= 4) ? 30000UL : 60000UL;
        unsigned long backoffStart = millis();
        while (millis() - backoffStart < backoffMs) {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    stackFree = uxTaskGetStackHighWaterMark(NULL);
    sendVirtualMessage(runtimeEmailBuildMessage("EMAILDAY: stack minimum = %u words", (unsigned)stackFree), DEBUG);

    if (!finalSuccess) {
        sendVirtualMessage(F("EMAILDAY: failed after retries"), ERROR);
    }
#if ENABLE_SCHEDULER==1
    // Direct-send path owns the receipt. Queued path is resolved by the queue
    // processor (processEmailQueue) when the deferred send completes.
    if (!emailSendActive) dayReportEndAttempt(finalSuccess);
#endif

    // When email was queued in PSRAM mode, skip cleanup here.
    // The queue processor owns the snapshot and will release it after sending.
    if (!(usesPsram && emailSendActive)) {
        endEmaildayFreezeCleanup();
    }

    localFolderPath = "";
    localSubject = "";
    localBody = "";

    priorityManager().endEmailday();
    sendVirtualMessage(F("🔓 EMAILDAY: released priority lock, downloads can resume"), INFO);

    esp_task_wdt_delete(NULL);

    emailDayInProgress = false;
    emailDayTaskHandle = NULL;
    vTaskDelete(NULL);
}

bool startEmailDayAsync(const String &folderPath, const String &subject, const String &body) {
    // CMD-7 duplicate guard (live .29 2026-10-14: one-shot fired twice, +60/+120 s
    // apart, and both sends delivered). Same (folder, subject) inside 3 min is a
    // duplicate from ANY caller (scheduler double-fire, journal race, API
    // double-click) — refuse loudly. Journal retries (20 min apart) and manual
    // re-sends pass through untouched. Suppression happens BEFORE the T24
    // archive write below so no second .txt is produced either.
    static EmailDayDedupeState s_emailDayDedupe; // zero-init: disarmed
    if (!emailDayResendAllowed(s_emailDayDedupe, folderPath, subject, (uint32_t)millis())) {
        sendVirtualMessage(String(F("EMAILDAY: duplicate suppressed (same folder+subject <3min): ")) + subject,
                           WARN, false, true);
        return false;
    }
    if (emailDayInProgress || emailDayTaskHandle != NULL) {
        // Self-healing: a task stuck past the watchdog window can never finish
        // (every phase has shorter timeouts), so release it and proceed instead
        // of blocking all future emails.
        if (emailDayInProgress && emailDayStartMs != 0 &&
            (millis() - emailDayStartMs >= EMAILDAY_STUCK_MS)) {
            emailDayForceCleanup("auto-recover on start");
        } else {
            sendVirtualMessage(F("⚠️ EMAILDAY: already in progress"), WARN, false, false);
            return false;
        }
    }
    emailDayStartMs = millis();
    emailDayFolderPath = folderPath;
    emailDaySubject = subject;
    emailDayBody = body;

    // T24: body .txt file (EMAILDAY/LOGDAY/SENSORDAY/SHIFTDAY, send
    // time), SD or LittleFS. SHIFTEND skips: T8b already writes the dated
    // turno_data (turnoN_YYYY-MM-DD_HH-MM.txt) with the same body.
    {
        String t24tipo = emailArchiveInferTipo(subject, "EMAILDAY");
        if (!t24tipo.equalsIgnoreCase(F("SHIFTEND"))) {
            emailArchiveSaveBody(t24tipo, body);
        }
    }

    // Combine command-specific extras with proximity shift emails into emailDayShiftEmails
    emailDayShiftEmails = "";
#if ENABLE_PROXIMITY == 1
    {
        const ProximityShiftConfig &sc = proximityGetShiftConfig();
        if (sc.shiftday_emails[0] != '\0') {
            emailDayShiftEmails = String(sc.shiftday_emails);
        }
    }
#endif
    if (emailDayCmdExtra.length() > 0) {
        emailDayShiftEmails = emailDayShiftEmails.length() > 0
            ? emailDayShiftEmails + "," + emailDayCmdExtra
            : emailDayCmdExtra;
    }
    emailDayCmdExtra = "";  // Clear after use

    emailDayInProgress = true;

    BaseType_t result = xTaskCreatePinnedToCore(
        emailDayTask,
        "EmailDay",
        32768, // 32KB: TLS + streaming + Strings; 24KB dava stack overflow -> PANIC
        NULL,
        1,
        &emailDayTaskHandle,
        0
    );

    if (result != pdPASS) {
        sendVirtualMessage(F("❌ EMAILDAY: failed to create task"), ERROR, false, false);
        emailDayTaskHandle = NULL;
        emailDayInProgress = false;
        emailDayFolderPath = "";
        emailDaySubject = "";
        emailDayBody = "";
        emailDayShiftEmails = "";
        sdcardFreezeWrites = false;
        sdcardFreezeCutoffSeq_RS232 = 0;
        sdcardFreezeCutoffSeq_LPT = 0;
        return false;
    }

    sendVirtualMessage(F("📧 EMAILDAY: task created, sending in background"), INFO, false, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    return true;
}
#endif // ENABLE_GOOGLE_EMAIL
#endif // ENABLE_SMTP || ENABLE_GOOGLE_EMAIL