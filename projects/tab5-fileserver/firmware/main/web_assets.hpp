#pragma once

namespace tab5_fs_web {

inline constexpr const char kIndexHtml[] = R"RAWHTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>TAB5 File Explorer — SD Card Root</title>
<style>
@font-face {
  font-family: 'Tab5Pixel13';
  src: url('/fonts/pixelfont13.ttf') format('truetype');
  font-weight: 600;
  font-style: normal;
}
@font-face {
  font-family: 'Tab5Pixel11';
  src: url('/fonts/pixelfont11.ttf') format('truetype');
  font-weight: 400;
  font-style: normal;
}
@font-face {
  font-family: 'Tab5Pixel7';
  src: url('/fonts/pixelfont7.ttf') format('truetype');
  font-weight: 400;
  font-style: normal;
}

:root {
  --bg: #101215;
  --surface: #1d2228;
  --surface-pressed: #2a3139;
  --border: #353d46;
  --text: #f7f8fa;
  --muted: #9ba6b2;
  --disabled: #59636f;
  --blue: #3b82f6;
  --orange: #f59e0b;
  --red: #ef4444;
  --green: #22c55e;
  --yellow: #eab308;
}

body.light-mode {
  --bg: #eef1f4;
  --surface: #ffffff;
  --surface-pressed: #dde2e8;
  --border: #c8cfd7;
  --text: #15181c;
  --muted: #59636f;
  --disabled: #a4acb5;
}

* {
  box-sizing: border-box;
  margin: 0;
  padding: 0;
  border-radius: 0 !important;
  -webkit-font-smoothing: none;
  -moz-osx-font-smoothing: grayscale;
  font-smooth: never;
  image-rendering: pixelated;
}

body {
  background-color: var(--bg);
  color: var(--text);
  font-family: 'Tab5Pixel11', 'Courier New', monospace;
  font-size: 22px;
  line-height: 1.3;
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  user-select: none;
}

/* Top Console Tab Strip */
.tab-strip {
  display: grid;
  grid-template-columns: repeat(6, 1fr);
  background: var(--bg);
  border-bottom: 2px solid var(--border);
  height: 46px;
}
.tab-item {
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--surface);
  border-right: 2px solid var(--border);
  border-bottom: 2px solid var(--border);
  color: var(--disabled);
  font-family: 'Tab5Pixel11', monospace;
  font-size: 20px;
  letter-spacing: 1px;
  text-transform: uppercase;
}
.tab-item.tab-terminal {
  border: 2px solid var(--red);
  color: var(--red);
}
.tab-item.tab-ollama {
  border: 2px solid var(--orange);
  color: var(--orange);
}
.tab-item.tab-active {
  background: var(--surface-pressed);
  border: 2px solid var(--blue);
  color: var(--text);
  font-family: 'Tab5Pixel13', monospace;
}
.tab-item.tab-web {
  border: 2px solid var(--green);
  color: var(--green);
}

/* Console Header */
.console-header {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  justify-content: space-between;
  padding: 14px 24px;
  border-bottom: 2px solid var(--border);
  gap: 16px;
}
.brand-block {
  display: flex;
  align-items: baseline;
  gap: 16px;
  flex-wrap: wrap;
}
.brand-title {
  font-family: 'Tab5Pixel13', monospace;
  font-size: 32px;
  color: var(--text);
  letter-spacing: 1px;
}
.brand-version {
  font-family: 'Tab5Pixel11', monospace;
  font-size: 20px;
  color: var(--muted);
}
.header-controls {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}

/* Console Buttons */
.btn {
  background: var(--surface);
  color: var(--text);
  border: 2px solid var(--border);
  padding: 8px 16px;
  font-family: 'Tab5Pixel13', monospace;
  font-size: 20px;
  cursor: pointer;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  text-decoration: none;
  text-transform: uppercase;
  transition: none;
}
.btn:hover, .btn:active {
  background: var(--surface-pressed);
}
.btn-blue {
  border-color: var(--blue);
  color: var(--blue);
}
.btn-blue:hover {
  background: var(--blue);
  color: #ffffff;
}
.btn-green {
  border-color: var(--green);
  color: var(--green);
}
.btn-green:hover {
  background: var(--green);
  color: #101215;
}
.btn-orange {
  border-color: var(--orange);
  color: var(--orange);
}
.btn-red {
  border-color: var(--red);
  color: var(--red);
}

