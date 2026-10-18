#include "google_apps_script_asset.h"

// Shared asset served by WiFi and Ethernet to avoid duplicate flash literals.
const char GOOGLE_APPS_SCRIPT_EMAIL_HANDLER_JS[] PROGMEM = R"GAS(// =============================================================
// [EMAIL] Google Apps Script Email Handler for ESP32
// =============================================================
// Deploy this script in Google Apps Script and copy the Web App URL
// to configure it on the ESP32 (the "Google Script URL" field).
//
// Deploy instructions:
// 1. Open https://script.google.com/
// 2. Create a new project
// 3. Paste this code
// 4. CUSTOMIZE the variables below (PROJECT_NAME, DEVICE_NAME, ADMIN_EMAIL)
// 5. Deploy -> New deployment
// 6. Type: Web app
// 7. Execute as: Me (your@email.com)
// 8. Who has access: Anyone
// 9. Copy the Web App URL to the ESP32
//
// [TEST] Testing:
// - Execute testGmailPermissions() manually to verify Gmail access
// - Execute testSimpleEmail() to send a test email
// - Check logs: View -> Logs (Ctrl+Enter)
//
// [WARN] Troubleshooting common errors:
// - "Cannot read properties of undefined": Redeploy with "New version"
// - "Execution cancelled": Gmail quota exhausted (500 emails/day limit)
// - "Invalid request": Check ESP32 is sending correct JSON format
// - "Session expired": Increase CACHE_EXPIRY_SECONDS if uploads are slow
// - Permission errors: Ensure "Execute as: Me" and Gmail scope granted
//
// Features:
// - Simple email (no attachments)
// - Chunked file upload (50KB chunks)
// - Session management (30min TTL)
// - Automatic cleanup
// - Error handling with detailed logs
// - Gmail quota checking

// =============================================================
// [CONFIG] CUSTOMIZE THESE VARIABLES BEFORE DEPLOY
// =============================================================

// [INFO] Project/device information shown in logs
const PROJECT_NAME = "Node32-Hub";           // Your project name
const DEVICE_NAME = "ESP32-001";             // Device name/ID
const ADMIN_EMAIL = "admin@example.com";     // Admin email for fallback/notifications

// [PACKAGE] Technical settings (adjust as needed)
const CONFIG = {
  CACHE_EXPIRY_SECONDS: 1800,  // 30 minutes session TTL
  MAX_CHUNK_SIZE_KB: 100,      // Maximum chunk size (100KB safety limit)
  MAX_FILE_SIZE_MB: 4,         // Email attachment limit (4MB, enforced by ESP32)
  LOG_ENABLED: true            // Enable detailed logging in Apps Script console
};

// =============================================================
// [MAIN] MAIN ENTRY POINT
// =============================================================

// GET handler - Health check / Status endpoint
function doGet(e) {
  try {
    const status = {
      status: 'active',
      project: PROJECT_NAME,
      device: DEVICE_NAME,
      version: '2.0',
      config: {
        maxChunkSizeKB: CONFIG.MAX_CHUNK_SIZE_KB,
        maxFileSizeMB: CONFIG.MAX_FILE_SIZE_MB,
        sessionTTLMinutes: CONFIG.CACHE_EXPIRY_SECONDS / 60
      },
      timestamp: new Date().toISOString()
    };

    return ContentService.createTextOutput(JSON.stringify(status, null, 2))
      .setMimeType(ContentService.MimeType.JSON);

  } catch(err) {
    return ContentService.createTextOutput(JSON.stringify({
      status: 'error',
      error: err.toString()
    })).setMimeType(ContentService.MimeType.JSON);
  }
}

function doPost(e) {
  try {
    // Defensive check: ensure event object exists
    if (!e || !e.parameter) {
      log_('ERROR: Invalid request - missing event or parameters');
      return jsonError('Invalid request: missing event data');
    }

    const action = e.parameter.action || 'simple';

    log_('Received request: ' + action);

    switch(action) {
      case 'simple':
        return handleSimpleEmail(e);
      case 'start':
        return handleStartSession(e);
      case 'chunk':
        return handleChunkUpload(e);
      case 'finish':
        return handleFinishSession(e);
      case 'cancel':
        return handleCancelSession(e);
      default:
        return jsonError('Unknown action: ' + action);
    }

  } catch(err) {
    log_('ERROR: ' + err.toString());
    return jsonError(err.toString());
  }
}

