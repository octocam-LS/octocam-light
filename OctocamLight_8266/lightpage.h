#pragma once

// Web page for the Octocam Light, in its own header so Arduino's ctags scan never sees the JavaScript.

const char PAGE_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="dark">
<title>Octocam Light</title>
<style>
  :root { --bg:#232323; --fg:#f5f5f0; --panel:#2c2c2c; --line:#444; --muted:#999; --steel:#4682b4; --teal:#4fd1c5; }
  * { box-sizing:border-box; }
  html, body { margin:0; padding:0; background:var(--bg); color:var(--fg); font-family:Arial, Helvetica, sans-serif; }
  .wrap { padding:24px 16px 48px; max-width:520px; margin:0 auto; }
  h1 { font-family:'Arial Black', Arial, sans-serif; font-weight:900; text-align:center;
    font-size:clamp(1.6rem,11vw,3.2rem); line-height:1.05; margin:8px 0 6px; }
  .live { text-align:center; font-size:0.8rem; color:var(--muted); margin-bottom:22px; }
  .section-label { font-weight:700; font-size:0.85rem; letter-spacing:0.06em; text-transform:uppercase; color:#ccc; margin:26px 0 12px 2px; }
  .status-row { display:grid; grid-template-columns:1fr 1fr 1fr 1fr; gap:8px; }
  .pill { background:var(--panel); border:1px solid var(--line); border-radius:10px; padding:10px 4px; text-align:center; font-size:0.75rem; color:var(--muted); }
  .pill b { display:block; font-size:1.05rem; color:var(--fg); margin-top:3px; }
  .field { width:100%; text-align:center; font-size:1.1rem; background:#2c2c2c; color:var(--fg);
    border:1px solid #444; border-radius:8px; padding:10px 6px; margin-bottom:10px; }
  .btn { display:block; width:100%; padding:14px; font-size:1rem; font-weight:700; border:none; border-radius:10px;
    background:#808080; color:var(--fg); cursor:pointer; touch-action:manipulation; margin-bottom:10px; }
  .btn:active { filter:brightness(1.15); }
  .btn:disabled { background:#4a4a4a; color:#888; }
  .btn.go { background:var(--teal); color:#123; }
  .note { text-align:center; font-size:0.78rem; color:var(--muted); margin:2px 0 16px; line-height:1.4; }
  .note.err { color:#e08a8a; }
  .note b { color:var(--fg); }

  .small-btn { padding:6px 14px; font-size:0.75rem; font-weight:700; border:none;
    border-radius:8px; background:#808080; color:var(--fg); cursor:pointer; margin:0 4px; }
  .small-btn:active { background:#666; }
  .small-btn.warn { background:#8b1e1e; }
  .small-btn.warn:active { background:#a52a2a; }
  .small-btn.on-state { background:var(--teal); color:#123; }
  .pairing-block { text-align:center; margin-top:0; margin-bottom:12px; }
  .pairing-count { font-size:0.85rem; color:#ccc; margin-top:10px; }
  .pairing-count b { font-weight:900; color:var(--fg); }

  /* Paired Devices: grey = ignored, blue = listened to */
  .sender-list { display:flex; flex-direction:column; gap:8px; }
  .sender-btn { width:100%; padding:14px 12px; font-size:1rem; font-weight:700; border:none;
    border-radius:10px; background:#4a4a4a; color:#999; cursor:pointer; touch-action:manipulation;
    word-break:break-word; transition: background 0.15s ease; user-select:none; -webkit-user-select:none; }
  .sender-btn.active { background:#2f6fd0; color:var(--fg); }
  .sender-btn:active { filter:brightness(1.15); }
  .sender-btn .sender-mac { display:block; font-size:0.65rem; font-weight:400; opacity:0.75; margin-top:3px; }
  .sender-row { display:flex; align-items:stretch; gap:8px; }
  .sender-row .sender-btn { flex:1 1 auto; min-width:0; width:auto; }
  .forget-btn { flex:0 0 auto; width:84px; padding:6px 4px; font-size:0.8rem; font-weight:700; border:none;
    border-radius:10px; background:#8b1e1e; color:var(--fg); cursor:pointer; touch-action:manipulation; }
  .forget-btn:active { background:#a52a2a; }
  .forget-btn.armed { background:#c0392b; }
</style>
</head>
<body>
<div class="wrap">
  <h1>Octocam Light</h1>
  <div class="live" id="live">connecting...</div>

  <div class="status-row">
    <div class="pill">LED<b id="led">--</b></div>
    <div class="pill">Fan<b id="fan">--</b></div>
    <div class="pill">Control<b id="src">--</b></div>
    <div class="pill">Paired<b id="paired">--</b></div>
  </div>
  <div class="note" style="margin-top:8px;">The remote keeps working while this network is on.</div>

  <div class="section-label">Shooting Max</div>
  <input class="field" type="text" inputmode="numeric" id="shootMax" value="">
  <div class="note" id="shootNote">The shooting light never goes above this, whatever the remote asks for. Saved in the light.</div>

  <div class="section-label">Paired Devices</div>
  <div class="pairing-block">
    <button class="small-btn" id="pairBtn">Pair</button>
    <button class="small-btn warn" id="clearPairingsBtn">Clear Pairings</button>
    <div class="pairing-count" id="pairingCount">Paired remotes: <b id="pairedCount">0</b></div>
  </div>
  <div class="sender-list" id="senderList"></div>
  <div class="note" style="margin-top:8px;">Pair opens an 8 s window: hold the remote's TX button 2.5-10 s meanwhile. Tap a remote to switch it on (blue) or off; the light ignores remotes that are off. Forget removes one (tap twice).</div>

  <div class="section-label">Firmware</div>
  <div class="note" id="verNote">Installed: --</div>
  <button class="btn go" id="updBtn">Update Firmware</button>
  <div class="note" id="updNote"></div>

  <div class="section-label">Home WiFi (for updates)</div>
  <input class="field" type="text" id="ssid" maxlength="32" placeholder="WiFi name" autocapitalize="off" autocorrect="off">
  <input class="field" type="password" id="pass" maxlength="64" placeholder="password (8+ characters)">
  <button class="btn" id="saveBtn">Save</button>
  <div class="note" id="wifiNote">Used only for Update Firmware. The password is saved only in the light.</div>
</div>

<script>
  const $ = id => document.getElementById(id);
  let st = null, ssidTouched = false, away = false;
  const HOW = 'The light leaves this network, joins your WiFi, gets the newest firmware from GitHub and installs it by itself (about a minute). ';
  function post(url, params) {
    return fetch(url, { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:new URLSearchParams(params || {}).toString() });
  }

  // ----- Shooting Max: "100 %" in the field, bare number while typing -----
  let shootMax = 100;
  const smIn = $('shootMax');
  function showPct() { smIn.value = (document.activeElement === smIn) ? String(shootMax) : (shootMax + ' %'); }
  smIn.addEventListener('focus', () => { smIn.value = String(shootMax); smIn.select(); });
  smIn.addEventListener('blur', () => {
    let v = parseInt(smIn.value, 10);
    if (isNaN(v)) v = shootMax;
    v = Math.min(100, Math.max(1, v));
    const changed = v !== shootMax;
    shootMax = v;
    showPct();
    if (changed) post('/api/settings', { shootMax:v }).then(r => r.json()).then(render).catch(() => {});
  });
  smIn.addEventListener('keydown', e => { if (e.key === 'Enter') smIn.blur(); });

  function render(d) {
    st = d;
    $('live').textContent = d.name + '  •  http://' + d.ip;
    $('led').textContent = d.led > 0 ? d.led + ' %' : 'OFF';
    $('fan').textContent = d.fan + ' %';
    $('src').textContent = d.source;
    $('paired').textContent = d.paired;
    if (document.activeElement !== smIn) { shootMax = d.shootMax; showPct(); }
    if (!ssidTouched) $('ssid').value = d.ssid;
    $('pass').placeholder = d.passSet ? 'password saved (type to change)' : 'password (8+ characters)';
    $('verNote').innerHTML = 'Installed: <b>' + d.version + '</b>';

    const busy = d.upd === 'joining' || d.upd === 'checking' || d.upd === 'installing';
    $('updBtn').disabled = busy || !d.passSet;
    const n = $('updNote');
    n.classList.remove('err');
    if (away) return;                         // the update message stays until the light is back
    if (!d.passSet) n.textContent = 'Enter your home WiFi below first.';
    else if (d.upd === 'uptodate') n.innerHTML = 'Up to date (GitHub: <b>' + d.remoteVersion + '</b>).';
    else if (d.upd === 'error') { n.textContent = 'Update failed: ' + (d.updError || 'something went wrong.'); n.classList.add('err'); }
    else if (busy) n.textContent = 'Updating...';
    else n.textContent = HOW;
  }
  function load() {
    fetch('/api/status').then(r => r.json()).then(d => {
      if (away && d.upd !== 'joining') away = false;   // back on this network: show the result
      render(d);
    }).catch(() => {
      if (away) $('live').textContent = 'light is away updating...';
    });
  }

  // ----- Paired Devices -----
  const senderList = $('senderList');
  let senderTapBusy = false, forgetArmedMac = null, forgetArmTimer = null;
  function senderLabel(s) { return s.name ? s.name : ('Remote ' + s.mac.slice(-5)); }
  function renderSenders(data) {
    const list = (data && data.senders) ? data.senders : [];
    $('pairedCount').textContent = list.length;
    const pb = $('pairBtn');
    if (data && data.pairing > 0) { pb.textContent = 'Pairing... ' + data.pairing + ' s'; pb.classList.add('on-state'); }
    else { pb.textContent = 'Pair'; pb.classList.remove('on-state'); }
    senderList.innerHTML = '';
    if (list.length === 0) {
      const empty = document.createElement('div');
      empty.className = 'note';
      empty.textContent = 'No paired remotes yet.';
      senderList.appendChild(empty);
      return;
    }
    const counts = {};
    list.forEach(s => { const l = senderLabel(s); counts[l] = (counts[l] || 0) + 1; });
    list.forEach(s => {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'sender-btn' + (s.active ? ' active' : '');
      b.textContent = senderLabel(s);
      if (counts[senderLabel(s)] > 1) {
        const m = document.createElement('span');
        m.className = 'sender-mac';
        m.textContent = s.mac;
        b.appendChild(m);
      }
      b.addEventListener('click', () => {
        if (senderTapBusy) return;
        senderTapBusy = true;
        const on = !b.classList.contains('active');
        b.classList.toggle('active', on);
        post('/api/sender-active', { mac:s.mac, active:on ? 1 : 0 })
          .then(r => r.json()).then(renderSenders)
          .catch(() => { b.classList.toggle('active', !on); })
          .finally(() => { senderTapBusy = false; });
      });
      const f = document.createElement('button');
      f.type = 'button';
      f.className = 'forget-btn';
      f.textContent = 'Forget';
      f.addEventListener('click', () => {
        if (forgetArmedMac !== s.mac) {
          document.querySelectorAll('.forget-btn.armed').forEach(x => { x.classList.remove('armed'); x.textContent = 'Forget'; });
          forgetArmedMac = s.mac;
          f.classList.add('armed');
          f.textContent = 'Confirm?';
          clearTimeout(forgetArmTimer);
          forgetArmTimer = setTimeout(() => { forgetArmedMac = null; f.classList.remove('armed'); f.textContent = 'Forget'; }, 5000);
          return;
        }
        clearTimeout(forgetArmTimer);
        forgetArmedMac = null;
        senderTapBusy = true;
        post('/api/sender-forget', { mac:s.mac }).then(r => r.json()).then(renderSenders)
          .catch(() => {}).finally(() => { senderTapBusy = false; });
      });
      const row = document.createElement('div');
      row.className = 'sender-row';
      row.appendChild(b);
      row.appendChild(f);
      senderList.appendChild(row);
    });
  }
  function loadSenders() {
    if (senderTapBusy || forgetArmedMac) return;
    fetch('/api/senders').then(r => r.json()).then(renderSenders).catch(() => {});
  }
  $('pairBtn').addEventListener('click', () => {
    post('/api/pair').then(r => r.json()).then(renderSenders).catch(() => {});
  });
  let clearArmed = false, clearArmTimer = null;
  $('clearPairingsBtn').addEventListener('click', () => {
    const cb = $('clearPairingsBtn');
    if (!clearArmed) {
      clearArmed = true;
      cb.textContent = 'Tap again to confirm';
      clearArmTimer = setTimeout(() => { clearArmed = false; cb.textContent = 'Clear Pairings'; }, 5000);
      return;
    }
    clearTimeout(clearArmTimer);
    clearArmed = false;
    cb.textContent = 'Clear Pairings';
    post('/api/clear-pairings').then(r => r.json()).then(renderSenders).catch(() => {});
  });

  $('ssid').addEventListener('input', () => { ssidTouched = true; });
  $('updBtn').addEventListener('click', () => {
    post('/api/update').then(r => r.json()).then(d => {
      if (d.upd === 'joining') {
        away = true;
        $('updNote').innerHTML = HOW + 'Your phone loses this network meanwhile: afterwards reconnect to <b>' +
          d.name + '</b> and this page shows the result. A new version shows up as "Installed" above.';
      }
      render(d);
    }).catch(() => {});
  });
  $('saveBtn').addEventListener('click', () => {
    const ssid = $('ssid').value.trim(), pass = $('pass').value;
    const note = $('wifiNote');
    if (!ssid) { note.textContent = 'Enter the WiFi name.'; return; }
    if (!pass && !(st && st.passSet)) { note.textContent = 'Enter the password (8+ characters).'; return; }
    if (pass && pass.length < 8) { note.textContent = 'The password needs at least 8 characters.'; return; }
    post('/api/wifi', { ssid:ssid, pass:pass }).then(r => r.json()).then(d => {
      if (!d.ok) { note.textContent = d.error; return; }
      $('pass').value = '';
      ssidTouched = false;
      note.innerHTML = 'Saved: <b>' + ssid + '</b>. Used only for Update Firmware.';
      load();
    }).catch(() => { note.textContent = 'Could not save.'; });
  });
  load();
  loadSenders();
  setInterval(load, 1500);
  setInterval(loadSenders, 1000);
</script>
</body>
</html>
)HTMLPAGE";
