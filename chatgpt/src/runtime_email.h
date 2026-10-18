#ifndef RUNTIME_EMAIL_H
#define RUNTIME_EMAIL_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <tuple>
#include <vector>

#include "feature_flags.h"

enum EmailTriggerSource : uint8_t {
	TRIGGER_NONE,
	TRIGGER_MANUAL_COMMAND,
	TRIGGER_AUTO_INACTIVITY_ENABLED,
	TRIGGER_AUTO_INACTIVITY_DISABLED_LONG_TIMEOUT,
	TRIGGER_AUTO_LINE_LIMIT,
	TRIGGER_AUTO_BUFFER_FULL,
	TRIGGER_AUTO_FORM_FEED
};

enum EmailSendingState {
	EMAIL_IDLE,
	EMAIL_TRIGGERED,
	EMAIL_WAITING_FOR_SENDER_STOP,
	EMAIL_START_SENDING,
	EMAIL_CHECK_TRANSPORT,
	EMAIL_PREPARE_CONTENT,
	EMAIL_CONNECTING_SMTP,
	EMAIL_AUTHENTICATING_SMTP,
	EMAIL_SENDING_MAIN_MESSAGE,
	EMAIL_SENDING_INDIVIDUAL_MESSAGES,
	EMAIL_FINISHED_SUCCESS,
	EMAIL_FINISHED_FAILURE,
	EMAIL_DELAY_BUSY_DEACTIVATION
};

inline constexpr int EMAIL_ATTEMPTS_PER_CYCLE = 3;
inline constexpr int EMAIL_NUM_CYCLES = 2;
inline constexpr unsigned long EMAIL_DELAY_BETWEEN_ATTEMPTS = 5000;
inline constexpr unsigned long EMAIL_DELAY_BETWEEN_CYCLES = 10000;
inline constexpr unsigned long BUSY_DEACTIVATION_DELAY_MS = 1000;

extern EmailSendingState currentEmailSendingState;
extern String emailContentToSend;
extern String emailMatchedRulesToFinalize;
extern String emailRawDataCache;
extern String emailTablesDataCache;
extern unsigned long lastEmailActionTime;
extern int emailAttemptCount;
extern int emailCycleCount;
extern unsigned long busyDeactivationStartTime;
extern std::vector<std::tuple<String, String, String>> foundRecipientsForEmail;
extern std::vector<String> foundNumericLabels;
extern EmailTriggerSource lastEmailTriggerSource;
extern bool emailProcessInitiatedThisCycle;
extern bool isEmailSendingActive;
extern bool emailNoRecipientFlag;

#if ENABLE_SMTP==1
extern TaskHandle_t emailTestTaskHandle;
extern volatile bool emailTestInProgress;
extern String emailTestSubject;
extern String emailTestBody;
extern String emailTestCustomRecipient;
extern bool emailTestForceBodyOnlyOnce;
extern SemaphoreHandle_t smtpMutex;
extern SemaphoreHandle_t emailQueueMutex;
extern SemaphoreHandle_t ramEmailQueueMutex;
extern SemaphoreHandle_t emailQueueProcessorMutex;

bool smtpLock(TickType_t timeoutTicks);
void smtpUnlock();
void runtimeEmailInitBackgroundTasks();
bool runtimeEmailStateMachineIdle();
void enqueueStreamingEmail(const String &content, const String &matchedRules = String());
void enqueueEmaildayEmail(const String &folderPath, const String &subject, const String &bodyText, const String &shiftEmails = "");
void processEmailQueue();
size_t runtimeEmailGetStreamQueueCount();
unsigned long runtimeEmailGetLastStreamEmailSentTime();
void startEmailSending(const String &content, EmailTriggerSource triggerSource, const String &matchedRules = String());
void runtimeEmailCollectUniqueRecipients(const String &listRaw, std::vector<String> &addedLower, std::vector<String> &addedEmails);
String runtimeEmailBuildRecipientsLog(const std::vector<String> &emails);
bool startEmailTestAsync(const String &subject, const String &body);
bool startUnikonEmailAsync(const String &subject, const String &body);
bool startInactivityAlertAsync(const String &recipients, const String &subject, const String &body);

extern TaskHandle_t inactivityAlertTaskHandle;
extern volatile bool inactivityAlertInProgress;
extern String inactivityAlertSubject;
extern String inactivityAlertBody;
extern String inactivityAlertRecipients;
#endif

#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
// T24 — .txt body file of ALL emails (T8b standard turno_data).
// Grava body em /{sys}/<tipo>_<YYYY-MM-DD>_<HH-MM>.txt (hora de envio),
// SD when (sdMode && mounted) otherwise the same path in LittleFS.
// Nao altera Wi-Fi nem configuracao de email. Definido em runtime_email.cpp.
bool emailArchiveSaveBody(const String &tipo, const String &body);
#endif

#if ENABLE_SMTP==1
extern TaskHandle_t emailDayTaskHandle;
extern volatile bool emailDayInProgress;
extern volatile unsigned long emailDayStartMs;
#define EMAILDAY_STUCK_MS (10UL * 60UL * 1000UL)
extern String emailDayFolderPath;
extern String emailDaySubject;
extern String emailDayBody;
extern String emailDayShiftEmails;
extern String emailDayCmdExtra;  // Set by caller before startEmailDayAsync to inject per-command extra recipients

bool startEmailDayAsync(const String &folderPath, const String &subject, const String &body);
// Safe cleanup for a stuck/dead EMAILDAY send (never kills a live task).
void emailDayForceCleanup(const char* reason);
// Watchdog: call from the main loop; releases sends stuck past EMAILDAY_STUCK_MS.
void emailDayWatchdogTick();
#endif

#endif // RUNTIME_EMAIL_H