#pragma once
#include <string>
#include <vector>

// Test-double for IEmailService — captures sent messages for verification.
// Does NOT inherit from the production interface (avoids Arduino dependency).
class MockEmailService {
public:
    struct SentMessage {
        std::string to;
        std::string subject;
        std::string body;
        bool success = true;
    };

private:
    std::vector<SentMessage> sent_;
    bool ready_ = true;
    bool nextResult_ = true;
    std::string lastError_;

public:
    bool init() { return true; }
    bool isReady() const { return ready_; }
    void setReady(bool r) { ready_ = r; }

    // Simulates send — records the message and returns nextResult_
    bool send(const std::string& to, const std::string& subject, const std::string& body) {
        SentMessage msg;
        msg.to = to;
        msg.subject = subject;
        msg.body = body;
        msg.success = nextResult_;
        sent_.push_back(msg);
        if (!nextResult_) lastError_ = "Mock send failure";
        return nextResult_;
    }

    void setNextResult(bool ok) { nextResult_ = ok; }
    const std::string& lastError() const { return lastError_; }

    // Test helpers
    size_t sentCount() const { return sent_.size(); }
    const SentMessage& lastSent() const {
        static SentMessage empty;
        return sent_.empty() ? empty : sent_.back();
    }
    const SentMessage& sentAt(size_t i) const {
        static SentMessage empty;
        return i < sent_.size() ? sent_[i] : empty;
    }
    void clear() { sent_.clear(); lastError_.clear(); }
};
