#!/usr/bin/env node
// Run all web tests, capture output to timestamped log
// Usage: node run_all.js

var path = require('path');
var fs = require('fs');
var cp = require('child_process');

function pad2(n) { return n < 10 ? '0' + n : '' + n; }
var now = new Date();
var ts = now.getFullYear() + '-' + pad2(now.getMonth()+1) + '-' + pad2(now.getDate())
       + '_' + pad2(now.getHours()) + pad2(now.getMinutes());

var testsDir = __dirname;
var logsDir = path.join(testsDir, 'logs');
if (!fs.existsSync(logsDir)) fs.mkdirSync(logsDir);
var logFile = path.join(logsDir, 'run_' + ts + '.log');
var logStream = fs.createWriteStream(logFile, { flags: 'a', encoding: 'utf8' });
var allOutput = [];

function tee(data) {
  process.stdout.write(data);
  logStream.write(data);
}

var testFiles = fs.readdirSync(testsDir).filter(function (f) {
  return f.startsWith('test_') && f.endsWith('.js');
});

if (testFiles.length === 0) {
  tee('No test files found in ' + testsDir + '\n');
  logStream.end();
  process.exit(0);
}

tee('=== Web Test Run ' + ts + ' ===\n');
tee('Found ' + testFiles.length + ' test suite(s)\n\n');

var exitCode = 0;
var passed = 0, failed = 0;
testFiles.forEach(function (file) {
  var result = cp.spawnSync('node', [path.join(testsDir, file)], {
    stdio: 'pipe',
    shell: true,
    encoding: 'utf8',
    timeout: 120000
  });

  var out = (result.stdout || '') + (result.stderr || '');
  tee(out);

  // Count pass/fail from summary line
  var m = out.match(/(\d+)\/(\d+)\s+passed/);
  if (m) {
    passed += parseInt(m[1], 10);
    failed += parseInt(m[2], 10) - parseInt(m[1], 10);
  }

  if (result.status !== 0 && result.status !== null) {
    tee('\n  [EXIT CODE ' + result.status + ']\n');
    exitCode = result.status;
  }
  tee('\n');
});

tee('=== Summary: ' + passed + '/' + (passed+failed) + ' passed, ' + failed + ' failed ===\n');
tee('Log saved: ' + logFile + '\n');
logStream.end();
process.exit(exitCode);