/* Storage Badge */
.storage-card {
  background: var(--surface);
  border: 2px solid var(--border);
  padding: 6px 14px;
  display: flex;
  flex-direction: column;
  min-width: 240px;
}
.storage-top {
  display: flex;
  justify-content: space-between;
  font-size: 16px;
  color: var(--muted);
  margin-bottom: 4px;
}
.meter-track {
  height: 8px;
  background: var(--surface-pressed);
  border: 1px solid var(--border);
  width: 100%;
}
.meter-fill {
  height: 100%;
  background: var(--blue);
  width: 0%;
}

/* Main Content Area */
.main-wrap {
  flex: 1;
  padding: 20px 24px;
  display: flex;
  flex-direction: column;
  gap: 16px;
  max-width: 1440px;
  width: 100%;
  margin: 0 auto;
}

/* Finder Toolbar */
.finder-toolbar {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  background: var(--surface);
  border: 2px solid var(--border);
  padding: 12px 16px;
}
.nav-group {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
  flex: 1;
}
.breadcrumb-bar {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 4px;
  background: var(--bg);
  border: 2px solid var(--border);
  padding: 6px 12px;
  flex: 1;
  min-width: 220px;
}
.crumb {
  color: var(--blue);
  cursor: pointer;
  font-family: 'Tab5Pixel13', monospace;
  font-size: 20px;
  padding: 2px 4px;
}
.crumb:hover {
  background: var(--surface-pressed);
}
.crumb-sep {
  color: var(--muted);
  padding: 0 2px;
}
.action-group {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}
.search-input {
  background: var(--bg);
  color: var(--text);
  border: 2px solid var(--border);
  padding: 8px 12px;
  font-family: 'Tab5Pixel11', monospace;
  font-size: 20px;
  width: 210px;
  outline: none;
}
.search-input:focus {
  border-color: var(--blue);
}

/* Upload Progress Bar */
.upload-panel {
  display: none;
  background: var(--surface);
  border: 2px solid var(--blue);
  padding: 12px 16px;
  flex-direction: column;
  gap: 8px;
}
.upload-panel.active {
  display: flex;
}
.upload-header {
  display: flex;
  justify-content: space-between;
  font-family: 'Tab5Pixel13', monospace;
  font-size: 20px;
  color: var(--text);
}
.upload-track {
  height: 16px;
  background: var(--bg);
  border: 2px solid var(--border);
  width: 100%;
}
.upload-fill {
  height: 100%;
  background: var(--green);
  width: 0%;
}

/* Dropzone Overlay */
.finder-card {
  flex: 1;
  background: var(--surface);
  border: 2px solid var(--border);
  display: flex;
  flex-direction: column;
  min-height: 420px;
  position: relative;
}
.finder-card.drag-over {
  border-color: var(--green);
  background: var(--surface-pressed);
}

/* File Table */
.file-table-head {
  display: grid;
  grid-template-columns: 90px 1fr 130px 200px 180px;
  background: var(--bg);
  border-bottom: 2px solid var(--border);
  padding: 10px 16px;
  font-family: 'Tab5Pixel13', monospace;
  font-size: 18px;
  color: var(--muted);
  text-transform: uppercase;
}
.file-list {
  flex: 1;
  overflow-y: auto;
}
.file-row {
  display: grid;
  grid-template-columns: 90px 1fr 130px 200px 180px;
  align-items: center;
  padding: 10px 16px;
  border-bottom: 1px solid var(--border);
  cursor: pointer;
  user-select: text;
}
.file-row:hover {
  background: var(--surface-pressed);
}
.type-badge {
  display: inline-block;
  padding: 2px 8px;
  font-family: 'Tab5Pixel13', monospace;
  font-size: 16px;
  border: 2px solid var(--border);
  text-align: center;
  width: 72px;
}
.type-dir {
  border-color: var(--blue);
  color: var(--blue);
}
.type-html {
  border-color: var(--green);
  color: var(--green);
}
.type-file {
  border-color: var(--muted);
  color: var(--muted);
}
.file-name {
  font-family: 'Tab5Pixel7', 'Tab5Pixel13', monospace;
  font-size: 28px;
  color: var(--text);
  word-break: break-all;
  padding-right: 12px;
}
.file-size, .file-date {
  font-family: 'Tab5Pixel11', monospace;
  font-size: 20px;
  color: var(--muted);
}
.file-actions {
  display: flex;
  gap: 6px;
  justify-content: flex-end;
}
.btn-sm {
  padding: 4px 10px;
  font-size: 16px;
}
.empty-state {
  padding: 48px 24px;
  text-align: center;
  color: var(--muted);
  font-size: 22px;
}