// =============================================================
// [EMAIL] SIMPLE EMAIL (No Attachments)
// =============================================================

function handleSimpleEmail(e) {
  const data = parseJsonBody(e);

  if (!data.to || !data.subject || !data.body) {
    log_('ERROR: Missing required fields for simple email');
    return jsonError('Missing required fields: to, subject, body');
  }

  // Validate email addresses
  const recipients = data.to.split(',');
  for (let i = 0; i < recipients.length; i++) {
    const email = recipients[i].trim();
    if (!email || email.indexOf('@') < 0) {
      log_('ERROR: Invalid email address: ' + email);
      return jsonError('Invalid email address: ' + email);
    }
  }

  try {
    // Check Gmail quota
    try {
      const remainingQuota = MailApp.getRemainingDailyQuota();
      log_('Simple: Gmail quota remaining: ' + remainingQuota + ' emails');
      if (remainingQuota < 1) {
        log_('ERROR: Gmail daily quota exhausted');
        return jsonError('Gmail daily quota exhausted. Please try again tomorrow.');
      }
    } catch(quotaErr) {
      log_('WARNING: Could not check Gmail quota: ' + quotaErr.toString());
      // Continue anyway
    }

    log_('Simple: Sending email to ' + data.to + '...');
    GmailApp.sendEmail(data.to, data.subject, data.body);
    log_('Simple: Email sent successfully to ' + data.to);
    return jsonSuccess({message: 'Email sent', recipients: data.to});
  } catch(err) {
    log_('ERROR: Failed to send simple email: ' + err.toString());
    log_('ERROR stack: ' + err.stack);
    return jsonError('Failed to send email: ' + err.toString());
  }
}

// =============================================================
// [SESSION] SESSION START (Initialize Upload)
// =============================================================

function handleStartSession(e) {
  const data = parseJsonBody(e);

  if (!data.filename || !data.to || !data.subject) {
    return jsonError('Missing required fields: filename, to, subject');
  }

  // Generate unique session ID
  const sessionId = Utilities.getUuid();

  // Store session metadata
  const metadata = {
    filename: data.filename,
    to: data.to,
    subject: data.subject,
    body: data.body || '',
    chunks: [],
    createdAt: new Date().toISOString()
  };

  const cache = CacheService.getScriptCache();
  cache.put(sessionId, JSON.stringify(metadata), CONFIG.CACHE_EXPIRY_SECONDS);

  log_('Session started: ' + sessionId + ' for file: ' + data.filename);

  return ContentService.createTextOutput(sessionId)
    .setMimeType(ContentService.MimeType.TEXT);
}

// =============================================================
// [SESSION] CHUNK UPLOAD (Store Chunk)
// =============================================================

function handleChunkUpload(e) {
  const data = parseJsonBody(e);

  if (!data.sessionId || data.chunkNum === undefined || !data.data) {
    return jsonError('Missing required fields: sessionId, chunkNum, data');
  }

  const cache = CacheService.getScriptCache();
  const metaStr = cache.get(data.sessionId);

  if (!metaStr) {
    return jsonError('Session not found or expired: ' + data.sessionId);
  }

  const metadata = JSON.parse(metaStr);

  // Validate chunk size (prevent abuse)
  const chunkSizeKB = data.data.length / 1024;
  if (chunkSizeKB > CONFIG.MAX_CHUNK_SIZE_KB) {
    return jsonError('Chunk too large: ' + chunkSizeKB.toFixed(2) + 'KB (max: ' + CONFIG.MAX_CHUNK_SIZE_KB + 'KB)');
  }

  // Store chunk
  metadata.chunks[data.chunkNum] = data.data;

  // Update session
  cache.put(data.sessionId, JSON.stringify(metadata), CONFIG.CACHE_EXPIRY_SECONDS);

  log_('Chunk ' + data.chunkNum + ' received (' + chunkSizeKB.toFixed(2) + 'KB)');

  return jsonSuccess({message: 'Chunk received', chunkNum: data.chunkNum});
}

// =============================================================
// [EMAIL] FINISH SESSION (Assemble and Send)
// =============================================================

