// SSE-R4: leaving a page closes its stream; BFCache restore opens exactly one
// replacement. One inseparable stage covers both SSE consumers and firmware.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
for (const file of ['web_rc_html_telemetry.h', 'web_rc_html_wifi.h']) {
  const text = fs.readFileSync(path.join(__dirname, '..', file), 'utf8');
  const script = text.match(/<script>([\s\S]*?)<\/script>/)[1];
  const elements = new Map(), listeners = {}, streams = [];
  const element = () => ({children: [], style: {}, disabled: false, textContent: '',
    addEventListener() {}, append(...items) {this.children.push(...items);},
    replaceChildren(...items) {this.children = items;}, add() {}, click() {}});
  const document = {getElementById(id) {if (!elements.has(id)) elements.set(id, element()); return elements.get(id);},
    createElement: element, createDocumentFragment: element};
  class EventSource {
    constructor() {this.closed = false; this.handlers = {}; streams.push(this);}
    addEventListener(name, handler) {this.handlers[name] = handler;}
    close() {this.closed = true;}
  }
  const context = {document, EventSource, location: {protocol: 'http:', hostname: 'drone'},
    window: {addEventListener(name, handler) {listeners[name] = handler;}},
    fetch: async () => ({ok: true, json: async () => ({profiles: [], motors: [], state: 'empty'})}),
    setInterval() {}, setTimeout() {}, Option: function() {}, console};
  vm.runInNewContext(script, context);
  assert.equal(streams.length, 1, file + ': initial stream');
  assert.equal(typeof listeners.pagehide, 'function', file + ': unload cleanup');
  assert.equal(typeof listeners.pageshow, 'function', file + ': BFCache recovery');
  for (let i = 0; i < 30; i++) {
    const previous = streams.at(-1);
    listeners.pageshow({persisted: false});
    assert.equal(streams.at(-1), previous, 'normal pageshow must not duplicate stream');
    listeners.pagehide({persisted: true});
    assert.equal(previous.closed, true, 'old connection is explicitly closed');
    listeners.pagehide({persisted: true});
    listeners.pageshow({persisted: true});
    assert.notEqual(streams.at(-1), previous, 'restore creates a replacement');
    assert.equal(streams.filter(stream => !stream.closed).length, 1, 'one live stream after restore');
    const state = document.getElementById(file.includes('telemetry') ? 'state' : 'event-state');
    state.textContent = 'current';
    if (previous.onerror) previous.onerror();
    if (previous.onopen) previous.onopen();
    assert.equal(state.textContent, 'current', 'stale connection callbacks cannot overwrite current UI');
    const beforeReceived = document.getElementById('received').textContent;
    const beforeEvents = document.getElementById('event-list').textContent;
    if (previous.handlers.sample) previous.handlers.sample({data: '1,2,3', lastEventId: 'old'});
    if (previous.handlers['system-log']) previous.handlers['system-log']({data: '1|WIFI|old', lastEventId: 'old'});
    assert.equal(document.getElementById('received').textContent, beforeReceived, 'late samples from closed stream are ignored');
    assert.equal(document.getElementById('event-list').textContent, beforeEvents, 'late events from closed stream are ignored');
  }
}
console.log('SSE page refresh and BFCache lifecycle checks passed');