/* Passcode Lock Modal */
.modal-backdrop {
  position: fixed;
  inset: 0;
  background: rgba(16, 18, 21, 0.92);
  display: none;
  align-items: center;
  justify-content: center;
  z-index: 1000;
  padding: 16px;
}
.modal-backdrop.active {
  display: flex;
}
.lock-card {
  background: var(--surface);
  border: 2px solid var(--blue);
  width: 100%;
  max-width: 480px;
  padding: 24px;
  display: flex;
  flex-direction: column;
  gap: 16px;
}
.lock-title {
  font-family: 'Tab5Pixel13', monospace;
  font-size: 28px;
  color: var(--text);
  text-transform: uppercase;
}
.lock-sub {
  font-size: 20px;
  color: var(--muted);
}
.pin-display {
  background: var(--bg);
  border: 2px solid var(--border);
  padding: 14px;
  text-align: center;
  font-family: 'Tab5Pixel13', monospace;
  font-size: 36px;
  letter-spacing: 6px;
  color: var(--blue);
  min-height: 68px;
  display: flex;
  align-items: center;
  justify-content: center;
}
.lock-error {
  color: var(--red);
  font-family: 'Tab5Pixel13', monospace;
  font-size: 20px;
  min-height: 26px;
  text-align: center;
}
.keypad-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 10px;
}
.key-btn {
  background: var(--surface-pressed);
  border: 2px solid var(--border);
  color: var(--text);
  font-family: 'Tab5Pixel13', monospace;
  font-size: 26px;
  padding: 14px 0;
  cursor: pointer;
  text-align: center;
}
.key-btn:hover, .key-btn:active {
  border-color: var(--blue);
  background: var(--bg);
}

/* Footer */
.console-footer {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  justify-content: space-between;
  padding: 12px 24px;
  background: var(--surface);
  border-top: 2px solid var(--border);
  font-size: 20px;
  color: var(--muted);
  gap: 12px;
}
.status-msg {
  color: var(--text);
  font-family: 'Tab5Pixel13', monospace;
}

@media (max-width: 820px) {
  .tab-strip {
    grid-template-columns: repeat(3, 1fr);
    height: auto;
  }
  .file-table-head {
    grid-template-columns: 76px 1fr 90px 130px;
  }
  .file-row {
    grid-template-columns: 76px 1fr 90px 130px;
  }
  .file-date {
    display: none;
  }
}
</style>
</head>
<body>

<!-- Console Top Tabs -->
<div class="tab-strip">
  <div class="tab-item">Console</div>
  <div class="tab-item tab-terminal">Terminal</div>
  <div class="tab-item tab-ollama">Ollama</div>
  <div class="tab-item tab-active">Files</div>
  <div class="tab-item tab-web">Web Server</div>
  <div class="tab-item">Network</div>
</div>

<!-- Header -->
<header class="console-header">
  <div class="brand-block">
    <div class="brand-title">TAB5 File Explorer</div>
    <div class="brand-version" id="netModeBadge">0.1.0 — SD CARD ROOT FINDER</div>
  </div>
  <div class="header-controls">
    <div class="storage-card">
      <div class="storage-top">
        <span>SD CARD ROOT</span>
        <span id="sdSummaryText">-- / -- GB</span>
      </div>
      <div class="meter-track"><div class="meter-fill" id="sdMeterFill"></div></div>
    </div>
    <button class="btn" id="themeBtn" onclick="toggleTheme()">LIGHT MODE</button>
    <button class="btn btn-red" onclick="lockSession()">LOCK</button>
  </div>
</header>

