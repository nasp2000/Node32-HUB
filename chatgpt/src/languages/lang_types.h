#pragma once

// Translatable string table.
// All char* members point to PROGMEM strings.
// To add a new language: add its data to lang_active.cpp, declare LangStrings
// for it, and add the branch in setupLanguage().
struct LangStrings {
    // --- Email footer (buildEmailInfoTable) ---
    const char* emailSentFrom;       // "This email was sent from"
    const char* emailRuleChosen;     // "Rule triggered"
    const char* emailOperator;       // "Operator"
    const char* emailValueFound;     // "Value found"
    const char* emailDateLabel;       // "Date" (calendar date label)
    const char* emailDate;           // "Time" (hour/time label)
    const char* emailLine;           // "Line"
    const char* emailFreqNote;       // "This notification has a configured frequency of"
    const char* emailFirmware;       // "Firmware"
    const char* emailLocalIP;        // "Local IP"
    const char* emailConnectedWifi;  // "Connected via WiFi"
    const char* emailConnectedEth;   // "Connected via Ethernet"
    const char* emailConnectedNA;    // "N/A"

    // --- Test email ---
    const char* emailTestSubject;    // "Email Test"
    const char* emailTestBody;       // "This is a test! Your email configuration is correct."
    const char* emailTestFreq;       // "one-time send"

    // --- Inactivity alert email ---
    const char* emailInactivitySubject; // "Inactivity Alert"
    const char* timeMinutes;            // "minutes"
    const char* timeHour;               // "hour"
    const char* timeHours;              // "hours"
    const char* emailInactivityPrefix;  // "The device has not received data for "
    const char* emailInactivityAlert;   // ". (Alert "
    const char* emailMaxAlertPrefix;    // "Max "
    const char* emailMaxAlertSuffix;    // " alerts/day"

    // --- Rule/alert emails ---
    const char* emailRulesSuffix;    // " - Rules"
    const char* emailFoundPrefix;    // " - Found ("
    const char* emailAlways;         // "always"
    const char* emailFromName;       // "Automated Data"
    const char* emailDataAttached;   // "Your data is attached!"
    const char* emailDataInBody;     // "Your data is above in the body of this email."

    // --- Operator labels (used in email subjects and bodies) ---
    const char* opLessThan;          // "less than"
    const char* opGreaterThan;       // "greater than"
    const char* opNotEqual;          // "different"
    const char* opEqual;             // "equal"
    const char* opGreaterOrEqual;    // "greater or equal"
    const char* opLessOrEqual;       // "less or equal"
    const char* opInterval;          // "interval"
    const char* opOutside;           // "outside interval"
    const char* opVariation;         // "variation"
    const char* opCount;             // "count"
    const char* opPresence;          // "presence"
    const char* opTimeWindow;        // "time window"
    const char* opHysteresis;        // "hysteresis"

    // --- Scheduler frequency labels ---
    const char* schedAutomatic;      // "automatic schedule"
    const char* schedDailyAt;        // "daily at "
    const char* schedOneTimeOn;      // "one-time on "

    // --- Sensor data labels ---
    const char* sensorAlcohol;       // "Alcohol"
    const char* sensorBenzene;       // "Benzene"
    const char* sensorWarmingUp;     // " [warming up]"
    const char* emailSensorAlert;    // "Sensor Alert"
    const char* emailAlarmImmediate; // "immediate alarm"

    // --- Rambox screen labels ---
    const char* screenSignal;        // "Signal"
    const char* screenContest;       // "Contest"
    const char* screenTraining;      // "Training"
    const char* screenNoPigeons;     // "No pigeons"
    const char* screenRing;          // "Ring"
    const char* screenTime;          // "Time"
    const char* screenClock;         // "Clock"

    // --- Shift email stacked time-bar legend ---
    const char* emailTimeProd;       // "Production"
    const char* emailTimePlan;       // "Planned"
    const char* emailTimeUnplan;     // "Unplanned"

    // --- Shift email body labels ---
    const char* emailShiftRecords;   // "Records:"
    const char* emailShiftName;      // "Shift"
    const char* emailShiftScrap;     // "scrap"
    const char* emailShiftNoTarget;  // "OEE N/A (set Target PPM)"
    const char* emailShiftPcsHour;   // "Pcs per hour:"

    // --- OEE alert email labels ---
    const char* emailOeeTag;         // "OEE Alert"
    const char* emailOeePrefix;      // "OEE alert: "
    const char* emailOeeThresholdLbl;// "threshold"
    const char* emailOeeMetricShift; // "Shift OEE"
    const char* emailOeeMetricDay;   // "Day OEE"
    const char* emailOeeMetricAvail; // "Availability"
    const char* emailOeeDayPieces;   // "Day pieces so far: "
    const char* emailOeeShiftPieces; // "Shift pieces so far: "

    // --- Shift email subject tags ---
    const char* emailShiftEndTag;    // "ShiftEnd"
    const char* emailShiftDayTest;   // "Test - ShiftDay"

    // --- OLED screen labels ---
    const char* screenBoot;
    const char* screenSent;
    const char* screenCommand;
    const char* screenArrivals;
    const char* screenRadioLabel;
    const char* screenRadioPlaying;
    const char* screenRadioStopped;
    const char* screenTempNoReading;
    const char* screenGasWarmup;
    const char* screenGasNoInit;
    const char* screenUnikonPlus;
    const char* screenUnikonOffline;
    const char* screenBriconPlus;
    const char* screenBriconOffline;
    const char* screenSchedLabel;

