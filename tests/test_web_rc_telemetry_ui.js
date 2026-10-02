// UI acceptance: a schema creates the telemetry cards once; each sample updates
// their values in place while capture/export continues to retain CSV rows.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '..', 'web_rc_html_telemetry.h'), 'utf8');
const page = source.match(/const char telemetryHtml\[\] PROGMEM = R"rawliteral\(([\s\S]*?)\)rawliteral";/);
assert.ok(page, 'telemetry page exists');
const script = page[1].match(/<script>\s*([\s\S]*?)<\/script>/);
assert.ok(script, 'telemetry page script exists');

class Element {
  constructor(tag = 'div') {
    this.tagName = tag;
    this.children = [];
    this.disabled = false;
    this.className = '';
    this.textContent = '';
    this.onclick = null;
    this.href = '';
    this.download = '';
  }
  append(...items) {
    for (const item of items) {
      if (item.isFragment) this.children.push(...item.children);
      else this.children.push(item);
    }
  }
  replaceChildren(...items) {
    this.children = [];
    this.append(...items);
  }
  click() { if (this.onclick) this.onclick(); }
}

const ids = ['state', 'values', 'received', 'captured', 'capture', 'download',
  'snapshot', 'resume-log', 'log-status'];
const elements = Object.fromEntries(ids.map(id => [id, new Element()]));
const objectUrls = [];
const sources = [];
class EventSource {
  constructor() { this.handlers = {}; sources.push(this); }
  addEventListener(name, handler) { this.handlers[name] = handler; }
}
class TestBlob {
  constructor(parts) { this.text = parts.join(''); }
}

const context = {
  document: {
    getElementById: id => elements[id],
    createElement: tag => new Element(tag),
    createDocumentFragment: () => Object.assign(new Element('#fragment'), {isFragment: true}),
  },
  EventSource,
  Blob: TestBlob,
  URL: {createObjectURL: blob => { objectUrls.push(blob); return 'blob:test'; }, revokeObjectURL() {}},
  location: {protocol: 'http:', hostname: 'drone'},
  fetch: async () => ({ok: true, json: async () => ({state: 'ROLLING', generation: 1, rowCount: 0})}),
  setInterval() {},
  console,
};
vm.runInNewContext(script[1], context);

const stream = sources[0];
assert.ok(stream, 'telemetry stream is initialized');
stream.handlers.schema({data: 'attitude.x,attitude.y,battery_v'});
stream.handlers.sample({data: '1,2,3', lastEventId: '10'});
const initialCards = elements.values.children.slice();
assert.equal(initialCards.length, 3);
assert.equal(initialCards[0].children[1].textContent, '1');

elements.capture.click();
stream.handlers.sample({data: '4,5,6', lastEventId: '11'});
assert.equal(elements.values.children.length, initialCards.length);
for (let i = 0; i < initialCards.length; i++)
  assert.strictEqual(elements.values.children[i], initialCards[i], 'sample reuses the schema-created card');
assert.equal(elements.values.children[0].children[1].textContent, '4');
assert.equal(elements.received.textContent, 2);
assert.equal(elements.captured.textContent, 1);

elements.download.click();
assert.match(objectUrls[0].text, /sequence,attitude\.x,attitude\.y,battery_v/);
assert.match(objectUrls[0].text, /11,4,5,6/);

stream.handlers.schema({data: 'attitude.x,battery_v'});
stream.handlers.sample({data: '7,8', lastEventId: '12'});
assert.equal(elements.values.children.length, 2, 'a new schema rebuilds the card list');
assert.equal(elements.values.children[0].children[1].textContent, '7');
assert.equal(elements.values.children[1].children[1].textContent, '8');
stream.handlers.sample({data: '9', lastEventId: '13'});
assert.equal(elements.values.children[0].children[1].textContent, '9');
assert.equal(elements.values.children[1].children[1].textContent, '—', 'missing sample values remain visible as unavailable');
console.log('web telemetry UI regression checks passed');