<!-- Main Finder -->
<main class="main-wrap">
  <div class="finder-toolbar">
    <div class="nav-group">
      <button class="btn" onclick="navigateUp()">UP</button>
      <button class="btn" onclick="loadDirectory('/')">ROOT /</button>
      <button class="btn" onclick="loadDirectory(currentPath)">REFRESH</button>
      <div class="breadcrumb-bar" id="breadcrumbBar"></div>
    </div>
    <div class="action-group">
      <input type="text" class="search-input" id="filterInput" placeholder="FILTER FILES..." oninput="renderEntries()">
      <button class="btn btn-orange" onclick="promptNewFolder()">+ FOLDER</button>
      <button class="btn btn-blue" onclick="document.getElementById('filePicker').click()">UPLOAD TO SD</button>
      <input type="file" id="filePicker" multiple style="display:none" onchange="handleSelectedFiles(this.files)">
    </div>
  </div>

  <!-- Upload Progress -->
  <div class="upload-panel" id="uploadPanel">
    <div class="upload-header">
      <span id="uploadLabel">UPLOADING TO SD CARD...</span>
      <span id="uploadPct">0%</span>
    </div>
    <div class="upload-track"><div class="upload-fill" id="uploadFill"></div></div>
  </div>

  <!-- Finder Table -->
  <div class="finder-card" id="dropZone">
    <div class="file-table-head">
      <div>TYPE</div>
      <div>NAME</div>
      <div>SIZE</div>
      <div class="file-date">MODIFIED</div>
      <div style="text-align:right">ACTION</div>
    </div>
    <div class="file-list" id="fileList">
      <div class="empty-state">AUTHENTICATING WITH TAB5...</div>
    </div>
  </div>
</main>

<!-- Footer -->
<footer class="console-footer">
  <div class="status-msg" id="footerStatus">TAB5 FILE SERVER READY</div>
  <div id="footerCounts">0 FOLDERS, 0 FILES</div>
</footer>

<!-- 8-Digit Passcode Lock Modal -->
<div class="modal-backdrop" id="lockModal">
  <div class="lock-card">
    <div class="lock-title">TAB5 8-DIGIT PASSCODE</div>
    <div class="lock-sub">ENTER THE 8-DIGIT CODE DISPLAYED ON THE TAB5 SCREEN TO ACCESS THE SD CARD FILE EXPLORER.</div>
    <div class="pin-display" id="pinDisplay">____ ____</div>
    <div class="lock-error" id="lockError"></div>
    <div class="keypad-grid">
      <button class="key-btn" onclick="pinKey('1')">1</button>
      <button class="key-btn" onclick="pinKey('2')">2</button>
      <button class="key-btn" onclick="pinKey('3')">3</button>
      <button class="key-btn" onclick="pinKey('4')">4</button>
      <button class="key-btn" onclick="pinKey('5')">5</button>
      <button class="key-btn" onclick="pinKey('6')">6</button>
      <button class="key-btn" onclick="pinKey('7')">7</button>
      <button class="key-btn" onclick="pinKey('8')">8</button>
      <button class="key-btn" onclick="pinKey('9')">9</button>
      <button class="key-btn" style="color:var(--red)" onclick="pinClear()">CLR</button>
      <button class="key-btn" onclick="pinKey('0')">0</button>
      <button class="key-btn" style="color:var(--orange)" onclick="pinBackspace()">DEL</button>
    </div>
    <button class="btn btn-blue" style="width:100%;padding:14px;font-size:24px" onclick="submitPasscode()">UNLOCK FILE EXPLORER</button>
  </div>
</div>

<!-- New Folder Modal -->
<div class="modal-backdrop" id="mkdirModal">
  <div class="lock-card">
    <div class="lock-title">CREATE NEW FOLDER</div>
    <div class="lock-sub" id="mkdirPathSub">IN /</div>
    <input type="text" class="search-input" id="mkdirInput" style="width:100%;font-size:24px;padding:12px" placeholder="FOLDER NAME">
    <div style="display:flex;gap:12px">
      <button class="btn" style="flex:1" onclick="closeMkdirModal()">CANCEL</button>
      <button class="btn btn-green" style="flex:1" onclick="confirmMkdir()">CREATE</button>
    </div>
  </div>
</div>

<script>
let currentPath = '/';
let allEntries = [];
let pinInput = '';
let sessionPasscode = sessionStorage.getItem('tab5_passcode') || '';
let isLightMode = localStorage.getItem('tab5_light') === '1';