function handleFinishSession(e) {
  const data = parseJsonBody(e);

  if (!data.sessionId) {
    return jsonError('Missing sessionId');
  }

  log_('Finish: Starting session finalization: ' + data.sessionId);

  const cache = CacheService.getScriptCache();
  const metaStr = cache.get(data.sessionId);

  if (!metaStr) {
    return jsonError('Session not found or expired: ' + data.sessionId);
  }

  let metadata;
  try {
    metadata = JSON.parse(metaStr);
    log_('Finish: Metadata parsed successfully (' + metadata.chunks.length + ' chunks)');
  } catch(parseErr) {
    log_('ERROR: Failed to parse metadata: ' + parseErr.toString());
    cache.remove(data.sessionId);
    return jsonError('Corrupted session metadata: ' + parseErr.toString());
  }

  try {
    log_('Finish: Starting session finalization: ' + data.sessionId);

    // Check Gmail quota before processing
    try {
      const remainingQuota = MailApp.getRemainingDailyQuota();
      log_('Finish: Gmail quota remaining: ' + remainingQuota + ' emails');
      if (remainingQuota < 1) {
        log_('ERROR: Gmail daily quota exhausted');
        cache.remove(data.sessionId);
        return jsonError('Gmail daily quota exhausted. Please try again tomorrow.');
      }
    } catch(quotaErr) {
      log_('WARNING: Could not check Gmail quota: ' + quotaErr.toString());
      // Continue anyway - quota check is not critical
    }

    // Assemble all chunks
    log_('Finish: Assembling ' + metadata.chunks.length + ' chunks...');
    let fullBase64 = '';
    for (let i = 0; i < metadata.chunks.length; i++) {
      if (metadata.chunks[i] === undefined || metadata.chunks[i] === null) {
        log_('ERROR: Missing chunk ' + i);
        cache.remove(data.sessionId);
        return jsonError('Missing chunk ' + i);
      }
      fullBase64 += metadata.chunks[i];
    }

    const base64Length = fullBase64.length;
    log_('Finish: Base64 assembled (' + (base64Length / 1024).toFixed(2) + ' KB)');

    // Decode base64
    log_('Finish: Decoding Base64...');
    const binaryData = Utilities.base64Decode(fullBase64);
    log_('Finish: Base64 decoded (' + (binaryData.length / 1024).toFixed(2) + ' KB binary)');

    // Create blob
    log_('Finish: Creating blob...');
    const blob = Utilities.newBlob(
      binaryData,
      'application/octet-stream',
      metadata.filename
    );

    // Check file size
    const fileSizeMB = blob.getBytes().length / (1024 * 1024);
    log_('Finish: Blob created (' + fileSizeMB.toFixed(2) + ' MB)');

    if (fileSizeMB > CONFIG.MAX_FILE_SIZE_MB) {
      log_('ERROR: File too large: ' + fileSizeMB.toFixed(2) + 'MB (max: ' + CONFIG.MAX_FILE_SIZE_MB + 'MB)');
      cache.remove(data.sessionId);
      return jsonError('File too large: ' + fileSizeMB.toFixed(2) + 'MB (max: ' + CONFIG.MAX_FILE_SIZE_MB + 'MB)');
    }

    // Validate email addresses
    const recipients = metadata.to.split(',');
    for (let i = 0; i < recipients.length; i++) {
      const email = recipients[i].trim();
      if (!email || email.indexOf('@') < 0) {
        log_('ERROR: Invalid email address: ' + email);
        cache.remove(data.sessionId);
        return jsonError('Invalid email address: ' + email);
      }
    }

    // Send email with attachment
    log_('Finish: Sending email to ' + metadata.to + '...');
    GmailApp.sendEmail({
      to: metadata.to,
      subject: metadata.subject,
      body: metadata.body,
      attachments: [blob]
    });

    log_('Finish: Email sent successfully!');

    // Cleanup session
    cache.remove(data.sessionId);

    log_('Email sent successfully: ' + metadata.filename + ' (' + fileSizeMB.toFixed(2) + 'MB) to ' + metadata.to);

    return jsonSuccess({
      message: 'Email sent',
      filename: metadata.filename,
      sizeMB: fileSizeMB.toFixed(2),
      chunks: metadata.chunks.length
    });

  } catch(err) {
    log_('ERROR in finish session: ' + err.toString());
    log_('ERROR stack: ' + err.stack);
    cache.remove(data.sessionId); // Cleanup on error
    return jsonError('Failed to send email: ' + err.toString());
  }
}

// =============================================================
// [CLEANUP] CANCEL SESSION (Cleanup)
// =============================================================

