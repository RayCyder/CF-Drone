const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '..', 'web_rc_recovery_html.h'), 'utf8');
const page = source.match(/R"rawliteral\(([\s\S]*?)\)rawliteral"/);
assert.ok(page, 'recovery page exists');
const script = page[1].match(/<script>([\s\S]*?)<\/script>/);
assert.ok(script, 'recovery script exists');

class Element {
  constructor(id) {
    this.id = id;
    this.textContent = '';
    this.disabled = false;
    this.clientWidth = 200;
    this.pointerId = null;
    this.knob = {style: {}};
  }
  querySelector() { return this.knob; }
  getBoundingClientRect() { return {left: 0, top: 0, width: 200, height: 200}; }
  setPointerCapture(id) { this.pointerId = id; }
  hasPointerCapture(id) { return this.pointerId === id; }
}

const elements = Object.fromEntries(['state', 'full', 'joystick-left', 'joystick-right']
  .map(id => [id, new Element(id)]));
const intervals = [];
const requests = [];
let conflict = false;
let hangBody = false;
let hangStatus = false;
let invalidStatus = false;
let leaseRequests = 0;
let releaseInitialLease = null;

function response(status, data, signal, bodyHangs = false) {
  return {
    status,
    ok: status >= 200 && status < 300,
    clone: () => ({
      arrayBuffer: () => hangBody || bodyHangs
        ? new Promise((_resolve, reject) => signal.addEventListener('abort',
          () => reject(new Error('body aborted')), {once: true}))
        : Promise.resolve(new ArrayBuffer(0)),
    }),
    json: async () => data,
  };
}

const context = {
  AbortController,
  document: {
    hidden: false,
    getElementById: id => elements[id],
    addEventListener() {},
  },
  window: {addEventListener() {}},
  localStorage: {
    getItem: key => key === 'cfDroneStopToken' ? 'continuity-token' : '',
    setItem() {},
  },
  sessionStorage: {getItem: () => '', setItem() {}},
  location: {protocol: 'http:', hostname: 'drone', reload() {}},
  performance: {now: () => 123},
  setTimeout: (callback, ms) => setTimeout(callback, ms === 3000 ? 25 : ms),
  clearTimeout,
  setInterval: callback => { intervals.push(callback); return intervals.length; },
  fetch: async (url, options = {}) => {
    requests.push({url, options});
    if (url === '/hang') return response(200, {}, options.signal);
    if (String(url).startsWith('/web_rc/lease')) {
      leaseRequests++;
      if (!releaseInitialLease) {
        return new Promise(resolve => {
          releaseInitialLease = () => resolve(response(200,
            {lease: 'lease-a', stop: 'continuity-token'}, options.signal));
        });
      }
      return response(200, {lease: 'lease-a', stop: 'continuity-token'}, options.signal);
    }
    if (url === '/web_rc/status')
      return response(200, invalidStatus ? {} : {armed: true, hover_throttle_pct: 48}, options.signal, hangStatus);
    if (url === '/web_rc' && conflict)
      return response(409, {error: 'web_rc_lease_in_use'}, options.signal);
    return response(200, {ok: true}, options.signal);
  },
  console,
};

const flush = () => new Promise(resolve => setTimeout(resolve, 0));

async function run() {
  vm.runInNewContext(script[1], context);
  assert.equal(intervals.length, 2, 'recovery page installs control and status timers');

  intervals[0]();
  const left = elements['joystick-left'];
  left.onpointerdown({pointerId: 7, clientX: 100, clientY: 50, currentTarget: left});
  const joinedAcquire = context.acquire();
  assert.equal(leaseRequests, 1,
    'startup, heartbeat, pointer input, and direct callers share one pending lease request');
  releaseInitialLease();
  assert.equal(await joinedAcquire, true, 'all concurrent callers receive the acquired lease');
  await flush();
  await flush();
  const firstStickCount = requests.filter(item => item.url === '/web_rc' &&
    JSON.parse(item.options.body).t === 1).length;
  assert.equal(firstStickCount, 1, 'initial pointer input sends one stick packet');

  intervals[0]();
  await flush();
  const renewedStickCount = requests.filter(item => item.url === '/web_rc' &&
    JSON.parse(item.options.body).t === 1).length;
  assert.equal(renewedStickCount, 2, 'control timer renews an unchanged active stick packet');

  // WEB-RECOVER-3: slow status reads do not overlap, and timeout unlocks retry.
  hangStatus = true;
  elements.full.disabled = false;
  const beforeStatus = requests.filter(item => item.url === '/web_rc/status').length;
  const statusRead = context.refresh();
  context.refresh();
  intervals[1]();
  assert.equal(requests.filter(item => item.url === '/web_rc/status').length - beforeStatus, 1,
    'manual refresh and status ticks share one in-flight status request');
  await statusRead;
  assert.equal(elements.full.disabled, true, 'unknown status cannot offer a full-page reload');
  assert.match(elements.state.textContent, /连接中断/);
  hangStatus = false;
  await context.refresh();
  assert.match(elements.state.textContent, /轻量控制可用/, 'polling recovers after a timed-out body');
  invalidStatus = true;
  await context.refresh();
  assert.equal(elements.full.disabled, true, 'malformed status cannot imply disarmed');
  assert.match(elements.state.textContent, /连接中断/);
  invalidStatus = false;
  context.document.hidden = true;
  const beforeHidden = requests.length;
  await context.refresh();
  assert.equal(requests.length, beforeHidden, 'hidden recovery page pauses status polling');
  context.document.hidden = false;

  conflict = true;
  intervals[0]();
  await flush();
  await flush();
  const requestCountAfterConflict = requests.length;
  const leaseCountAfterConflict = leaseRequests;
  intervals[0]();
  intervals[0]();
  await flush();
  assert.equal(requests.length, requestCountAfterConflict,
    'a displaced recovery page stops all automatic control requests');
  assert.equal(leaseRequests, leaseCountAfterConflict,
    'a displaced recovery page does not reacquire a lease');
  intervals[1]();
  await flush();
  assert.equal(elements.state.textContent, '控制权已被其他页面接管',
    'status polling does not hide the displaced-page warning');

  hangBody = true;
  await assert.rejects(
    Promise.race([
      context.timeoutFetch('/hang', {}, 20),
      new Promise((_resolve, reject) => setTimeout(() => reject(new Error('test guard expired')), 500)),
    ]),
    /body aborted/,
  );
}

run().then(() => console.log('web recovery behavior checks passed'));