function applyTheme() {
  if (isLightMode) {
    document.body.classList.add('light-mode');
    document.getElementById('themeBtn').textContent = 'DARK MODE';
  } else {
    document.body.classList.remove('light-mode');
    document.getElementById('themeBtn').textContent = 'LIGHT MODE';
  }
}

function toggleTheme() {
  isLightMode = !isLightMode;
  localStorage.setItem('tab5_light', isLightMode ? '1' : '0');
  applyTheme();
}

function setStatus(msg) {
  document.getElementById('footerStatus').textContent = msg;
}

function formatBytes(bytes) {
  if (!bytes || bytes <= 0) return '0 B';
  if (bytes < 1024) return bytes + ' B';
  if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB';
  if (bytes < 1073741824) return (bytes / 1048576).toFixed(1) + ' MB';
  return (bytes / 1073741824).toFixed(1) + ' GB';
}

function formatTime(ts) {
  if (!ts || ts <= 0) return '--';
  const d = new Date(ts * 1000);
  if (isNaN(d.getTime())) return '--';
  const y = d.getFullYear();
  const m = String(d.getMonth() + 1).padStart(2, '0');
  const day = String(d.getDate()).padStart(2, '0');
  const hh = String(d.getHours()).padStart(2, '0');
  const mm = String(d.getMinutes()).padStart(2, '0');
  return `${y}-${m}-${day} ${hh}:${mm}`;
}

function updatePinView() {
  let left = pinInput.slice(0, 4).padEnd(4, '_');
  let right = pinInput.slice(4, 8).padEnd(4, '_');
  document.getElementById('pinDisplay').textContent = left + ' ' + right;
}

function pinKey(d) {
  if (pinInput.length < 8) {
    pinInput += d;
    document.getElementById('lockError').textContent = '';
    updatePinView();
    if (pinInput.length === 8) {
      submitPasscode();
    }
  }
}

function pinBackspace() {
  if (pinInput.length > 0) {
    pinInput = pinInput.slice(0, -1);
    updatePinView();
  }
}

function pinClear() {
  pinInput = '';
  document.getElementById('lockError').textContent = '';
  updatePinView();
}

function showLockModal(errText) {
  pinInput = '';
  updatePinView();
  document.getElementById('lockError').textContent = errText || '';
  document.getElementById('lockModal').classList.add('active');
}

function hideLockModal() {
  document.getElementById('lockModal').classList.remove('active');
}

async function lockSession() {
  sessionPasscode = '';
  sessionStorage.removeItem('tab5_passcode');
  try { await fetch('/api/logout', { method: 'POST' }); } catch (e) {}
  showLockModal('SESSION LOCKED — ENTER 8-DIGIT PASSCODE');
}

function authHeaders() {
  const h = {};
  if (sessionPasscode) h['X-Tab5-Passcode'] = sessionPasscode;
  return h;
}

async function submitPasscode() {
  if (pinInput.length !== 8) {
    document.getElementById('lockError').textContent = 'ENTER ALL 8 DIGITS';
    return;
  }
  try {
    const resp = await fetch('/api/auth?code=' + encodeURIComponent(pinInput), {
      method: 'POST',
      headers: { 'X-Tab5-Passcode': pinInput }
    });
    const data = await resp.json();
    if (resp.ok && data.ok) {
      sessionPasscode = pinInput;
      sessionStorage.setItem('tab5_passcode', sessionPasscode);
      hideLockModal();
      await refreshServerStatus();
      await loadDirectory(currentPath);
    } else {
      document.getElementById('lockError').textContent = (data && data.error) ? data.error : 'INVALID 8-DIGIT PASSCODE';
      pinInput = '';
      updatePinView();
    }
  } catch (err) {
    document.getElementById('lockError').textContent = 'NETWORK ERROR CONNECTING TO TAB5';
  }
}

