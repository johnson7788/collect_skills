#!/usr/bin/env node
// Minimal Chrome DevTools Protocol helper for browser-assisted crawling.
// Requires Node 22+ (global WebSocket). No npm dependencies.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';

const PORT = process.env.CDP_PORT || '9222';
const BASE = `http://127.0.0.1:${PORT}`;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function getJson(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url} -> HTTP ${r.status}`);
  return r.json();
}

function connect(wsUrl, onEvent) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(wsUrl);
    let seq = 0;
    const pending = new Map();
    ws.addEventListener('open', () => resolve({
      send(method, params = {}) {
        return new Promise((res, rej) => {
          const id = ++seq;
          pending.set(id, { res, rej });
          ws.send(JSON.stringify({ id, method, params }));
        });
      },
      close() { ws.close(); },
    }));
    ws.addEventListener('message', (event) => {
      const msg = JSON.parse(event.data);
      if (msg.id && pending.has(msg.id)) {
        const { res, rej } = pending.get(msg.id);
        pending.delete(msg.id);
        msg.error ? rej(new Error(JSON.stringify(msg.error))) : res(msg.result);
      } else if (msg.method && onEvent) {
        onEvent(msg.method, msg.params);
      }
    });
    ws.addEventListener('error', (e) => reject(e));
  });
}

async function pageTarget() {
  const targets = await getJson(`${BASE}/json`);
  let page = targets.find((t) => t.type === 'page');
  if (!page) {
    const r = await fetch(`${BASE}/json/new?about:blank`, { method: 'PUT' });
    if (!r.ok) throw new Error(`new tab -> HTTP ${r.status}`);
    page = await r.json();
  }
  return page;
}

async function pageClient() {
  const page = await pageTarget();
  const c = await connect(page.webSocketDebuggerUrl);
  await c.send('Page.enable');
  await c.send('Runtime.enable');
  return c;
}

async function browserClient(onEvent) {
  const info = await getJson(`${BASE}/json/version`);
  return connect(info.webSocketDebuggerUrl, onEvent);
}

async function evaluate(c, expression) {
  const r = await c.send('Runtime.evaluate', {
    expression,
    returnByValue: true,
    awaitPromise: true,
  });
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.text || 'evaluate failed');
  return r.result?.value;
}

function snapshot(dirs) {
  const out = new Map();
  for (const dir of dirs) {
    try {
      for (const name of fs.readdirSync(dir)) {
        const p = path.join(dir, name);
        const st = fs.statSync(p);
        if (st.isFile()) out.set(p, { size: st.size, mtime: st.mtimeMs });
      }
    } catch { /* ignore */ }
  }
  return out;
}

function findNew(dirs, before) {
  const now = snapshot(dirs);
  for (const [p, st] of now) {
    const old = before.get(p);
    if (!old || old.size !== st.size || old.mtime !== st.mtime) {
      if (st.size > 0) return p;
    }
  }
  return null;
}

async function waitForFile(dirs, before, timeoutMs, browserState) {
  const started = Date.now();
  let last = null;
  let stable = 0;
  while (Date.now() - started < timeoutMs) {
    if (browserState?.canceled) throw new Error('download canceled');
    const p = findNew(dirs, before);
    if (p) {
      const st = fs.statSync(p);
      if (last === p && st.size === stable) return p;
      last = p;
      stable = st.size;
    }
    await sleep(400);
  }
  throw new Error(`download timeout (${timeoutMs}ms); checked: ${dirs.join(', ')}`);
}

function downloadDirs(dir) {
  return [dir, path.join(os.homedir(), 'Downloads')];
}

async function doDownload(url, dir) {
  fs.mkdirSync(dir, { recursive: true });
  const dirs = downloadDirs(dir);
  const state = { canceled: false, completed: false };
  const browser = await browserClient((method, params) => {
    if (method === 'Browser.downloadProgress') {
      if (params.state === 'completed') state.completed = true;
      if (params.state === 'canceled') state.canceled = true;
    }
  });
  await browser.send('Browser.setDownloadBehavior', {
    behavior: 'allow',
    downloadPath: dir,
    eventsEnabled: true,
  });
  const before = snapshot(dirs);
  const page = await pageClient();
  await page.send('Page.navigate', { url });
  const file = await waitForFile(dirs, before, 90000, state);
  page.close();
  browser.close();
  return file;
}

async function doWatch(dir, seconds) {
  fs.mkdirSync(dir, { recursive: true });
  const dirs = downloadDirs(dir);
  const browser = await browserClient();
  await browser.send('Browser.setDownloadBehavior', {
    behavior: 'allow',
    downloadPath: dir,
    eventsEnabled: true,
  });
  const before = snapshot(dirs);
  try {
    const file = await waitForFile(dirs, before, seconds * 1000, null);
    return file;
  } finally {
    browser.close();
  }
}

const [cmd, ...args] = process.argv.slice(2);

try {
  if (cmd === 'status' || cmd === 'tabs') {
    const info = await getJson(`${BASE}/json/version`);
    const tabs = (await getJson(`${BASE}/json`))
      .filter((t) => t.type === 'page')
      .map((t) => ({ title: t.title, url: t.url, id: t.id }));
    console.log(JSON.stringify({ browser: info.Browser, port: PORT, tabs }, null, 2));
  } else if (cmd === 'goto') {
    const page = await pageClient();
    await page.send('Page.navigate', { url: args[0] });
    await sleep(Number(process.env.WAIT_MS || 3000));
    const title = await evaluate(page, 'document.title');
    const url = await evaluate(page, 'location.href');
    console.log(JSON.stringify({ title, url }));
    page.close();
  } else if (cmd === 'shot') {
    const page = await pageClient();
    const r = await page.send('Page.captureScreenshot', { format: 'png' });
    fs.writeFileSync(args[0], Buffer.from(r.data, 'base64'));
    console.log(args[0]);
    page.close();
  } else if (cmd === 'text') {
    const page = await pageClient();
    const text = await evaluate(page, 'document.body ? document.body.innerText.slice(0, 20000) : ""');
    console.log(text);
    page.close();
  } else if (cmd === 'links') {
    const filter = args[0] || '';
    const page = await pageClient();
    const links = await evaluate(page, `(() => {
      const f = ${JSON.stringify(filter)};
      const re = f ? new RegExp(f, 'i') : null;
      return Array.from(document.querySelectorAll('a')).map(a => ({
        text: (a.innerText || a.textContent || '').trim().slice(0, 200),
        href: a.href
      })).filter(x => x.href && (!re || re.test(x.text + ' ' + x.href)));
    })()`);
    console.log(JSON.stringify(links, null, 2));
    page.close();
  } else if (cmd === 'click') {
    const page = await pageClient();
    const result = await evaluate(page, `(() => {
      const el = document.querySelector(${JSON.stringify(args[0])});
      if (!el) return { ok: false, reason: 'not found' };
      el.scrollIntoView({ block: 'center', inline: 'center' });
      el.click();
      return { ok: true, text: (el.innerText || '').trim().slice(0, 100) };
    })()`);
    await sleep(Number(process.env.WAIT_MS || 1200));
    console.log(JSON.stringify(result));
    page.close();
  } else if (cmd === 'type') {
    const page = await pageClient();
    const result = await evaluate(page, `(() => {
      const el = document.querySelector(${JSON.stringify(args[0])});
      if (!el) return { ok: false, reason: 'not found' };
      el.focus();
      const value = ${JSON.stringify(args.slice(1).join(' '))};
      const proto = el.tagName === 'TEXTAREA' ? HTMLTextAreaElement.prototype :
                    el.tagName === 'INPUT' ? HTMLInputElement.prototype : null;
      const desc = proto && Object.getOwnPropertyDescriptor(proto, 'value');
      if (desc && desc.set) desc.set.call(el, value); else el.value = value;
      el.dispatchEvent(new Event('input', { bubbles: true }));
      el.dispatchEvent(new Event('change', { bubbles: true }));
      return { ok: true, value: el.value };
    })()`);
    console.log(JSON.stringify(result));
    page.close();
  } else if (cmd === 'eval') {
    const page = await pageClient();
    const value = await evaluate(page, args.join(' '));
    console.log(JSON.stringify(value));
    page.close();
  } else if (cmd === 'wait') {
    await sleep(Number(args[0] || 2000));
    console.log('ok');
  } else if (cmd === 'download') {
    const file = await doDownload(args[0], args[1]);
    console.log(JSON.stringify({ file, size: fs.statSync(file).size }));
  } else if (cmd === 'watch') {
    const file = await doWatch(args[0], Number(args[1] || 120));
    console.log(JSON.stringify({ file, size: fs.statSync(file).size }));
  } else {
    console.error(`usage: cdp.mjs <status|tabs|goto|shot|text|links|click|type|eval|wait|download|watch> ...`);
    process.exit(2);
  }
} catch (error) {
  console.error(error.stack || String(error));
  process.exit(1);
}