function handleCancelSession(e) {
  const data = parseJsonBody(e);

  if (!data.sessionId) {
    return jsonError('Missing sessionId');
  }

  const cache = CacheService.getScriptCache();
  cache.remove(data.sessionId);

  log_('Session cancelled: ' + data.sessionId);

  return jsonSuccess({message: 'Session cancelled'});
}

// =============================================================
// [TOOLS] HELPER FUNCTIONS
// =============================================================

function parseJsonBody(e) {
  try {
    // Defensive check: ensure postData exists
    if (!e || !e.postData || !e.postData.contents) {
      throw new Error('Missing POST data');
    }
    return JSON.parse(e.postData.contents);
  } catch(err) {
    throw new Error('Invalid JSON: ' + err.toString());
  }
}

function jsonSuccess(data) {
  const response = {
    success: true,
    timestamp: new Date().toISOString(),
    data: data
  };
  return ContentService.createTextOutput(JSON.stringify(response))
    .setMimeType(ContentService.MimeType.JSON);
}

function jsonError(message) {
  const response = {
    success: false,
    error: message,
    timestamp: new Date().toISOString()
  };
  return ContentService.createTextOutput(JSON.stringify(response))
    .setMimeType(ContentService.MimeType.JSON);
}

function log_(message) {
  if (CONFIG.LOG_ENABLED) {
    Logger.log('[' + PROJECT_NAME + '/' + DEVICE_NAME + '] ' + message);
  }
}

// =============================================================
// [DIAG] DIAGNOSTIC FUNCTIONS
// =============================================================

// Test Gmail permissions and basic functionality
function testGmailPermissions() {
  try {
    const quota = MailApp.getRemainingDailyQuota();
    Logger.log('[OK] Gmail API accessible');
    Logger.log('[STATS] Remaining daily quota: ' + quota + ' emails');

    // Test email send (to yourself)
    const userEmail = Session.getEffectiveUser().getEmail();
    if (userEmail) {
      GmailApp.sendEmail(
        userEmail,
        '[TEST] Gmail Permission Check',
        'This is a test email to verify Gmail permissions are working correctly.\n\nSent from: ' + PROJECT_NAME + '\nTimestamp: ' + new Date().toISOString()
      );
      Logger.log('[OK] Test email sent to: ' + userEmail);
      return '[OK] Gmail permissions are working correctly! Check your inbox.';
    } else {
      Logger.log('[WARN] Could not determine user email');
      return '[WARN] Gmail API accessible but email not available';
    }
  } catch(err) {
    Logger.log('[ERROR] Gmail permission error: ' + err.toString());
    return '[ERROR] ERROR: ' + err.toString() + '\n\nPlease ensure:\n1. Script is deployed as Web App\n2. "Execute as: Me" is selected\n3. Gmail permissions are granted';
  }
}

// =============================================================
// [TEST] TEST FUNCTIONS (for development)
// =============================================================

function testSimpleEmail() {
  const e = {
    postData: {
      contents: JSON.stringify({
        to: 'your-email@example.com',
        subject: 'Test from Google Apps Script',
        body: 'This is a test email sent from ESP32 via Google Apps Script.'
      })
    },
    parameter: {
      action: 'simple'
    }
  };

  const result = doPost(e);
  Logger.log(result.getContent());
}

function testChunkedUpload() {
  // Start session
  const startE = {
    postData: {
      contents: JSON.stringify({
        filename: 'test.txt',
        to: 'your-email@example.com',
        subject: 'Test Chunked Upload',
        body: 'File attached via chunked upload'
      })
    },
    parameter: {
      action: 'start'
    }
  };

  const sessionId = doPost(startE).getContent();
  Logger.log('Session ID: ' + sessionId);

  // Upload chunk
  const testData = 'Hello, this is test data!';
  const base64Data = Utilities.base64Encode(testData);

  const chunkE = {
    postData: {
      contents: JSON.stringify({
        sessionId: sessionId,
        chunkNum: 0,
        data: base64Data
      })
    },
    parameter: {
      action: 'chunk'
    }
  };

  Logger.log(doPost(chunkE).getContent());

  // Finish session
  const finishE = {
    postData: {
      contents: JSON.stringify({
        sessionId: sessionId
      })
    },
    parameter: {
      action: 'finish'
    }
  };

  Logger.log(doPost(finishE).getContent());
}
)GAS";