async function refreshServerStatus() {
  try {
    const resp = await fetch('/api/status', { headers: authHeaders() });
    const data = await resp.json();
    if (data.sd_total > 0) {
      const used = data.sd_total - data.sd_free;
      const pct = Math.min(100, Math.round((used * 100) / data.sd_total));
      document.getElementById('sdSummaryText').textContent =
        formatBytes(data.sd_free) + ' FREE / ' + formatBytes(data.sd_total);
      document.getElementById('sdMeterFill').style.width = pct + '%';
    }
    if (data.mode) {
      document.getElementById('netModeBadge').textContent =
        '0.1.0 — ' + data.mode + ' (' + (data.ip || '') + ')';
    }
    if (!data.authenticated) {
      showLockModal();
      return false;
    }
    return true;
  } catch (e) {
    return false;
  }
}

function renderBreadcrumbs() {
  const bar = document.getElementById('breadcrumbBar');
  bar.innerHTML = '';
  const rootSpan = document.createElement('span');
  rootSpan.className = 'crumb';
  rootSpan.textContent = 'SD ROOT /';
  rootSpan.onclick = () => loadDirectory('/');
  bar.appendChild(rootSpan);

  const parts = currentPath.split('/').filter(Boolean);
  let acc = '';
  for (const part of parts) {
    acc += '/' + part;
    const target = acc;
    const sep = document.createElement('span');
    sep.className = 'crumb-sep';
    sep.textContent = '>';
    bar.appendChild(sep);

    const c = document.createElement('span');
    c.className = 'crumb';
    c.textContent = part;
    c.onclick = () => loadDirectory(target);
    bar.appendChild(c);
  }
}

function buildFileUrl(relPath, forceDownload) {
  const encoded = relPath.split('/').map(encodeURIComponent).join('/');
  let url = '/fs' + (encoded.startsWith('/') ? encoded : '/' + encoded);
  const params = [];
  if (sessionPasscode) params.push('code=' + encodeURIComponent(sessionPasscode));
  if (forceDownload) params.push('dl=1');
  if (params.length) url += '?' + params.join('&');
  return url;
}

function renderEntries() {
  const list = document.getElementById('fileList');
  const filter = (document.getElementById('filterInput').value || '').trim().toLowerCase();
  list.innerHTML = '';

  const filtered = allEntries.filter(e => !filter || e.name.toLowerCase().includes(filter));
  let dirs = 0, files = 0;
  for (const e of filtered) {
    if (e.is_dir) dirs++; else files++;
  }
  document.getElementById('footerCounts').textContent = `${dirs} FOLDERS, ${files} FILES`;

  if (currentPath !== '/') {
    const upRow = document.createElement('div');
    upRow.className = 'file-row';
    upRow.onclick = () => navigateUp();
    upRow.innerHTML = `
      <div><span class="type-badge type-dir">DIR</span></div>
      <div class="file-name">.. (PARENT DIRECTORY)</div>
      <div class="file-size">--</div>
      <div class="file-date">--</div>
      <div class="file-actions"><span class="btn btn-sm">UP</span></div>
    `;
    list.appendChild(upRow);
  }

  if (filtered.length === 0) {
    const empty = document.createElement('div');
    empty.className = 'empty-state';
    empty.textContent = filter ? 'NO MATCHING ITEMS IN THIS DIRECTORY' : 'DIRECTORY IS EMPTY — DROP FILES HERE TO UPLOAD';
    list.appendChild(empty);
    return;
  }

  for (const item of filtered) {
    const row = document.createElement('div');
    row.className = 'file-row';
    const childPath = currentPath === '/' ? ('/' + item.name) : (currentPath + '/' + item.name);

    let badgeClass = 'type-file';
    let badgeText = 'FILE';
    if (item.is_dir) {
      badgeClass = 'type-dir';
      badgeText = 'DIR';
    } else if (item.is_html) {
      badgeClass = 'type-html';
      badgeText = 'HTML';
    }

    const typeCol = document.createElement('div');
    const badge = document.createElement('span');
    badge.className = 'type-badge ' + badgeClass;
    badge.textContent = badgeText;
    typeCol.appendChild(badge);

    const nameCol = document.createElement('div');
    nameCol.className = 'file-name';
    nameCol.textContent = item.name;

    const sizeCol = document.createElement('div');
    sizeCol.className = 'file-size';
    sizeCol.textContent = item.is_dir ? 'FOLDER' : formatBytes(item.size);

    const dateCol = document.createElement('div');
    dateCol.className = 'file-date';
    dateCol.textContent = formatTime(item.mtime);

    const actCol = document.createElement('div');
    actCol.className = 'file-actions';

    if (item.is_dir) {
      const openBtn = document.createElement('button');
      openBtn.className = 'btn btn-sm btn-blue';
      openBtn.textContent = 'OPEN';
      openBtn.onclick = (ev) => { ev.stopPropagation(); loadDirectory(childPath); };
      actCol.appendChild(openBtn);
      row.onclick = () => loadDirectory(childPath);
    } else if (item.is_html) {
      const streamLink = document.createElement('a');
      streamLink.className = 'btn btn-sm btn-green';
      streamLink.textContent = 'STREAM HTML';
      streamLink.href = buildFileUrl(childPath, false);
      streamLink.target = '_blank';
      streamLink.onclick = (ev) => {
        ev.stopPropagation();
        setStatus('STREAMING HTML: ' + item.name);
      };
      const dlLink = document.createElement('a');
      dlLink.className = 'btn btn-sm';
      dlLink.textContent = 'DL';
      dlLink.href = buildFileUrl(childPath, true);
      dlLink.setAttribute('download', item.name);
      dlLink.onclick = (ev) => {
        ev.stopPropagation();
        setStatus('DOWNLOADING: ' + item.name);
      };
      actCol.appendChild(streamLink);
      actCol.appendChild(dlLink);
      row.onclick = () => {
        setStatus('STREAMING HTML: ' + item.name);
        window.open(buildFileUrl(childPath, false), '_blank');
      };
    } else {
      const dlLink = document.createElement('a');
      dlLink.className = 'btn btn-sm btn-blue';
      dlLink.textContent = 'DOWNLOAD';
      dlLink.href = buildFileUrl(childPath, false);
      dlLink.setAttribute('download', item.name);
      dlLink.onclick = (ev) => {
        ev.stopPropagation();
        setStatus('DOWNLOADING: ' + item.name);
      };
      actCol.appendChild(dlLink);
      row.onclick = () => {
        setStatus('DOWNLOADING: ' + item.name);
        window.location.href = buildFileUrl(childPath, false);
      };
    }

    row.appendChild(typeCol);
    row.appendChild(nameCol);
    row.appendChild(sizeCol);
    row.appendChild(dateCol);
    row.appendChild(actCol);
    list.appendChild(row);
  }
}

