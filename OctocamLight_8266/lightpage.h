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
  .status-row { display:grid; grid-template-columns:1fr 1fr 1fr; gap:8px; }
  .pill { background:var(--panel); border:1px solid var(--line); border-radius:10px; padding:10px 4px; text-align:center; font-size:0.75rem; color:var(--muted); }
  .pill b { display:block; font-size:1.05rem; color:var(--fg); margin-top:3px; }
  .field { width:100%; text-align:center; font-size:1.1rem; background:#2c2c2c; color:var(--fg);
    border:1px solid #444; border-radius:8px; padding:10px 6px; margin-bottom:10px; }
  .btn { display:block; width:100%; padding:14px; font-size:1rem; font-weight:700; border:none; border-radius:10px;
    background:#808080; color:var(--fg); cursor:pointer; touch-action:manipulation; margin-bottom:10px; }
  .btn:active { filter:brightness(1.15); }
  .btn:disabled { background:#4a4a4a; color:#888; }
  .btn.go { background:var(--teal); color:#123; }
  .btn.blue { background:#2f6fd0; }
  .note { text-align:center; font-size:0.78rem; color:var(--muted); margin:2px 0 16px; line-height:1.4; }
  .note.err { color:#e08a8a; }
  .note b { color:var(--fg); }
  .bar { width:100%; height:10px; background:#2c2c2c; border:1px solid #444; border-radius:6px; overflow:hidden; margin:6px 0 8px; }
  .fill { height:100%; width:0%; background:var(--teal); transition:width 0.3s ease; }
</style>
</head>
<body>
<div class="wrap">
  <h1>Octocam Light</h1>
  <div class="live" id="live">connecting...</div>

  <div class="status-row">
    <div class="pill">LED<b id="led">--</b></div>
    <div class="pill">Control<b id="src">--</b></div>
    <div class="pill">Paired<b id="paired">--</b></div>
  </div>
  <div class="note" style="margin-top:8px;">While WiFi is on, the light can't hear the remote. The button and the dimmer still work.</div>

  <div class="section-label">Firmware</div>
  <div class="note" id="verNote">Installed: --</div>
  <button class="btn blue" id="checkBtn">Check for Update</button>
  <button class="btn go" id="installBtn" style="display:none;">Install Update</button>
  <div id="progWrap" style="display:none;"><div class="bar"><div class="fill" id="fill"></div></div></div>
  <div class="note" id="updNote"></div>

  <div class="section-label">WiFi Network</div>
  <input class="field" type="text" id="ssid" maxlength="32" placeholder="WiFi name" autocapitalize="off" autocorrect="off">
  <input class="field" type="password" id="pass" maxlength="64" placeholder="password (8+ characters)">
  <button class="btn" id="saveBtn">Save &amp; Connect</button>
  <div class="note" id="wifiNote">The light joins this network when you switch WiFi on (button D5 held 1-3 s). The password is saved only in the light. If it can't join within 20 s it opens its setup network again.</div>

  <button class="btn" id="offBtn" style="margin-top:24px;">Back to Remote</button>
  <div class="note">WiFi off, the light listens to the remote again. This also happens 90 s after you close this page.</div>
</div>

<script>
  const $ = id => document.getElementById(id);
  let st = null, ssidTouched = false, polling = null;
  function post(url, params) {
    return fetch(url, { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:new URLSearchParams(params || {}).toString() });
  }
  function render(d) {
    st = d;
    $('live').textContent = d.name + '  •  ' + (d.mode === 'home' ? ('WiFi ' + d.ssid + ', ' + d.ip) : 'setup network');
    $('led').textContent = d.led > 0 ? d.led + ' %' : 'OFF';
    $('src').textContent = d.source;
    $('paired').textContent = d.paired;
    if (!ssidTouched) $('ssid').value = d.ssid;
    $('pass').placeholder = d.passSet ? 'password saved (type to change)' : 'password (8+ characters)';
    $('verNote').innerHTML = 'Installed: <b>' + d.version + '</b>';

    const home = d.mode === 'home';
    $('checkBtn').disabled = !home || d.upd === 'checking' || d.upd === 'installing';
    const n = $('updNote');
    n.classList.remove('err');
    $('installBtn').style.display = 'none';
    $('progWrap').style.display = 'none';
    if (!home) n.textContent = 'Updates need the home WiFi: enter it below first.';
    else if (d.upd === 'checking') n.textContent = 'Asking GitHub...';
    else if (d.upd === 'checked') {
      if (d.remoteVersion === d.version) {
        n.innerHTML = 'Up to date (GitHub: <b>' + d.remoteVersion + '</b>).';
        $('installBtn').textContent = 'Install Again';
      } else {
        n.innerHTML = 'Available on GitHub: <b>' + d.remoteVersion + '</b>';
        $('installBtn').textContent = 'Install Update';
      }
      $('installBtn').style.display = 'block';
    } else if (d.upd === 'installing') {
      $('progWrap').style.display = 'block';
      $('fill').style.width = d.progress + '%';
      n.textContent = 'Downloading and installing... ' + d.progress + ' %. The light restarts by itself (WiFi is off afterwards).';
    } else if (d.upd === 'error') { n.textContent = d.updError || 'Something went wrong.'; n.classList.add('err'); }
    else n.textContent = '';
  }
  function load() {
    fetch('/api/status').then(r => r.json()).then(render).catch(() => {
      if (st && st.upd === 'installing') $('updNote').textContent = 'The light is restarting with the new firmware. This page stops here: switch WiFi on again (D5 1-3 s) to check the version.';
    });
  }
  $('ssid').addEventListener('input', () => { ssidTouched = true; });
  $('checkBtn').addEventListener('click', () => {
    post('/api/check-update').then(r => r.json()).then(render).catch(() => {});
  });
  $('installBtn').addEventListener('click', () => {
    post('/api/install-update').then(r => r.json()).then(render).catch(() => {});
  });
  $('saveBtn').addEventListener('click', () => {
    const ssid = $('ssid').value.trim(), pass = $('pass').value;
    const note = $('wifiNote');
    if (!ssid) { note.textContent = 'Enter the WiFi name.'; return; }
    if (!pass && !(st && st.passSet && ssid === st.ssid)) { note.textContent = 'Enter the password (8+ characters).'; return; }
    if (pass && pass.length < 8) { note.textContent = 'The password needs at least 8 characters.'; return; }
    const params = { ssid:ssid, pass:pass };
    if (!pass) { note.textContent = 'Nothing to change.'; return; }
    post('/api/wifi', params).then(r => r.json()).then(d => {
      if (!d.ok) { note.textContent = d.error; return; }
      note.innerHTML = 'Saved. The light now joins <b>' + ssid + '</b>; this page disconnects. Look at the light\'s display for its new address.';
      clearInterval(polling);
    }).catch(() => { note.textContent = 'Saved -- the light is joining the network now.'; });
  });
  $('offBtn').addEventListener('click', () => {
    post('/api/wifi-off').catch(() => {});
    clearInterval(polling);
    $('live').textContent = 'WiFi off -- the light listens to the remote again';
  });
  load();
  polling = setInterval(load, 1500);
</script>
</body>
</html>
)HTMLPAGE";