    // --- API messages surfaced in the browser (JSON responses) ---
    const char* apiOtaErrFileType;
    const char* apiOtaErrTooLarge;
    const char* apiOtaErrSignature;
    const char* apiOtaErrSignatureAlert;
    const char* apiOtaDoneRestarting;
    const char* apiEmailTestBusy;
    const char* apiEmailTestNoConfig;
    const char* apiEmailTestStarted;
    const char* apiEmailTestStartFail;
    const char* apiNetNoChange;
    const char* apiNetTelnetSaved;
    const char* apiErrPsramUnsupported;
    const char* apiSdBusy;

    // --- TouchMeteo-7 display ---
    const char* touch7LocalSunset;
    const char* touch7MenuClock;
    const char* touch7MenuAnalog;
    const char* touch7MenuScreen;
    const char* touch7SkyClear;
    const char* touch7WxMostlyClear;
    const char* touch7WxPartlyCloudy;
    const char* touch7WxCloudy;
    const char* touch7WxFog;
    const char* touch7WxDrizzle;
    const char* touch7WxRain;
    const char* touch7WxSnow;
    const char* touch7WxShowers;
    const char* touch7WxSnowShowers;
    const char* touch7WxThunder;
    const char* touch7SkyVariable;
    const char* touch7MenuArc;
    const char* touch7TodayForecast;
    // --- Ethernet bootstrap pages ---
    const char* ethQuickStatus;
    const char* ethOpenFullUi;
    const char* ethFullUiAvailable;
    const char* ethFullUiUnavailable;
    const char* ethScopeTitle;
    const char* ethScopeDesc;
    const char* ethMonitorDesc;
    const char* ethNavHome;
    const char* ethBtnRefresh;
    const char* ethBtnClearBuffer;
    const char* ethLabelState;
    const char* ethLabelLoading;
    const char* ethViewTitle;
    const char* ethViewDesc;

    // --- HTML <title> per page ---
    const char* pageTitleHx711;
    const char* pageTitleMax31865;
    const char* pageTitleMq135;
    const char* pageTitleFalas;
    const char* pageTitleUda1334a;
    const char* pageTitleSound;
    const char* pageTitleAccess;
    const char* pageTitleEcra;
    const char* pageTitleNetworksOther;
    const char* pageTitleAmbient;

    // --- API messages surfaced in the browser (JSON responses) ---
    const char* apiTouch7NoMemory;

    // --- API messages surfaced in the browser (JSON responses) ---
    const char* apiErrOtaBegin;
    const char* apiCanSendFail;
    const char* apiCanCfgSaved;
    const char* apiCanRestartFail;
    const char* apiNotWritten;
    const char* apiErrSaveRecipient;
    const char* apiErrSaveExtraEmails;
    const char* apiErrSaveSched;
    const char* apiErrSaveSchedDate;
    const char* apiErrSaveReboot;
    const char* apiCanNotCompiled;
    const char* apiErrSaveRs232Template;
    const char* apiFileNotWritten;
    const char* apiUnikonCfgSaved;
    const char* apiBriconCfgActive;
    const char* apiBriconCfgOff;
    const char* apiBriconDisabled;
    const char* apiBriconPending;
    const char* apiBriconNothingReset;
    const char* apiProfileRestored;   // "Module profile applied again" (bench-mode restore)
    const char* apiJsonInvalid;
    const char* apiCfgSaved;
    const char* apiEmailDisabled;
    const char* apiErrSaveModbus;
    const char* apiNetNotFound;
    const char* apiErrSaveWifiState;
    const char* apiErrSaveAp;
    const char* apiErrSaveWifiNet;
    const char* apiErrRemoveWifi;
    const char* apiErrSave;
    const char* apiOtaNotCompiled;
    const char* apiErrSaveCurrentTemplate;
    const char* apiNoNewData;
    const char* apiErrSaveMq135;
    const char* apiNoValidRs;
    const char* apiErrSaveHx711;
    const char* apiTareFail;
    const char* apiErrSaveIna226;
    const char* apiErrSaveMax31865;
    const char* apiErrSaveSound;
    const char* apiErrSaveBh1750;
    const char* apiNotDeleted;
    const char* apiOtaNotEnabled;
    const char* apiOtaWriteFail;
    const char* apiNetApSaved;
    const char* apiNetWifiAdded;
    const char* apiNetWifiRemoved;
    const char* apiOtaErrOta;
    const char* tabLabelState;
    const char* tabLabelMonitor;
    const char* tabLabelSend;
    const char* tabLabelConfig;
    const char* tabLabelDevices;
    const char* tabLabelOutput;
    const char* apCfgSavedConnecting;
    const char* apiOtaUpdateFail;
    const char* apiOtaErrTooSmall;
    const char* ethStorageFlash;
    const char* ethStoragePsram;
    const char* ethStorageSd;
    const char* ethPsramSpace;
    const char* ethFlashSpace;
    const char* ethStorageLabel;
    const char* ethStorageUnavailable;
    const char* apiErrNotMounted;
    const char* apiOpDone;
    const char* apiErrBitrate;
    const char* apiErrBadIndex;
};