async function loadDirectory(path) {
  setStatus('LOADING ' + path + ' ...');
  try {
    const resp = await fetch('/api/list?path=' + encodeURIComponent(path), {
      headers: authHeaders()
    });
    if (resp.status === 401) {
      showLockModal('PASSCODE REQUIRED');
      return;
    }
    const data = await resp.json();
    if (!resp.ok || !data.ok) {
      setStatus((data && data.error) ? data.error : 'FAILED TO READ DIRECTORY');
      return;
    }
    currentPath = data.path || '/';
    allEntries = data.entries || [];
    renderBreadcrumbs();
    renderEntries();
    setStatus('BROWSING SD CARD: ' + currentPath);
    refreshServerStatus();
  } catch (err) {
    setStatus('NETWORK ERROR LOADING DIRECTORY');
  }
}

function navigateUp() {
  if (currentPath === '/' || !currentPath) return;
  const idx = currentPath.lastIndexOf('/');
  const parent = idx <= 0 ? '/' : currentPath.slice(0, idx);
  loadDirectory(parent);
}

function promptNewFolder() {
  document.getElementById('mkdirPathSub').textContent = 'IN ' + currentPath;
  const inp = document.getElementById('mkdirInput');
  inp.value = '';
  document.getElementById('mkdirModal').classList.add('active');
  setTimeout(() => inp.focus(), 50);
}

function closeMkdirModal() {
  document.getElementById('mkdirModal').classList.remove('active');
}

async function confirmMkdir() {
  const name = (document.getElementById('mkdirInput').value || '').trim();
  if (!name) return;
  closeMkdirModal();
  setStatus('CREATING FOLDER ' + name + '...');
  try {
    const url = `/api/mkdir?path=${encodeURIComponent(currentPath)}&name=${encodeURIComponent(name)}`;
    const resp = await fetch(url, { method: 'POST', headers: authHeaders() });
    const data = await resp.json();
    if (resp.ok && data.ok) {
      setStatus('CREATED FOLDER: ' + name);
      await loadDirectory(currentPath);
    } else {
      setStatus((data && data.error) ? data.error : 'COULD NOT CREATE FOLDER');
    }
  } catch (e) {
    setStatus('ERROR CREATING FOLDER');
  }
}

