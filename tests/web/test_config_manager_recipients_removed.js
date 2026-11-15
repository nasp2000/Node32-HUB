// Ponto 4 do backlog de crawl: o editor de destinatários legado do
// /config-manager.js.
//
// `config_manager_js_asset.cpp` (servido como /config-manager.js) trazia um
// editor "recipients" que assumia que `GET /api/recipients` devolvia um ARRAY de
// `{name,email}`. A rota devolve `{total,offset,limit,items:[{name,op,emails,…}]}`
// (o shape dos slots), portanto `Array.isArray(list)` era sempre falso e a
// tabela — que já não existe em nenhuma página — nunca era preenchida. O bloco
// era 95 linhas de código morto com uma suposição de shape errada.
//
// Estes dois casos fixam a remoção: o asset deixou de trazer o editor legado e o
// dispatcher nunca pede a secção `recipients` nem chama /api/recipients.

const { JSDOM } = require('jsdom');
const fs = require('fs');
const path = require('path');

var passed = 0, failed = 0;
function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }

const ASSET = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'assets', 'config_manager_js_asset.cpp');

function assetBody() {
  const src = fs.readFileSync(ASSET, 'utf8');
  const m = src.match(/R"CFG\(([\s\S]*?)\)CFG"/);
  if (!m) throw new Error('CONFIG_MANAGER_JS_BODY not found in asset');
  return m[1];
}

test('the legacy recipients editor is gone from /config-manager.js', function () {
  const js = assetBody();
  ['loadRecipientsConfig', 'addRecipientRow', 'saveRecipient', 'removeRecipient',
   'collectRecipientsFromTable', 'recipients-table'].forEach(function (sym) {
    ok(js.indexOf(sym) < 0, 'asset still contains ' + sym);
  });
  // The wrong-shape assumption must be gone too, not just renamed.
  ok(js.indexOf('rec.email') < 0, 'asset still reads rec.email from the slot list shape');
});

test('loadConfig() never requests the recipients section or /api/recipients', async function () {
  const js = assetBody();
  const calls = [];
  const dom = new JSDOM('<!doctype html><body><input id="smtp_server"></body>', {
    url: 'http://localhost',
    runScripts: 'dangerously',
    pretendToBeVisual: true,
    beforeParse(win) {
      win.fetch = function (url) {
        calls.push(String(url));
        return Promise.resolve({ ok: true, json: function () { return Promise.resolve({ email: {}, smtp: {} }); } });
      };
    }
  });
  const script = dom.window.document.createElement('script');
  script.textContent = js;
  dom.window.document.body.appendChild(script);

  is(typeof dom.window.loadConfig, 'function', 'asset exposes loadConfig');
  is(typeof dom.window.loadRecipientsConfig, 'undefined', 'loadRecipientsConfig removed by the asset');

  dom.window.loadConfig();
  await new Promise(function (r) { setTimeout(r, 30); });

  ok(calls.indexOf('/api/recipients') < 0 && calls.indexOf('/api/recipients?limit=100') < 0,
     'no /api/recipients request: ' + JSON.stringify(calls));
  ok(calls.some(function (u) { return u.indexOf('section=email') >= 0; }),
     'detects the email page from #smtp_server: ' + JSON.stringify(calls));
});

setTimeout(function () {
  console.log('\nResults: ' + passed + '/' + (passed + failed) + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}, 50);
