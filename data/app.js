(() => {
  "use strict";

  let sessionPin = "";
  let currentPlanes = [];
  let selectedPlane = null;
  let radarRangeKm = 100;
  let showTrails = true;
  let showVectors = true;
  let sweepAngle = 0;
  let activeTheme = 0; // 0: Green, 1: Cyan, 2: Amber
  let lastDataUpdateMs = Date.now();
  let mapTilesEnabled = false;
  let mapTileCache = {}; // url -> ImageBitmap|null|'loading'
  let mapCenter = { lat: 22.5726, lon: 88.3639 };
  let mapZoom = 8;

  // History for breadcrumb trails: hex -> [{dst, brg}]
  const planeTrails = new Map();

  // -------------------------------------------------------------
  // API Helpers (No PIN block for local LAN access)
  // -------------------------------------------------------------
  async function apiGet(url) {
    const res = await fetch(url, { headers: pinHeaders() });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return res.json();
  }

  async function apiPost(url, body) {
    const res = await fetch(url, {
      method: "POST",
      headers: Object.assign({ "Content-Type": "application/json" }, pinHeaders()),
      body: JSON.stringify(body || {})
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return res.json();
  }

  function pinHeaders() {
    return sessionPin ? { "X-Config-Pin": sessionPin } : {};
  }

  // -------------------------------------------------------------
  // Toast & Auto-Save Feedback
  // -------------------------------------------------------------
  const toastEl = document.getElementById("toast");
  let toastTimer;
  function showToast(msg, isError = false) {
    if (!toastEl) return;
    toastEl.textContent = msg;
    toastEl.className = isError ? "toast error show" : "toast show";
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => {
      toastEl.classList.remove("show");
    }, 3200);
  }

  const autoSaveBadge = document.getElementById("autoSaveBadge");
  let autoSaveTimer;
  function setSavingIndicator(saving, msg = "Auto-Saved to Hardware") {
    if (!autoSaveBadge) return;
    const label = autoSaveBadge.querySelector(".autosave-label");
    if (saving) {
      autoSaveBadge.classList.add("saving");
      if (label) label.textContent = "Saving to Hardware...";
    } else {
      autoSaveBadge.classList.remove("saving");
      if (label) label.textContent = msg;
      clearTimeout(autoSaveTimer);
      autoSaveTimer = setTimeout(() => {
        if (label) label.textContent = "Auto-Saved to Hardware";
      }, 2500);
    }
  }

  // -------------------------------------------------------------
  // Theme Palettes
  // -------------------------------------------------------------
  const THEME_PALETTES = {
    0: { // Matrix Green
      bg: "#04070B",
      grid: "rgba(0, 255, 102, 0.18)",
      gridSub: "rgba(0, 255, 102, 0.08)",
      accent: "#00FF66",
      glow: "rgba(0, 255, 102, 0.25)",
      text: "#E0F5E9"
    },
    1: { // Ice Cyan
      bg: "#04080E",
      grid: "rgba(0, 229, 255, 0.18)",
      gridSub: "rgba(0, 229, 255, 0.08)",
      accent: "#00E5FF",
      glow: "rgba(0, 229, 255, 0.25)",
      text: "#E0F7FA"
    },
    2: { // Amber Retro
      bg: "#0B0703",
      grid: "rgba(255, 179, 0, 0.20)",
      gridSub: "rgba(255, 179, 0, 0.09)",
      accent: "#FFB300",
      glow: "rgba(255, 179, 0, 0.28)",
      text: "#FFF3E0"
    }
  };

  // -------------------------------------------------------------
  // Cockpit Navigation Tabs (3 Tabs)
  // -------------------------------------------------------------
  document.querySelectorAll(".nav-tab").forEach(tab => {
    tab.addEventListener("click", () => {
      const targetTab = tab.dataset.tab;
      document.querySelectorAll(".nav-tab").forEach(t => t.classList.remove("active"));
      document.querySelectorAll(".cockpit-panel").forEach(p => p.classList.remove("active"));

      tab.classList.add("active");
      const targetPanel = document.getElementById(`panel-${targetTab}`);
      if (targetPanel) targetPanel.classList.add("active");

      // Resize radar canvas if entering radar view
      if (targetTab === "radar") {
        resizeRadarCanvas();
      }
    });
  });

  // -------------------------------------------------------------
  // Radar Scope Canvas Rendering (60 FPS)
  // -------------------------------------------------------------
  const canvas = document.getElementById("radarCanvas");
  const ctx = canvas ? canvas.getContext("2d") : null;

  function resizeRadarCanvas() {
    if (!canvas) return;
    const rect = canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    const size = Math.min(rect.width, rect.height) || 540;
    if (canvas.width !== size * dpr) {
      canvas.width = size * dpr;
      canvas.height = size * dpr;
    }
  }

  window.addEventListener("resize", resizeRadarCanvas);
  setTimeout(resizeRadarCanvas, 100);

  function renderRadar() {
    if (!ctx || !canvas) return;

    const w = canvas.width;
    const h = canvas.height;
    if (w === 0 || h === 0) {
      requestAnimationFrame(renderRadar);
      return;
    }

    const cx = w / 2;
    const cy = h / 2;
    const maxR = Math.min(cx, cy) * 0.90; // scope radius leaving outer margin for compass labels

    const colors = THEME_PALETTES[activeTheme] || THEME_PALETTES[0];

    // Clear Canvas
    ctx.clearRect(0, 0, w, h);

    // Deep radar scope circle
    ctx.fillStyle = colors.bg;
    ctx.beginPath();
    ctx.arc(cx, cy, maxR, 0, Math.PI * 2);
    ctx.fill();

    // Map tile overlay (optional, underneath all radar elements)
    drawMapOverlay(ctx, cx, cy, maxR, mapCenter.lat, mapCenter.lon, radarRangeKm);

    // Concentric Range Rings (3 rings: 33%, 66%, 100%)
    ctx.lineWidth = 1;
    [0.333, 0.666, 1.0].forEach((ratio, idx) => {
      const r = maxR * ratio;
      ctx.strokeStyle = (idx === 2) ? colors.accent : colors.grid;
      ctx.beginPath();
      ctx.arc(cx, cy, r, 0, Math.PI * 2);
      ctx.stroke();

      // Range text on vertical axis
      const dist = Math.round(radarRangeKm * ratio);
      ctx.font = `${Math.max(9, Math.round(w * 0.02))}px 'JetBrains Mono', monospace`;
      ctx.fillStyle = colors.accent;
      ctx.textAlign = "left";
      ctx.fillText(`${dist}km`, cx + 6, cy - r + 12);
    });

    // Crosshair Lines
    ctx.strokeStyle = colors.gridSub;
    ctx.beginPath();
    ctx.moveTo(cx - maxR, cy);
    ctx.lineTo(cx + maxR, cy);
    ctx.moveTo(cx, cy - maxR);
    ctx.lineTo(cx, cy + maxR);
    ctx.stroke();

    // Compass Radial Ticks & Degree Labels (Outside scope, so legend never collides)
    for (let deg = 0; deg < 360; deg += 30) {
      const rad = (deg - 90) * Math.PI / 180;
      const x1 = cx + Math.cos(rad) * (maxR - 6);
      const y1 = cy + Math.sin(rad) * (maxR - 6);
      const x2 = cx + Math.cos(rad) * maxR;
      const y2 = cy + Math.sin(rad) * maxR;

      ctx.strokeStyle = colors.grid;
      ctx.beginPath();
      ctx.moveTo(x1, y1);
      ctx.lineTo(x2, y2);
      ctx.stroke();

      // Compass label
      const tx = cx + Math.cos(rad) * (maxR + 14);
      const ty = cy + Math.sin(rad) * (maxR + 14) + 4;
      ctx.font = `bold ${Math.max(10, Math.round(w * 0.022))}px 'JetBrains Mono', monospace`;
      ctx.fillStyle = colors.accent;
      ctx.textAlign = "center";
      let lbl = `${deg}°`;
      if (deg === 0) lbl = "N";
      else if (deg === 90) lbl = "E";
      else if (deg === 180) lbl = "S";
      else if (deg === 270) lbl = "W";
      ctx.fillText(lbl, tx, ty);
    }

    // Rotating Radar Sweep Beam with Authentic CRT Phosphor Fade Trail
    // Layered under all targets, graticule, and labels so it NEVER blocks telemetry
    const sweepRad = (sweepAngle - 90) * Math.PI / 180;
    const trailSpan = (55 * Math.PI) / 180; // ~55° glowing wake
    const tailRad = sweepRad - trailSpan;

    // Conic gradient anchored at trailing edge (tailRad) fading smoothly forward to leading edge (sweepRad)
    const sweepGrad = ctx.createConicGradient(tailRad, cx, cy);
    const spanRatio = trailSpan / (Math.PI * 2);

    sweepGrad.addColorStop(0, "transparent");
    sweepGrad.addColorStop(spanRatio * 0.25, "transparent");
    sweepGrad.addColorStop(spanRatio * 0.55, colors.glow);
    sweepGrad.addColorStop(spanRatio * 0.85, colors.accent + "44");
    sweepGrad.addColorStop(spanRatio, colors.accent + "88");
    sweepGrad.addColorStop(spanRatio + 0.001, "transparent");
    sweepGrad.addColorStop(1, "transparent");

    ctx.save();
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.arc(cx, cy, maxR, tailRad, sweepRad, false);
    ctx.closePath();
    ctx.fillStyle = sweepGrad;
    ctx.fill();

    // Sharp glowing leading sweep line
    ctx.strokeStyle = colors.accent;
    ctx.lineWidth = 1.8;
    ctx.shadowColor = colors.accent;
    ctx.shadowBlur = 4;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(cx + Math.cos(sweepRad) * maxR, cy + Math.sin(sweepRad) * maxR);
    ctx.stroke();
    ctx.restore();

    // Advance sweep angle clockwise
    sweepAngle = (sweepAngle + 1.2) % 360;

    // Breadcrumb Trails
    if (showTrails) {
      currentPlanes.forEach(p => {
        const trail = planeTrails.get(p.hex);
        if (trail && trail.length > 1) {
          ctx.strokeStyle = colors.grid;
          ctx.lineWidth = 1.2;
          ctx.beginPath();
          trail.forEach((pt, i) => {
            if (pt.dst > radarRangeKm) return;
            const r = (pt.dst / radarRangeKm) * maxR;
            const rad = (pt.brg - 90) * Math.PI / 180;
            const x = cx + Math.cos(rad) * r;
            const y = cy + Math.sin(rad) * r;
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
          });
          ctx.stroke();
        }
      });
    }

    // Aircraft Blips & Labels
    currentPlanes.forEach(p => {
      if (p.dst > radarRangeKm) return;

      const r = (p.dst / radarRangeKm) * maxR;
      const rad = (p.brg - 90) * Math.PI / 180;
      const px = cx + Math.cos(rad) * r;
      const py = cy + Math.sin(rad) * r;

      // Color based on altitude
      let altColor = "#00FF66"; // Cruise (>28k ft)
      if (p.alt < 10000) altColor = "#FF453A"; // Low approach (<10k ft)
      else if (p.alt < 28000) altColor = "#FFB300"; // Mid transit

      // Draw Heading Vector
      if (showVectors && p.spd > 15) {
        const trkRad = (p.track - 90) * Math.PI / 180;
        const vecLen = Math.min(26, Math.max(10, p.spd * 0.05));
        ctx.strokeStyle = altColor;
        ctx.lineWidth = 1.4;
        ctx.beginPath();
        ctx.moveTo(px, py);
        ctx.lineTo(px + Math.cos(trkRad) * vecLen, py + Math.sin(trkRad) * vecLen);
        ctx.stroke();
      }

      // Draw tactical blip (diamond + heading tick)
      drawAircraftGlyph(ctx, px, py, p.track, altColor);

      // Highlight if selected
      if (selectedPlane && selectedPlane.hex === p.hex) {
        ctx.strokeStyle = colors.accent;
        ctx.lineWidth = 2;
        const pulse = 9 + Math.sin(Date.now() / 150) * 3;
        ctx.beginPath();
        ctx.arc(px, py, pulse, 0, Math.PI * 2);
        ctx.stroke();

        ctx.setLineDash([3, 3]);
        ctx.beginPath();
        ctx.arc(px, py, pulse + 5, 0, Math.PI * 2);
        ctx.stroke();
        ctx.setLineDash([]);
      }

      // Callsign Tag
      ctx.font = `bold ${Math.max(9, Math.round(w * 0.02))}px 'JetBrains Mono', monospace`;
      ctx.fillStyle = "#FFFFFF";
      ctx.textAlign = "left";
      ctx.fillText(p.flight || p.hex, px + 9, py - 4);
    });

    requestAnimationFrame(renderRadar);
  }

  function drawAircraftGlyph(ctx, x, y, trackDeg, color) {
    // Tactical radar blip: filled diamond + short heading tick
    ctx.save();

    // Glow
    ctx.shadowColor = color;
    ctx.shadowBlur = 6;

    // Diamond body
    const d = 5;
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.moveTo(x, y - d);   // Top
    ctx.lineTo(x + d, y);   // Right
    ctx.lineTo(x, y + d);   // Bottom
    ctx.lineTo(x - d, y);   // Left
    ctx.closePath();
    ctx.fill();

    // White center core
    ctx.fillStyle = "rgba(255,255,255,0.9)";
    ctx.beginPath();
    ctx.arc(x, y, 1.5, 0, Math.PI * 2);
    ctx.fill();

    // Heading tick line (pointing in flight direction)
    const trkRad = (trackDeg - 90) * Math.PI / 180;
    ctx.strokeStyle = color;
    ctx.lineWidth = 1.8;
    ctx.shadowBlur = 4;
    ctx.beginPath();
    ctx.moveTo(x + Math.cos(trkRad) * d, y + Math.sin(trkRad) * d);
    ctx.lineTo(x + Math.cos(trkRad) * (d + 9), y + Math.sin(trkRad) * (d + 9));
    ctx.stroke();

    ctx.restore();
  }

  // Radar Scope Click Target Detection
  if (canvas) {
    const handleScopeClick = (e) => {
      const rect = canvas.getBoundingClientRect();
      const clientX = e.touches ? e.touches[0].clientX : e.clientX;
      const clientY = e.touches ? e.touches[0].clientY : e.clientY;
      const x = (clientX - rect.left) * (canvas.width / rect.width);
      const y = (clientY - rect.top) * (canvas.height / rect.height);

      const cx = canvas.width / 2;
      const cy = canvas.height / 2;
      const maxR = Math.min(cx, cy) * 0.90;

      let nearest = null;
      let minDis = 28 * (canvas.width / rect.width);

      currentPlanes.forEach(p => {
        if (p.dst > radarRangeKm) return;
        const r = (p.dst / radarRangeKm) * maxR;
        const rad = (p.brg - 90) * Math.PI / 180;
        const px = cx + Math.cos(rad) * r;
        const py = cy + Math.sin(rad) * r;
        const dist = Math.hypot(px - x, py - y);
        if (dist < minDis) {
          minDis = dist;
          nearest = p;
        }
      });

      if (nearest) {
        selectAircraft(nearest);
      }
    };

    canvas.addEventListener("click", handleScopeClick);
    canvas.addEventListener("touchstart", handleScopeClick, { passive: true });
  }

  // -------------------------------------------------------------
  // Realtime Data Ingestion & Live Feeds
  // -------------------------------------------------------------
  async function fetchAircraft() {
    try {
      const data = await apiGet("/api/aircraft");
      const list = data.aircraft || [];
      currentPlanes = list.map(p => ({
        hex: p.hex || "",
        flight: (p.flight || "").trim() || p.hex || "UNK",
        alt: p.alt || 0,
        spd: Math.round(p.spd || 0),
        track: Math.round(p.track || 0),
        dst: Math.round((p.dst || 0) * 10) / 10,
        brg: Math.round(p.brg || 0),
        squawk: p.squawk || "----",
        type: p.type || "---",
        reg: p.reg || "",
        op: p.op || "",
        desc: p.desc || ""
      }));

      lastDataUpdateMs = Date.now();

      // Record Breadcrumb Trails
      currentPlanes.forEach(p => {
        if (!planeTrails.has(p.hex)) planeTrails.set(p.hex, []);
        const tr = planeTrails.get(p.hex);
        tr.push({ dst: p.dst, brg: p.brg });
        if (tr.length > 20) tr.shift();
      });

      // Update UI Counters & Airspace Snapshot
      const count = currentPlanes.length;
      document.getElementById("targetCount").textContent = count;
      document.getElementById("trafficBadge").textContent = count;

      if (data.rangeKm && !window.userManuallySelectedRange) {
        radarRangeKm = data.rangeKm;
        document.querySelectorAll(".range-btn").forEach(b => {
          b.classList.toggle("active", Number(b.dataset.range) === radarRangeKm);
        });
      }

      if (data.lat && data.lon) {
        mapCenter = { lat: Number(data.lat), lon: Number(data.lon) };
        const coordsEl = document.getElementById("scopeCoordsText");
        if (coordsEl) coordsEl.textContent = `${Number(data.lat).toFixed(4)}° N, ${Number(data.lon).toFixed(4)}° E`;
      }

      updateAirspaceStats();
      renderTrafficTable();

      // Refresh selected aircraft telemetry
      if (selectedPlane) {
        const found = currentPlanes.find(p => p.hex === selectedPlane.hex);
        if (found) selectAircraft(found);
      }
    } catch (e) {
      console.warn("Realtime fetch error:", e);
    }
  }

  function updateAirspaceStats() {
    const airborneCount = currentPlanes.filter(p => p.alt > 0).length;
    document.getElementById("statAirborne").textContent = airborneCount;

    if (currentPlanes.length > 0) {
      const maxAltPlane = currentPlanes.reduce((prev, cur) => (cur.alt > prev.alt ? cur : prev), currentPlanes[0]);
      document.getElementById("statMaxAlt").textContent = `${maxAltPlane.alt.toLocaleString()} ft (${maxAltPlane.flight})`;

      const maxSpdPlane = currentPlanes.reduce((prev, cur) => (cur.spd > prev.spd ? cur : prev), currentPlanes[0]);
      document.getElementById("statMaxSpeed").textContent = `${maxSpdPlane.spd} kt (${maxSpdPlane.flight})`;

      const closestPlane = currentPlanes.reduce((prev, cur) => (cur.dst < prev.dst ? cur : prev), currentPlanes[0]);
      document.getElementById("statClosest").textContent = `${closestPlane.dst} km (${closestPlane.flight})`;
    } else {
      document.getElementById("statMaxAlt").textContent = "-";
      document.getElementById("statMaxSpeed").textContent = "-";
      document.getElementById("statClosest").textContent = "-";
    }
  }

  function renderTrafficTable() {
    const tbody = document.getElementById("trafficTableBody");
    if (!tbody) return;

    const query = (document.getElementById("trafficSearchInput")?.value || "").toLowerCase().trim();
    const filtered = currentPlanes.filter(p => {
      if (!query) return true;
      return (
        p.flight.toLowerCase().includes(query) ||
        p.hex.toLowerCase().includes(query) ||
        p.type.toLowerCase().includes(query) ||
        p.op.toLowerCase().includes(query)
      );
    });

    if (filtered.length === 0) {
      tbody.innerHTML = `<tr><td colspan="10" class="empty-table-msg">${currentPlanes.length === 0 ? "Searching airspace..." : "No targets matching search filter."}</td></tr>`;
      return;
    }

    filtered.sort((a, b) => a.dst - b.dst);

    tbody.innerHTML = filtered.map(p => `
      <tr class="${selectedPlane && selectedPlane.hex === p.hex ? "selected-row" : ""}" data-hex="${p.hex}">
        <td class="font-bold font-mono">${p.flight}</td>
        <td class="font-mono text-dim">${p.hex}</td>
        <td>${p.type || "---"}</td>
        <td class="font-mono">${p.alt.toLocaleString()} ft</td>
        <td class="font-mono">${p.spd} kt</td>
        <td class="font-mono">${p.track}°</td>
        <td class="font-mono font-bold">${p.dst} km</td>
        <td class="font-mono">${p.brg}°</td>
        <td class="font-mono squawk-badge">${p.squawk}</td>
        <td>
          <button class="btn btn-secondary btn-sm track-row-btn" data-hex="${p.hex}">Inspect</button>
        </td>
      </tr>
    `).join("");

    tbody.querySelectorAll(".track-row-btn").forEach(btn => {
      btn.addEventListener("click", (e) => {
        e.stopPropagation();
        const hex = btn.dataset.hex;
        const target = currentPlanes.find(p => p.hex === hex);
        if (target) {
          selectAircraft(target);
          document.querySelector(".nav-tab[data-tab='radar']")?.click();
        }
      });
    });

    tbody.querySelectorAll("tr").forEach(tr => {
      tr.addEventListener("click", () => {
        const hex = tr.dataset.hex;
        const target = currentPlanes.find(p => p.hex === hex);
        if (target) selectAircraft(target);
      });
    });
  }

  function selectAircraft(p) {
    selectedPlane = p;
    document.getElementById("targetDetailEmpty").style.display = "none";
    document.getElementById("targetDetailContent").style.display = "block";
    document.getElementById("targetTag").textContent = p.flight || p.hex;

    document.getElementById("detailCallsign").textContent = p.flight;
    document.getElementById("detailHex").textContent = `HEX: ${p.hex}`;
    document.getElementById("detailAlt").innerHTML = `${p.alt.toLocaleString()} <small>ft</small>`;
    document.getElementById("detailSpeed").innerHTML = `${p.spd} <small>kt</small>`;
    document.getElementById("detailDist").innerHTML = `${p.dst} <small>km</small>`;
    document.getElementById("detailBrg").innerHTML = `${p.brg}&deg;`;
    document.getElementById("detailTrack").innerHTML = `${p.track}&deg;`;
    document.getElementById("detailSquawk").textContent = p.squawk || "----";

    document.getElementById("detailType").textContent = p.type || "Unknown / Not Broadcast";
    document.getElementById("detailReg").textContent = p.reg || "Unknown";
    document.getElementById("detailOp").textContent = p.op || "Unknown Operator";
    document.getElementById("detailDesc").textContent = p.desc || "ADS-B Transponder Active";

    const fr24Btn = document.getElementById("flightRadarTrackBtn");
    if (fr24Btn) {
      fr24Btn.href = `https://www.flightradar24.com/${encodeURIComponent(p.flight || p.hex)}`;
    }
  }

  // Search input live filtering
  document.getElementById("trafficSearchInput")?.addEventListener("input", renderTrafficTable);

  // -------------------------------------------------------------
  // Background Status Ingestion
  // -------------------------------------------------------------
  async function fetchStatus() {
    try {
      const s = await apiGet("/api/status");
      const statusPill = document.getElementById("statusPill");
      const statusText = document.getElementById("liveStatusText");
      const providerVal = document.getElementById("activeProvider");
      const wifiSsid = document.getElementById("wifiSsidHeader");
      const wifiIpHeader = document.getElementById("wifiIpHeader");
      const wifiStateEl = document.getElementById("wifiState");
      const wifiIpEl = document.getElementById("wifiIp");
      const mdnsAddrEl = document.getElementById("mdnsAddr");
      const wifiBars = document.getElementById("wifiBars");

      if (s.lastFetchOk) {
        statusPill.className = "hud-pill";
        statusText.textContent = "LIVE";
      } else if (s.fetchInProgress) {
        statusPill.className = "hud-pill";
        statusText.textContent = "SYNCING";
      } else {
        statusPill.className = "hud-pill offline";
        statusText.textContent = "SEARCHING";
      }

      if (providerVal) providerVal.textContent = s.lastProvider || "OpenSky";
      if (wifiSsid) wifiSsid.textContent = s.staIp ? "Connected" : "AP Mode";
      if (wifiIpHeader) wifiIpHeader.textContent = s.staIp ? `(${s.staIp})` : "";
      if (wifiStateEl) wifiStateEl.textContent = s.wifiState || "Connected";
      if (wifiIpEl) wifiIpEl.textContent = s.staIp || "-";
      if (mdnsAddrEl) mdnsAddrEl.textContent = s.mdns || "flyradar32.local";

      if (wifiBars) {
        wifiBars.className = s.wifiState === "connected" ? "wifi-bars good" : "wifi-bars";
      }

      // Update seconds ago in header
      const secAgo = Math.max(0, Math.round((Date.now() - lastDataUpdateMs) / 1000));
      const refreshText = document.getElementById("refreshTimerText");
      if (refreshText) {
        refreshText.textContent = secAgo <= 1 ? "LIVE" : `${secAgo}s AGO`;
      }
    } catch (e) {
      console.warn("Status fetch error:", e);
    }
  }

  // -------------------------------------------------------------
  // Universal Auto-Save Engine
  // -------------------------------------------------------------
  let autoSaveDisplayTimer = null;
  function triggerAutoSaveDisplay(params) {
    setSavingIndicator(true);
    clearTimeout(autoSaveDisplayTimer);
    autoSaveDisplayTimer = setTimeout(async () => {
      try {
        await apiPost("/api/settings/display", params);
        setSavingIndicator(false, "Display Settings Saved");
      } catch (e) {
        console.warn("Display save error:", e);
        setSavingIndicator(false, "Save Failed");
      }
    }, 250);
  }

  let autoSaveLocationTimer = null;
  function triggerAutoSaveLocation(lat, lon) {
    if (isNaN(lat) || isNaN(lon) || lat < -90 || lat > 90 || lon < -180 || lon > 180) return;
    setSavingIndicator(true);
    clearTimeout(autoSaveLocationTimer);
    autoSaveLocationTimer = setTimeout(async () => {
      try {
        await apiPost("/api/settings/location", { lat, lon });
        setSavingIndicator(false, "GPS Coordinates Saved");
        const coordsEl = document.getElementById("scopeCoordsText");
        if (coordsEl) coordsEl.textContent = `${lat.toFixed(4)}° N, ${lon.toFixed(4)}° E`;
        setTimeout(fetchAircraft, 500);
      } catch (e) {
        console.warn("Location save error:", e);
        setSavingIndicator(false, "Save Failed");
      }
    }, 600);
  }

  async function triggerAutoSaveProviders(refreshInterval) {
    setSavingIndicator(true);
    try {
      await apiPost("/api/settings/providers", { refreshInterval });
      setSavingIndicator(false, "Interval Saved");
    } catch (e) {
      console.warn("Interval save error:", e);
      setSavingIndicator(false, "Save Failed");
    }
  }

  // -------------------------------------------------------------
  // Load Settings & Bind Real-Time Auto-Save Listeners
  // -------------------------------------------------------------
  async function loadSettings() {
    try {
      const s = await apiGet("/api/settings");
      const latInput = document.getElementById("latInput");
      const lonInput = document.getElementById("lonInput");
      const zoomSelect = document.getElementById("zoomSelect");
      const labelsSelect = document.getElementById("labelsSelect");
      const iconSelect = document.getElementById("iconSelect");
      const sweepToggle = document.getElementById("sweepToggle");
      const compassToggle = document.getElementById("compassToggle");
      const rangeLabelsToggle = document.getElementById("rangeLabelsToggle");
      const trailToggle = document.getElementById("trailToggle");
      const refreshSelect = document.getElementById("refreshSelect");
      const brightnessSlider = document.getElementById("brightnessSlider");
      const brightVal = document.getElementById("brightVal");

      if (latInput) latInput.value = s.lat;
      if (lonInput) lonInput.value = s.lon;
      if (zoomSelect) zoomSelect.value = s.zoomLevel;
      if (labelsSelect) labelsSelect.value = s.labelsMode;
      if (iconSelect) iconSelect.value = s.aircraftIcon;
      if (sweepToggle) sweepToggle.checked = s.showSweepAnim;
      if (compassToggle) compassToggle.checked = s.showCompass;
      if (rangeLabelsToggle) rangeLabelsToggle.checked = s.showRangeLabels;
      if (trailToggle) trailToggle.checked = s.showTrail;
      if (refreshSelect) refreshSelect.value = s.refreshInterval;

      if (brightnessSlider) {
        brightnessSlider.value = s.brightness || 255;
        if (brightVal) brightVal.textContent = `${Math.round((s.brightness || 255) / 2.55)}%`;
      }

      // Sync Active Theme Swatch
      activeTheme = s.theme || 0;
      applyTheme(activeTheme);
    } catch (e) {
      console.warn("Settings load error:", e);
    }
  }

  function applyTheme(themeIdx) {
    document.body.classList.remove("theme-green", "theme-cyan", "theme-amber");
    if (themeIdx === 1) document.body.classList.add("theme-cyan");
    else if (themeIdx === 2) document.body.classList.add("theme-amber");
    else document.body.classList.add("theme-green");

    document.querySelectorAll(".theme-swatch").forEach(s => {
      s.classList.toggle("active", Number(s.dataset.theme) === themeIdx);
    });
  }

  // Theme swatches instant auto-save
  document.querySelectorAll(".theme-swatch").forEach(s => {
    s.addEventListener("click", () => {
      activeTheme = Number(s.dataset.theme);
      applyTheme(activeTheme);
      triggerAutoSaveDisplay({ theme: activeTheme });
    });
  });

  // Range quick selector buttons on radar toolbar
  document.querySelectorAll(".range-btn").forEach(btn => {
    btn.addEventListener("click", () => {
      window.userManuallySelectedRange = true;
      document.querySelectorAll(".range-btn").forEach(b => b.classList.remove("active"));
      btn.classList.add("active");
      radarRangeKm = Number(btn.dataset.range);
    });
  });

  // Map View Toggle
  const mapToggleBtn = document.getElementById("mapToggleBtn");
  if (mapToggleBtn) {
    mapToggleBtn.addEventListener("click", () => {
      mapTilesEnabled = !mapTilesEnabled;
      mapToggleBtn.classList.toggle("active", mapTilesEnabled);
      if (mapTilesEnabled) showToast("Map overlay enabled");
    });
  }

  // Trail and Vector HUD toggle buttons
  const trailToggleBtn = document.getElementById("trailToggleBtn");
  if (trailToggleBtn) {
    trailToggleBtn.addEventListener("click", () => {
      showTrails = !showTrails;
      trailToggleBtn.classList.toggle("active", showTrails);
    });
  }

  const vectorToggleBtn = document.getElementById("vectorToggleBtn");
  if (vectorToggleBtn) {
    vectorToggleBtn.addEventListener("click", () => {
      showVectors = !showVectors;
      vectorToggleBtn.classList.toggle("active", showVectors);
    });
  }

  // Brightness Slider auto-save
  const brightnessSlider = document.getElementById("brightnessSlider");
  const brightVal = document.getElementById("brightVal");
  if (brightnessSlider) {
    brightnessSlider.addEventListener("input", () => {
      const val = parseInt(brightnessSlider.value, 10);
      if (brightVal) brightVal.textContent = `${Math.round(val / 2.55)}%`;
      triggerAutoSaveDisplay({ brightness: val });
    });
  }

  // Dropdowns auto-save
  document.getElementById("zoomSelect")?.addEventListener("change", (e) => {
    triggerAutoSaveDisplay({ zoomLevel: parseInt(e.target.value, 10) });
  });

  document.getElementById("labelsSelect")?.addEventListener("change", (e) => {
    triggerAutoSaveDisplay({ labelsMode: parseInt(e.target.value, 10) });
  });

  document.getElementById("iconSelect")?.addEventListener("change", (e) => {
    triggerAutoSaveDisplay({ aircraftIcon: parseInt(e.target.value, 10) });
  });

  // Toggles auto-save
  document.getElementById("compassToggle")?.addEventListener("change", (e) => {
    triggerAutoSaveDisplay({ showCompass: e.target.checked });
  });

  document.getElementById("rangeLabelsToggle")?.addEventListener("change", (e) => {
    triggerAutoSaveDisplay({ showRangeLabels: e.target.checked });
  });

  document.getElementById("trailToggle")?.addEventListener("change", (e) => {
    triggerAutoSaveDisplay({ showTrail: e.target.checked });
  });

  document.getElementById("sweepToggle")?.addEventListener("change", (e) => {
    triggerAutoSaveDisplay({ showSweepAnim: e.target.checked });
  });

  // Refresh Interval auto-save
  document.getElementById("refreshSelect")?.addEventListener("change", (e) => {
    triggerAutoSaveProviders(parseInt(e.target.value, 10));
  });

  // Latitude / Longitude auto-save
  const latInput = document.getElementById("latInput");
  const lonInput = document.getElementById("lonInput");

  function onCoordsChanged() {
    const lat = parseFloat(latInput?.value);
    const lon = parseFloat(lonInput?.value);
    if (!isNaN(lat) && !isNaN(lon)) {
      triggerAutoSaveLocation(lat, lon);
    }
  }

  latInput?.addEventListener("input", onCoordsChanged);
  lonInput?.addEventListener("input", onCoordsChanged);

  // Google Maps Coordinates Parser & Auto-Save
  const parseCoordsBtn = document.getElementById("parseCoordsBtn");
  if (parseCoordsBtn) {
    parseCoordsBtn.addEventListener("click", () => {
      const val = document.getElementById("pasteCoords").value.trim();
      const match = val.match(/(-?\d+\.\d+)[,\s]+(-?\d+\.\d+)/);
      if (match) {
        const lat = parseFloat(match[1]);
        const lon = parseFloat(match[2]);
        if (latInput) latInput.value = lat;
        if (lonInput) lonInput.value = lon;
        triggerAutoSaveLocation(lat, lon);
        showToast(`Parsed & saved coordinates: ${lat}, ${lon}`);
      } else {
        showToast("Invalid format. Expected: latitude, longitude", true);
      }
    });
  }

  // Danger Zone Actions
  document.getElementById("disconnectBtn")?.addEventListener("click", async () => {
    if (confirm("Forget Wi-Fi credentials and restart in AP setup mode?")) {
      showToast("Restarting in setup mode... Connect to 'FlyRadar32-Setup' Wi-Fi in ~10s.");
      try {
        await apiPost("/api/wifi/clear", {});
      } catch (e) {
        console.log("Device rebooted into setup mode:", e);
      }
    }
  });

  document.getElementById("resetBtn")?.addEventListener("click", async () => {
    if (confirm("Restore factory defaults? All settings and calibration will be reset to factory fresh state.")) {
      showToast("Hardware restoring factory defaults... Please wait.");
      try {
        await apiPost("/api/factory-reset", {});
      } catch (e) {
        console.log("Device restored to factory defaults:", e);
      }
    }
  });

  // -------------------------------------------------------------
  // Wi-Fi Scan & Connect Handlers
  // -------------------------------------------------------------
  const scanBtn = document.getElementById("scanBtn");
  const connectBtn = document.getElementById("connectBtn");
  const ssidInput = document.getElementById("ssidInput");
  const passInput = document.getElementById("passInput");
  const scanCard = document.getElementById("scanCard");
  const netList = document.getElementById("netList");
  const scanCount = document.getElementById("scanCount");

  if (scanBtn) {
    scanBtn.addEventListener("click", async () => {
      scanBtn.disabled = true;
      scanBtn.textContent = "Scanning...";
      if (scanCard) scanCard.style.display = "none";
      if (netList) netList.innerHTML = "";
      try {
        const res = await apiGet("/api/scan");
        const nets = Array.isArray(res) ? res : [];
        if (scanCount) scanCount.textContent = `(${nets.length})`;
        if (nets.length === 0) {
          if (netList) netList.innerHTML = "<p class='text-dim' style='padding:8px'>No networks found.</p>";
        } else {
          netList.innerHTML = nets.sort((a, b) => b.rssi - a.rssi).map(n => `
            <div class="net-item" data-ssid="${n.ssid}">
              <span class="net-name">${n.ssid}</span>
              <span class="net-rssi">${n.rssi} dBm ${n.secure ? '🔒' : ''}</span>
            </div>`).join("");
          netList.querySelectorAll(".net-item").forEach(item => {
            item.addEventListener("click", () => {
              if (ssidInput) ssidInput.value = item.dataset.ssid;
              if (passInput) passInput.focus();
              if (scanCard) scanCard.style.display = "none";
            });
          });
        }
        if (scanCard) scanCard.style.display = "block";
      } catch (e) {
        showToast("Wi-Fi scan failed: " + e.message, true);
      } finally {
        scanBtn.disabled = false;
        scanBtn.textContent = "Scan";
      }
    });
  }

  if (connectBtn) {
    connectBtn.addEventListener("click", async () => {
      const ssid = ssidInput?.value.trim();
      const pass = passInput?.value;
      if (!ssid) { showToast("Enter an SSID first", true); return; }
      connectBtn.disabled = true;
      connectBtn.textContent = "Connecting...";
      try {
        await apiPost("/api/wifi/connect", { ssid, password: pass });
        showToast("Wi-Fi credentials saved. Device reconnecting...");
      } catch (e) {
        showToast("Connect failed: " + e.message, true);
      } finally {
        connectBtn.disabled = false;
        connectBtn.textContent = "Connect Wi-Fi";
      }
    });
  }

  // -------------------------------------------------------------
  // Map Tile Overlay (OpenStreetMap behind radar scope)
  // -------------------------------------------------------------
  function latLonToTile(lat, lon, zoom) {
    const n = Math.pow(2, zoom);
    const x = Math.floor((lon + 180) / 360 * n);
    const latRad = lat * Math.PI / 180;
    const y = Math.floor((1 - Math.log(Math.tan(latRad) + 1 / Math.cos(latRad)) / Math.PI) / 2 * n);
    return { x, y, z: zoom };
  }

  function tileToLatLon(tx, ty, zoom) {
    const n = Math.pow(2, zoom);
    const lon = tx / n * 360 - 180;
    const latRad = Math.atan(Math.sinh(Math.PI * (1 - 2 * ty / n)));
    return { lat: latRad * 180 / Math.PI, lon };
  }

  function drawMapOverlay(ctx, cx, cy, maxR, lat, lon, rangeKm) {
    if (!mapTilesEnabled) return;

    // Calculate zoom level from range
    // rangeKm => zoom: 50=>10, 100=>9, 150=>8
    const zoom = rangeKm <= 50 ? 10 : rangeKm <= 100 ? 9 : 8;
    mapZoom = zoom;
    mapCenter = { lat, lon };

    const degPerKm = 1 / 111.0;
    const rangeLatDeg = rangeKm * degPerKm;
    const rangeLonDeg = rangeKm * degPerKm / Math.cos(lat * Math.PI / 180);

    // Tile size in pixels on the map (256px/tile at zoom)
    const TILE_PX = 256;
    // Degrees per tile at this zoom
    const n = Math.pow(2, zoom);
    const degPerTileX = 360 / n;
    const latRad = lat * Math.PI / 180;
    const degPerTileY = (Math.atan(Math.sinh(Math.PI * (1 - 2 * (Math.floor((1 - Math.log(Math.tan(latRad) + 1/Math.cos(latRad))/Math.PI)/2*n))/n))) -
                        Math.atan(Math.sinh(Math.PI * (1 - 2 * (Math.floor((1 - Math.log(Math.tan(latRad) + 1/Math.cos(latRad))/Math.PI)/2*n) + 1)/n)))) * 180 / Math.PI;

    // Pixels per degree at our canvas scale (maxR covers rangeLatDeg degrees)
    const pxPerDegLat = maxR / rangeLatDeg;
    const pxPerDegLon = maxR / rangeLonDeg;

    // Center tile
    const ct = latLonToTile(lat, lon, zoom);
    const ctLatLon = tileToLatLon(ct.x, ct.y, zoom);
    const ctLatLonEnd = tileToLatLon(ct.x + 1, ct.y + 1, zoom);
    const tileDegH = ctLatLon.lat - ctLatLonEnd.lat; // tile height in degrees lat
    const tileDegW = ctLatLonEnd.lon - ctLatLon.lon; // tile width in degrees lon

    // How big is one tile in canvas pixels?
    const tilePxH = tileDegH * pxPerDegLat;
    const tilePxW = tileDegW * pxPerDegLon;

    // How many tiles around center to load?
    const tilesX = Math.ceil(maxR / tilePxW) + 2;
    const tilesY = Math.ceil(maxR / tilePxH) + 2;

    ctx.save();
    ctx.beginPath();
    ctx.arc(cx, cy, maxR, 0, Math.PI * 2);
    ctx.clip();
    ctx.globalAlpha = 0.25;

    for (let dy = -tilesY; dy <= tilesY; dy++) {
      for (let dx = -tilesX; dx <= tilesX; dx++) {
        const tx = ct.x + dx;
        const ty = ct.y + dy;
        const tll = tileToLatLon(tx, ty, zoom);
        // Canvas position of tile top-left corner
        const canX = cx + (tll.lon - lon) * pxPerDegLon;
        const canY = cy - (tll.lat - lat) * pxPerDegLat;

        const url = `https://tile.openstreetmap.org/${zoom}/${tx}/${ty}.png`;
        if (mapTileCache[url] === undefined) {
          mapTileCache[url] = 'loading';
          const img = new Image();
          img.crossOrigin = 'anonymous';
          img.onload = () => { mapTileCache[url] = img; };
          img.onerror = () => { mapTileCache[url] = null; };
          img.src = url;
        } else if (mapTileCache[url] && mapTileCache[url] !== 'loading') {
          ctx.drawImage(mapTileCache[url], canX, canY, tilePxW, tilePxH);
        }
      }
    }

    ctx.globalAlpha = 1;
    ctx.restore();
  }

  // -------------------------------------------------------------
  // Initialization & Continuous Realtime Loops
  // -------------------------------------------------------------
  loadSettings();
  fetchAircraft();
  fetchStatus();

  // Start continuous 60fps radar sweep
  requestAnimationFrame(renderRadar);

  // Automated Realtime Data Fetch (Every 2.5 seconds)
  setInterval(fetchAircraft, 2500);

  // Background Status Refresh (Every 3.5 seconds)
  setInterval(fetchStatus, 3500);

})();
