/* SPDX-License-Identifier: MIT -- Molecular website: the home page's molecule, moving as the pedal moves it (the
 * firmware's core/motion.c, simplified): each disc wanders on a smoothly turning heading, bonds pull like springs,
 * crowded discs push apart, a gentle pull to the middle; every few seconds the molecule rearranges itself (rewired,
 * an atom grown or pruned, swelling in and shrinking away). Still for anyone who asks for reduced motion. */
(function () {
  var cv = document.querySelector(".drift");
  if (!cv || !cv.getContext) return;
  var g = cv.getContext("2d"), W = cv.width, H = cv.height, C = W / 2, R = W / 2 - 6;
  var KINDS = [["FL", "#50dcff"], ["FO", "#ff9628"], ["SH", "#ff64d2"], ["MO", "#78ff78"], ["TA", "#b4a0ff"],
               ["SP", "#d2d2ff"], ["CB", "#ff9628"], ["BK", "#78ff78"], ["XM", "#ff64d2"], ["CL", "#b4a0ff"],
               ["VO", "#50dcff"], ["RV", "#d2d2ff"]];
  var BOND = 128, RG = 50, RA = 40;
  var nodes = [], still = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  function rnd(a, b) { return a + Math.random() * (b - a); }
  function disc(x, y, kind, parent) {
    return { x: x, y: y, h: rnd(0, 6.283), t: 0, sp: rnd(0.6, 1.4), k: kind, p: parent, size: 0, leave: false };
  }
  function start() {
    nodes = [disc(C - 40, C + 20, null, -1)];
    nodes[0].size = 1;
    var a = [[C + 50, C - 50, 0], [C + 150, C + 10, 1], [C + 40, C - 170, 1], [C - 150, C + 90, 0]];
    a.forEach(function (q) { var d = disc(q[0], q[1], KINDS[(Math.random() * KINDS.length) | 0], q[2]); d.size = 1; nodes.push(d); });
  }
  function children(i) { var n = 0; nodes.forEach(function (d) { if (d.p === i && !d.leave) n++; }); return n; }
  function event() {
    var w = Math.random() * 8 | 0, i, j;
    if (w < 2) {                                        // rewired: each atom to another parent with room
      for (i = 1; i < nodes.length; i++) {
        var c = [];
        for (j = 0; j < i; j++) if (children(j) < (j ? 2 : 3)) c.push(j);
        if (c.length) nodes[i].p = c[(Math.random() * c.length) | 0];
      }
    } else if (w < 4 && nodes.length < 6) {             // grown, beside an atom with room
      var c2 = [];
      for (j = 0; j < nodes.length; j++) if (children(j) < (j ? 2 : 3)) c2.push(j);
      var p = c2[(Math.random() * c2.length) | 0], a = rnd(0, 6.283);
      nodes.push(disc(nodes[p].x + Math.cos(a) * BOND, nodes[p].y + Math.sin(a) * BOND,
                      KINDS[(Math.random() * KINDS.length) | 0], p));
    } else if (w === 4 && nodes.length > 2) {           // pruned: an outermost atom shrinks away
      var leaves = [];
      for (i = 1; i < nodes.length; i++) if (!children(i)) leaves.push(i);
      nodes[leaves[(Math.random() * leaves.length) | 0]].leave = true;
    }
  }
  function toward(a, b, want, k, lim) {
    var dx = b.x - a.x, dy = b.y - a.y, d = Math.hypot(dx, dy);
    if (d < 1) return;
    var m = Math.max(-lim, Math.min(lim, (d - want) * k / 2)) / d;
    a.x += dx * m; a.y += dy * m; b.x -= dx * m; b.y -= dy * m;
  }
  function step(dt) {
    var v = 30 * dt, k = Math.min(1, dt / 0.25), lim = 40 * dt, i, j;
    nodes.forEach(function (n) {
      n.t += (Math.random() - 0.5) * 9 * dt; n.t *= 0.98; n.t = Math.max(-1.6, Math.min(1.6, n.t));
      n.h += n.t * dt;
      n.x += Math.cos(n.h) * v * n.sp + (C - n.x) * dt / 6;
      n.y += Math.sin(n.h) * v * n.sp + (C - n.y) * dt / 6;
      n.size = n.leave ? Math.max(0, n.size - dt) : Math.min(1, n.size + dt);
    });
    for (i = 1; i < nodes.length; i++) toward(nodes[i], nodes[nodes[i].p], BOND, k, lim);
    for (i = 0; i < nodes.length; i++)
      for (j = i + 1; j < nodes.length; j++) {
        var m = (i ? RA : RG) + RA + 16, dx = nodes[j].x - nodes[i].x, dy = nodes[j].y - nodes[i].y;
        if (dx * dx + dy * dy < m * m) toward(nodes[i], nodes[j], m, 2 * k, lim);
      }
    nodes.forEach(function (n, i) {                     // inside the circle
      var r = (i ? RA : RG) + 4, dx = n.x - C, dy = n.y - C, d = Math.hypot(dx, dy);
      if (d > R - r) { n.x = C + dx * (R - r) / d; n.y = C + dy * (R - r) / d; n.h = Math.atan2(-dy, -dx) + rnd(-0.8, 0.8); }
    });
    for (i = nodes.length - 1; i > 0; i--)              // a pruned atom gone: the rest renumbered
      if (nodes[i].leave && nodes[i].size === 0) {
        nodes.splice(i, 1);
        nodes.forEach(function (n) { if (n.p > i) n.p--; else if (n.p === i) n.p = 0; });
      }
  }
  function draw() {
    g.clearRect(0, 0, W, H);
    g.lineWidth = 8; g.strokeStyle = "#eceae4";
    for (var i = 1; i < nodes.length; i++) {
      var a = nodes[i], b = nodes[a.p];
      g.globalAlpha = Math.min(a.size, b.size) * 0.9;
      g.beginPath(); g.moveTo(a.x, a.y); g.lineTo(b.x, b.y); g.stroke();
    }
    g.globalAlpha = 1;
    nodes.forEach(function (n, i) {
      var r = (i ? RA : RG) * n.size;
      if (r < 2) return;
      g.fillStyle = i ? n.k[1] : "#ffe63c";
      g.beginPath(); g.arc(n.x, n.y, r, 0, 6.283); g.fill();
      if (n.size > 0.8) {
        g.fillStyle = "#000"; g.font = "700 " + (i ? 30 : 34) + "px 'IBM Plex Mono', monospace";
        g.textAlign = "center"; g.textBaseline = "middle";
        g.fillText(i ? n.k[0] : "FM", n.x, n.y + 1);
      }
    });
  }
  start();
  var last = performance.now(), next = 2.5;
  function frame(t) {
    var dt = Math.min(0.05, (t - last) / 1000);
    last = t;
    step(dt);
    if ((next -= dt) <= 0) { event(); next = rnd(2.5, 4); }
    draw();
    requestAnimationFrame(frame);
  }
  if (still) { for (var s = 0; s < 120; s++) step(0.05); draw(); }
  else requestAnimationFrame(frame);
})();
