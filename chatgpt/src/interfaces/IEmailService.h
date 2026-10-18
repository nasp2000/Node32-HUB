#pragma once
#include <Arduino.h>
#include <functional>

struct EmailAddress {
    String name;
    String address;
};

struct EmailMessage {
    String subject;
    String body;
    String htmlBody;
    EmailAddress from;
    EmailAddress to;
    EmailAddress cc;
    bool isHtml;
    String attachmentPath;
    String attachmentName;
};

struct SmtpConfig {
    String host;
    uint16_t port = 587;
    String username;
    String password;
    EmailAddress from;
    bool useTls = true;
    uint32_t timeoutMs = 10000;
};

struct EmailSendResult {
    bool success;
    String message;
    uint32_t durationMs;
};

class IEmailService {
public:
    virtual ~IEmailService() = default;

    virtual bool init(const SmtpConfig& config) = 0;
    virtual void loop() = 0;

    virtual EmailSendResult send(const EmailMessage& msg) = 0;
    virtual bool sendAsync(const EmailMessage& msg) = 0;

    virtual bool isReady() const = 0;
    virtual bool isSending() const = 0;
    virtual uint32_t queueDepth() const = 0;
    virtual void clearQueue() = 0;

    virtual void setSendCallback(std::function<void(const EmailSendResult&)> cb) = 0;
};

class IGoogleEmailService : public IEmailService {
public:
    virtual ~IGoogleEmailService() = default;
    virtual bool setScriptUrl(const String& url) = 0;
    virtual String getScriptUrl() const = 0;
};
