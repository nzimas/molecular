// SPDX-License-Identifier: GPL-3.0-only
// The install page: loads Molecular (firmware/latest.json, then its application section), finds the Pocket Master
// over Web MIDI and installs it with pmota.js; or installs a Sonicake firmware file the user chooses.
"use strict";

(() => {
  const $ = id => document.getElementById(id);
  const status = (t, cls) => { $("status").textContent = t; $("status").className = "status" + (cls ? " " + cls : ""); };
  const detail = t => { $("detail").textContent = t || ""; };
  let fw = null, pedal = null, busy = false, stock = null;

  function bar(done, total) {
    $("bar").hidden = false;
    $("bar").firstElementChild.style.width = (100 * done / total).toFixed(1) + "%";
  }
  function refresh() {
    const ready = !busy && $("agree").checked;
    $("connect").disabled = busy || !fw;
    $("install").disabled = !ready || !fw || !(pedal && pedal.present);
    $("stockinstall").disabled = busy || !stock || !(pedal && pedal.present);
    if (pedal) $("f-dev").textContent = pedal.present ? "connected" : "not connected";
  }
  async function sha256(buf) {
    const h = new Uint8Array(await crypto.subtle.digest("SHA-256", buf));
    return Array.from(h, b => b.toString(16).padStart(2, "0")).join("");
  }

  async function load() {
    try {
      const meta = await (await fetch("firmware/latest.json", { cache: "no-cache" })).json();
      const buf = await (await fetch("firmware/" + meta.file + "?v=" + meta.sha256.slice(0, 10))).arrayBuffer();
      if (buf.byteLength !== meta.size || await sha256(buf) !== meta.sha256)
        throw new Error("The firmware file did not load intact. Reload the page.");
      fw = { meta, data: new Uint8Array(buf) };
      $("facts").hidden = false;
      $("f-fw").textContent = "Molecular " + meta.version + " (" + Math.round(meta.size / 1024) + " KB)";
      status("Ready.");
      detail("Connect the pedal, switch it on, then find it.");
    } catch (e) {
      status("The firmware could not be loaded.", "bad");
      detail(e.message);
    }
    refresh();
  }

  async function connect() {
    try {
      if (!pedal) {
        pedal = await PMOTA.Pedal.open();
        pedal.onchange = refresh;
      }
      pedal.bind();
      if (pedal.present) { status("Pocket Master found."); detail("Tick the box above, then install."); }
      else { status("No Pocket Master found.", "bad"); detail("Check the cable carries data, switch the pedal on, close other MIDI programs, then try again."); }
    } catch (e) {
      status("MIDI is not available.", "bad");
      detail(e.message + (e.name === "SecurityError" || e.name === "NotAllowedError"
        ? " Allow this page to control MIDI devices (the icon in the address bar), then reload." : ""));
    }
    refresh();
  }

  async function run(label, sections, after) {
    busy = true; refresh();
    window.onbeforeunload = () => "The update is running.";
    try {
      const msgs = PMOTA.updateMessages(sections);
      await pedal.enterUpdate(t => { status(t); detail(""); });
      status("Writing " + label + "…");
      detail("Keep the pedal connected and this page open.");
      const t0 = performance.now();
      await pedal.send(msgs, (n, total) => {
        bar(n, total);
        detail(Math.round(100 * n / total) + " %  ·  " + Math.round((performance.now() - t0) / 1000) + " s");
      });
      status("Done.", "ok");
      detail(after);
    } catch (e) {
      status("The install did not finish.", "bad");
      detail(e.message);
    }
    window.onbeforeunload = null;
    busy = false; refresh();
  }

  $("agree").addEventListener("change", refresh);
  $("connect").addEventListener("click", connect);
  $("install").addEventListener("click", () => run("Molecular " + fw.meta.version, [{ tag: "b", data: fw.data }],
    "The pedal restarts into Molecular, already sounding. Hold the right button to start MOTION; the manual has the rest."));
  $("stockfile").addEventListener("change", async e => {
    stock = null;
    const f = e.target.files[0];
    if (f) {
      try {
        const img = PMOTA.parseHTFW(await f.arrayBuffer());
        if (img.product !== "Pocket Master") throw new Error("This firmware is for the " + img.product + ", not the Pocket Master.");
        stock = img;
        $("stockinfo").textContent = "Sonicake firmware " + img.version + " for the Pocket Master, " + img.sections.length +
          " sections. Find the pedal above first (Find my Pocket Master), then install.";
      } catch (err) {
        $("stockinfo").textContent = err.message;
      }
    }
    refresh();
  });
  $("stockinstall").addEventListener("click", () => {
    if (confirm("Install Sonicake's firmware " + stock.version + "? Molecular will be replaced, and its patches are kept in flash but unused."))
      run("Sonicake's firmware " + stock.version + " (a few minutes)", stock.sections,
        "The pedal restarts with Sonicake's firmware " + stock.version + ".");
  });
  load();
})();
