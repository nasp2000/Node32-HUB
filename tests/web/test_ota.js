// OTA Page Tests — firmware validation logic (magic bytes, chip ID, file extension)
var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

// Standalone reimplementation of OTA validation logic from page_ota_asset.cpp
var VALID_CHIP_IDS = [2, 9, 18]; // ESP32-S2=2, ESP32-S3=9, ESP32-P4=18

function validateFirmwareBytes(a) {
  var result = { valid: true, errors: [], chipId: null, magic: null };
  if (!a || a.length < 32) {
    result.valid = false;
    result.errors.push('too_small');
    return result;
  }
  result.magic = a[0];
  // ZIP check: 0x50 0x4B
  if (a[0] === 0x50 && a[1] === 0x4B) {
    result.valid = false;
    result.errors.push('zip');
    return result;
  }
  // ELF check: 0x7F 'E' 'L' 'F'
  if (a[0] === 0x7F && a[1] === 0x45 && a[2] === 0x4C && a[3] === 0x46) {
    result.valid = false;
    result.errors.push('elf');
    return result;
  }
  // ESP32 magic: 0xE9
  if (a[0] !== 0xE9) {
    result.valid = false;
    result.errors.push('bad_magic');
  }
  // Chip ID at offset 12
  result.chipId = a[12];
  if (VALID_CHIP_IDS.indexOf(result.chipId) < 0) {
    result.valid = false;
    result.errors.push('unknown_chip_' + result.chipId);
  }
  return result;
}

function validateFileName(name) {
  if (!name) return { valid: false, error: 'no_file' };
  if (!name.endsWith('.bin')) return { valid: false, error: 'not_bin' };
  return { valid: true, error: null };
}

function run() {
  console.log('  --- validateFileName ---');
  test('validateFileName: valid .bin file', function() {
    var r = validateFileName('firmware.bin');
    ok(r.valid); is(r.error, null);
  });
  test('validateFileName: missing file', function() {
    var r = validateFileName(null);
    ok(!r.valid); is(r.error, 'no_file');
  });
  test('validateFileName: wrong extension', function() {
    var r = validateFileName('firmware.zip');
    ok(!r.valid); is(r.error, 'not_bin');
  });
  test('validateFileName: no extension', function() {
    var r = validateFileName('firmware');
    ok(!r.valid); is(r.error, 'not_bin');
  });

  console.log('  --- validateFirmwareBytes ---');
  test('validateFirmwareBytes: valid ESP32-S3 firmware', function() {
    var a = new Uint8Array(32);
    a[0] = 0xE9; a[12] = 9;
    var r = validateFirmwareBytes(a);
    ok(r.valid); is(r.errors.length, 0);
    is(r.chipId, 9); is(r.magic, 0xE9);
  });
  test('validateFirmwareBytes: valid ESP32-S2 firmware', function() {
    var a = new Uint8Array(32);
    a[0] = 0xE9; a[12] = 2;
    var r = validateFirmwareBytes(a);
    ok(r.valid); is(r.chipId, 2);
  });
  test('validateFirmwareBytes: valid ESP32-P4 firmware', function() {
    var a = new Uint8Array(32);
    a[0] = 0xE9; a[12] = 18;
    var r = validateFirmwareBytes(a);
    ok(r.valid); is(r.chipId, 18);
  });
  test('validateFirmwareBytes: ZIP file detected', function() {
    var a = new Uint8Array(32);
    a[0] = 0x50; a[1] = 0x4B;
    var r = validateFirmwareBytes(a);
    ok(!r.valid); is(r.errors[0], 'zip');
  });
  test('validateFirmwareBytes: ELF file detected', function() {
    var a = new Uint8Array(32);
    a[0] = 0x7F; a[1] = 0x45; a[2] = 0x4C; a[3] = 0x46;
    var r = validateFirmwareBytes(a);
    ok(!r.valid); is(r.errors[0], 'elf');
  });
  test('validateFirmwareBytes: invalid magic byte', function() {
    var a = new Uint8Array(32);
    a[0] = 0x01; a[12] = 9;
    var r = validateFirmwareBytes(a);
    ok(!r.valid);
    ok(r.errors.indexOf('bad_magic') >= 0);
  });
  test('validateFirmwareBytes: unknown chip ID', function() {
    var a = new Uint8Array(32);
    a[0] = 0xE9; a[12] = 99;
    var r = validateFirmwareBytes(a);
    ok(!r.valid);
    ok(r.errors.indexOf('unknown_chip_99') >= 0);
  });
  test('validateFirmwareBytes: both bad magic and unknown chip', function() {
    var a = new Uint8Array(32);
    a[0] = 0x01; a[12] = 99;
    var r = validateFirmwareBytes(a);
    ok(!r.valid);
    ok(r.errors.indexOf('bad_magic') >= 0);
    ok(r.errors.indexOf('unknown_chip_99') >= 0);
  });
  test('validateFirmwareBytes: null/undefined input', function() {
    var r = validateFirmwareBytes(null);
    ok(!r.valid); is(r.errors[0], 'too_small');
    r = validateFirmwareBytes(undefined);
    ok(!r.valid); is(r.errors[0], 'too_small');
  });
  test('validateFirmwareBytes: file too small (< 32 bytes)', function() {
    var a = new Uint8Array(10);
    a[0] = 0xE9;
    var r = validateFirmwareBytes(a);
    ok(!r.valid); is(r.errors[0], 'too_small');
  });
  test('validateFirmwareBytes: boundary at exactly 32 bytes', function() {
    var a = new Uint8Array(32);
    a[0] = 0xE9; a[12] = 9;
    var r = validateFirmwareBytes(a);
    ok(r.valid);
  });

  var total = passed + failed;
  console.log('\n  OTA: ' + passed + '/' + total + ' passed, ' + failed + ' failed');
}

try { run(); } catch (e) { console.log('  \u2717 Fatal: ' + e.message); failed++; }
process.exit(failed > 0 ? 1 : 0);