function uploadSingleFile(file, index, total) {
  return new Promise((resolve, reject) => {
    const panel = document.getElementById('uploadPanel');
    const label = document.getElementById('uploadLabel');
    const pctEl = document.getElementById('uploadPct');
    const fill = document.getElementById('uploadFill');

    panel.classList.add('active');
    label.textContent = `UPLOADING (${index}/${total}): ${file.name}`;
    pctEl.textContent = '0%';
    fill.style.width = '0%';

    const xhr = new XMLHttpRequest();
    const url = `/api/upload?path=${encodeURIComponent(currentPath)}&name=${encodeURIComponent(file.name)}`;
    xhr.open('POST', url, true);
    if (sessionPasscode) {
      xhr.setRequestHeader('X-Tab5-Passcode', sessionPasscode);
    }
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');

    xhr.upload.onprogress = (ev) => {
      if (ev.lengthComputable && ev.total > 0) {
        const pct = Math.min(100, Math.round((ev.loaded * 100) / ev.total));
        pctEl.textContent = pct + '%';
        fill.style.width = pct + '%';
      }
    };

    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve();
      } else if (xhr.status === 401) {
        showLockModal('PASSCODE REQUIRED FOR UPLOAD');
        reject(new Error('Unauthorized'));
      } else {
        reject(new Error('Upload failed'));
      }
    };
    xhr.onerror = () => reject(new Error('Network error'));
    xhr.send(file);
  });
}

async function handleSelectedFiles(fileList) {
  if (!fileList || fileList.length === 0) return;
  const files = Array.from(fileList);
  const panel = document.getElementById('uploadPanel');
  let uploadedCount = 0;

  for (let i = 0; i < files.length; i++) {
    try {
      await uploadSingleFile(files[i], i + 1, files.length);
      uploadedCount++;
    } catch (e) {
      setStatus('UPLOAD FAILED: ' + files[i].name);
      break;
    }
  }

  document.getElementById('filePicker').value = '';
  setTimeout(() => panel.classList.remove('active'), 800);
  if (uploadedCount > 0) {
    setStatus(`UPLOADED ${uploadedCount} FILE(S) TO ${currentPath}`);
    await loadDirectory(currentPath);
  }
}

// Drag and drop support
const dropZone = document.getElementById('dropZone');
dropZone.addEventListener('dragover', (e) => {
  e.preventDefault();
  dropZone.classList.add('drag-over');
});
dropZone.addEventListener('dragleave', () => {
  dropZone.classList.remove('drag-over');
});
dropZone.addEventListener('drop', (e) => {
  e.preventDefault();
  dropZone.classList.remove('drag-over');
  if (e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files.length > 0) {
    handleSelectedFiles(e.dataTransfer.files);
  }
});

// Keyboard support for Passcode modal & Mkdir modal
window.addEventListener('keydown', (e) => {
  const lockActive = document.getElementById('lockModal').classList.contains('active');
  if (lockActive) {
    if (e.key >= '0' && e.key <= '9') {
      e.preventDefault();
      pinKey(e.key);
    } else if (e.key === 'Backspace') {
      e.preventDefault();
      pinBackspace();
    } else if (e.key === 'Escape') {
      e.preventDefault();
      pinClear();
    } else if (e.key === 'Enter') {
      e.preventDefault();
      submitPasscode();
    }
    return;
  }
  const mkdirActive = document.getElementById('mkdirModal').classList.contains('active');
  if (mkdirActive) {
    if (e.key === 'Enter') {
      e.preventDefault();
      confirmMkdir();
    } else if (e.key === 'Escape') {
      e.preventDefault();
      closeMkdirModal();
    }
  }
});

window.addEventListener('DOMContentLoaded', async () => {
  applyTheme();
  const ok = await refreshServerStatus();
  if (ok) {
    await loadDirectory('/');
  }
});
</script>
</body>
</html>
)RAWHTML";

}  // namespace tab5_fs_web
