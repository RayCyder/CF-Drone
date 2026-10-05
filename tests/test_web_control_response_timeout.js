const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '..', 'web_rc_html_controller.h'), 'utf8');
const start = source.indexOf('async function fetchWithTimeout(');
const end = source.indexOf('\nfunction recoverExpiredLease', start);
assert.ok(start >= 0 && end > start, 'fetchWithTimeout implementation exists');
const implementation = source.slice(start, end);

async function run() {
  let aborted = false;
  const hangingContext = {
    AbortController,
    setTimeout,
    clearTimeout,
    fetch: async (_input, options) => ({
      clone: () => ({
        arrayBuffer: () => new Promise((_resolve, reject) => {
          options.signal.addEventListener('abort', () => {
            aborted = true;
            reject(new Error('aborted while reading body'));
          }, {once: true});
        }),
      }),
    }),
  };
  vm.runInNewContext(`${implementation}; this.fetchWithTimeout = fetchWithTimeout;`, hangingContext);
  await assert.rejects(
    Promise.race([
      hangingContext.fetchWithTimeout('/status', {}, 20),
      new Promise((_resolve, reject) => setTimeout(() => reject(new Error('test guard expired')), 500)),
    ]),
    /aborted while reading body/,
  );
  assert.equal(aborted, true, 'deadline remains active while the response body is pending');

  const response = {
    clone: () => ({arrayBuffer: async () => new Uint8Array([1, 2, 3]).buffer}),
    json: async () => ({ok: true}),
  };
  const successContext = {
    AbortController,
    setTimeout,
    clearTimeout,
    fetch: async () => response,
  };
  vm.runInNewContext(`${implementation}; this.fetchWithTimeout = fetchWithTimeout;`, successContext);
  assert.strictEqual(await successContext.fetchWithTimeout('/status', {}, 100), response,
    'successful requests preserve the original Response object for callers');
}

run().then(() => console.log('web control full-response timeout checks passed'));
