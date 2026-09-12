(() => {
  "use strict";

  let currentPlanes = [];
  let selectedPlane = null;
  let radarRangeKm = 100;
  let showTrails = true;
  let showVectors = true;
  let showSweepAnim = true;
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
  // API Helpers
  // -------------------------------------------------------------
  async function apiGet(url) {
    const res = await fetch(url);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return res.json();
  }

  async function apiPost(url, body) {
    const res = await fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body || {})
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return res.json();
  }

  // HTML-escape anything rendered into innerHTML (callsigns, SSIDs, aircraft
  // fields) so hostile data can't inject script into the page.
  function esc(s) {
    return String(s ?? "").replace(/[&<>"']/g, c => ({
      "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"
    }[c]));
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
    0: { // Matrix Green (Pure Vibrant Phosphor Green)
      bg: "#02070d",
      grid: "rgba(0, 255, 0, 0.30)",
      gridSub: "rgba(0, 255, 0, 0.12)",
      accent: "#00FF00",
      beam: "#99FF99",
      glow: "rgba(0, 255, 0, 0.40)",
      text: "#E0FFE0"
    },
    1: { // Ice Cyan
      bg: "#020812",
      grid: "rgba(0, 229, 255, 0.28)",
      gridSub: "rgba(0, 229, 255, 0.12)",
      accent: "#00E5FF",
      beam: "#88F5FF",
      glow: "rgba(0, 229, 255, 0.38)",
      text: "#E0F7FA"
    },
    2: { // Amber Retro
      bg: "#0a0602",
      grid: "rgba(255, 179, 0, 0.28)",
      gridSub: "rgba(255, 179, 0, 0.12)",
      accent: "#FFB300",
      beam: "#FFE088",
      glow: "rgba(255, 179, 0, 0.40)",
      text: "#FFF3E0"
    }
  };

  // -------------------------------------------------------------
  // Cockpit Navigation Tabs (3 Tabs)
  // -------------------------------------------------------------
  let activeCockpitTab = "radar";
  document.querySelectorAll(".nav-tab").forEach(tab => {
    tab.addEventListener("click", () => {
      const targetTab = tab.dataset.tab;
      activeCockpitTab = targetTab;
      document.querySelectorAll(".nav-tab").forEach(t => t.classList.remove("active"));
      document.querySelectorAll(".cockpit-panel").forEach(p => p.classList.remove("active"));

      tab.classList.add("active");
      const targetPanel = document.getElementById(`panel-${targetTab}`);
      if (targetPanel) targetPanel.classList.add("active");

      // Resize radar canvas if entering radar view
      if (targetTab === "radar") {
        resizeRadarCanvas();
      } else if (targetTab === "traffic") {
        renderTrafficTable();
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
    try {
      if (!ctx || !canvas) return;

      const w = canvas.width;
      const h = canvas.height;
      if (w === 0 || h === 0) return;

      const cx = w / 2;
      const cy = h / 2;
      const maxR = Math.min(cx, cy) * 0.90; // scope radius leaving outer margin for compass labels

      const colors = THEME_PALETTES[activeTheme] || THEME_PALETTES[0];

      // Clear Canvas
      ctx.clearRect(0, 0, w, h);

      // Deep radar scope circle with sleek radial glass depth
      const scopeGrad = ctx.createRadialGradient(cx, cy, 0, cx, cy, maxR);
      scopeGrad.addColorStop(0, "#081624");
      scopeGrad.addColorStop(0.55, "#040e17");
      scopeGrad.addColorStop(1, "#020508");
      ctx.fillStyle = scopeGrad;
      ctx.beginPath();
      ctx.arc(cx, cy, maxR, 0, Math.PI * 2);
      ctx.fill();

      // Outer boundary glowing rim
      ctx.save();
      ctx.strokeStyle = colors.accent;
      ctx.lineWidth = 1.8;
      ctx.shadowColor = colors.accent;
      ctx.shadowBlur = 8;
      ctx.beginPath();
      ctx.arc(cx, cy, maxR, 0, Math.PI * 2);
      ctx.stroke();
      ctx.restore();

      // Map tile overlay (optional, underneath all radar elements)
      drawMapOverlay(ctx, cx, cy, maxR, mapCenter.lat, mapCenter.lon, radarRangeKm);

      // Concentric Range Rings (3 rings: 33%, 66%, 100%)
      [0.333, 0.666, 1.0].forEach((ratio, idx) => {
        const r = maxR * ratio;
        ctx.save();
        ctx.lineWidth = (idx === 2) ? 1.8 : 1.2;
        ctx.strokeStyle = (idx === 2) ? colors.accent : colors.grid;
        if (idx === 2) {
          ctx.shadowColor = colors.accent;
          ctx.shadowBlur = 6;
        }
        ctx.beginPath();
        ctx.arc(cx, cy, r, 0, Math.PI * 2);
        ctx.stroke();
        ctx.restore();

        // Range text badge on vertical axis with subtle dark backing for crisp readability
        const dist = Math.round(radarRangeKm * ratio);
        const textStr = `${dist}km`;
        const fontSize = Math.max(9, Math.round(w * 0.021));
        ctx.font = `600 ${fontSize}px 'JetBrains Mono', monospace`;
        const txtMetrics = ctx.measureText(textStr);
        const tx = cx + 6;
        const ty = cy - r + 12;

        ctx.fillStyle = "rgba(4, 10, 18, 0.75)";
        ctx.fillRect(tx - 2, ty - fontSize + 2, txtMetrics.width + 4, fontSize + 2);

        ctx.fillStyle = colors.accent;
        ctx.textAlign = "left";
        ctx.fillText(textStr, tx, ty);
      });

      // Crosshair Lines
      ctx.save();
      ctx.strokeStyle = colors.gridSub;
      ctx.lineWidth = 1;
      ctx.setLineDash([4, 4]);
      ctx.beginPath();
      ctx.moveTo(cx - maxR, cy);
      ctx.lineTo(cx + maxR, cy);
      ctx.moveTo(cx, cy - maxR);
      ctx.lineTo(cx, cy + maxR);
      ctx.stroke();
      ctx.restore();

      // Compass Radial Ticks & Degree Labels
      for (let deg = 0; deg < 360; deg += 30) {
        const rad = (deg - 90) * Math.PI / 180;
        const x1 = cx + Math.cos(rad) * (maxR - 6);
        const y1 = cy + Math.sin(rad) * (maxR - 6);
        const x2 = cx + Math.cos(rad) * maxR;
        const y2 = cy + Math.sin(rad) * maxR;

        ctx.strokeStyle = colors.grid;
        ctx.lineWidth = 1.2;
        ctx.beginPath();
        ctx.moveTo(x1, y1);
        ctx.lineTo(x2, y2);
        ctx.stroke();

        const tx = cx + Math.cos(rad) * (maxR + 15);
        const ty = cy + Math.sin(rad) * (maxR + 15) + 4;
        ctx.font = `bold ${Math.max(10, Math.round(w * 0.023))}px 'JetBrains Mono', monospace`;
        ctx.fillStyle = colors.accent;
        ctx.textAlign = "center";
        let lbl = `${deg}°`;
        if (deg === 0) lbl = "N";
        else if (deg === 90) lbl = "E";
        else if (deg === 180) lbl = "S";
        else if (deg === 270) lbl = "W";
        ctx.fillText(lbl, tx, ty);
      }

      // Smooth scanning pulse and HUD text when no targets in range
      if (currentPlanes.length === 0) {
        const scanPhase = (Date.now() % 2400) / 2400;
        const pulseR = 8 + (maxR - 8) * scanPhase;
        const pulseAlpha = Math.max(0, 1 - scanPhase) * 0.45;

        ctx.save();
        ctx.strokeStyle = colors.accent;
        ctx.globalAlpha = pulseAlpha;
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.arc(cx, cy, pulseR, 0, Math.PI * 2);
        ctx.stroke();

        ctx.globalAlpha = 0.85;
        ctx.fillStyle = colors.accent;
        ctx.font = `600 ${Math.max(10, Math.round(w * 0.022))}px 'JetBrains Mono', monospace`;
        ctx.textAlign = "center";
        ctx.fillText("SCANNING AIRSPACE...", cx, cy + maxR * 0.42);
        ctx.restore();
      }

      // Rotating Radar Sweep Beam with Authentic CRT Phosphor Fade Trail
      if (showSweepAnim) {
        const sweepRad = (sweepAngle - 90) * Math.PI / 180;
        const trailSpan = (48 * Math.PI) / 180; // ~48° glowing wake
        const tailRad = sweepRad - trailSpan;

        if (typeof ctx.createConicGradient === "function") {
          try {
            const sweepGrad = ctx.createConicGradient(tailRad, cx, cy);
            const spanRatio = trailSpan / (Math.PI * 2);

            sweepGrad.addColorStop(0, "transparent");
            sweepGrad.addColorStop(Math.min(1, spanRatio * 0.20), "rgba(0, 255, 0, 0.02)");
            sweepGrad.addColorStop(Math.min(1, spanRatio * 0.50), "rgba(0, 255, 0, 0.09)");
            sweepGrad.addColorStop(Math.min(1, spanRatio * 0.80), "rgba(0, 255, 0, 0.22)");
            sweepGrad.addColorStop(Math.min(1, spanRatio), "rgba(0, 255, 0, 0.40)");
            sweepGrad.addColorStop(Math.min(1, spanRatio + 0.002), "transparent");
            sweepGrad.addColorStop(1, "transparent");

            ctx.save();
            ctx.beginPath();
            ctx.moveTo(cx, cy);
            ctx.arc(cx, cy, maxR, tailRad, sweepRad, false);
            ctx.closePath();
            ctx.fillStyle = sweepGrad;
            ctx.fill();
            ctx.restore();
          } catch (gradErr) {
            // fallback gracefully
          }
        }

        // Sharp luminous glowing leading sweep line
        ctx.save();
        ctx.strokeStyle = colors.beam || "#99FF99";
        ctx.lineWidth = 2.0;
        ctx.shadowColor = colors.accent;
        ctx.shadowBlur = 10;
        ctx.beginPath();
        ctx.moveTo(cx, cy);
        ctx.lineTo(cx + Math.cos(sweepRad) * maxR, cy + Math.sin(sweepRad) * maxR);
        ctx.stroke();
        ctx.restore();

        // Advance sweep angle clockwise
        sweepAngle = (sweepAngle + 1.2) % 360;
      }

      // Central Ground Station Emitter with subtle radar ping
      const pingR = 4 + Math.sin(Date.now() / 250) * 2;
      ctx.save();
      ctx.strokeStyle = colors.accent;
      ctx.lineWidth = 1.2;
      ctx.beginPath();
      ctx.arc(cx, cy, pingR + 4, 0, Math.PI * 2);
      ctx.stroke();

      ctx.fillStyle = colors.accent;
      ctx.beginPath();
      ctx.arc(cx, cy, 3, 0, Math.PI * 2);
      ctx.fill();
      ctx.restore();

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

        const isSelected = selectedPlane && selectedPlane.hex === p.hex;
        const color = getAltColor(p.alt, p.gnd);

        // Heading Vector line
        if (showVectors && p.spd > 20) {
          const vecLen = Math.min(35, (p.spd / 500) * 35);
          const trkRad = (p.track - 90) * Math.PI / 180;
          ctx.strokeStyle = color;
          ctx.lineWidth = 1.2;
          ctx.beginPath();
          ctx.moveTo(px, py);
          ctx.lineTo(px + Math.cos(trkRad) * vecLen, py + Math.sin(trkRad) * vecLen);
          ctx.stroke();
        }

        // Draw Authentic Silhouette
        drawAircraftGlyph(ctx, px, py, p.track, color);

        // Target Selection Ring
        if (isSelected) {
          ctx.save();
          ctx.strokeStyle = "#FFFFFF";
          ctx.lineWidth = 1.8;
          ctx.shadowColor = "#FFFFFF";
          ctx.shadowBlur = 6;
          ctx.beginPath();
          ctx.arc(px, py, 10, 0, Math.PI * 2);
          ctx.stroke();
          ctx.restore();

          // Reticle Stems
          ctx.strokeStyle = "rgba(255, 255, 255, 0.4)";
          ctx.lineWidth = 1;
          ctx.setLineDash([2, 2]);
          ctx.beginPath();
          ctx.moveTo(px, py - 14); ctx.lineTo(px, py + 14);
          ctx.moveTo(px - 14, py); ctx.lineTo(px + 14, py);
          ctx.stroke();
          ctx.setLineDash([]);
        }

        // Callsign Tag
        ctx.font = `bold ${Math.max(9, Math.round(w * 0.02))}px 'JetBrains Mono', monospace`;
        ctx.fillStyle = "#FFFFFF";
        ctx.textAlign = "left";
        ctx.fillText(p.flight || p.hex, px + 9, py - 4);
      });
    } catch (err) {
      console.error("renderRadar error:", err);
    } finally {
      requestAnimationFrame(renderRadar);
    }
  }

  function getAltColor(alt, gnd) {
    if (gnd) return "#00E5FF";
    if (alt < 10000) return "#FF453A";
    if (alt < 28000) return "#FFB300";
    return "#00FF00";
  }

  function drawAircraftGlyph(ctx, x, y, trackDeg, color) {
    ctx.save();
    ctx.translate(x, y);
    ctx.rotate(trackDeg * Math.PI / 180);
    ctx.fillStyle = color;
    ctx.strokeStyle = color;
    ctx.shadowColor = color;
    ctx.shadowBlur = 6;

    // Authentic FlightRadar24 commercial jet airliner silhouette
    ctx.beginPath();
    ctx.moveTo(0, -10);                           // Nose
    ctx.bezierCurveTo(1.5, -7, 2, -3, 2, 0);       // Right fuselage
    ctx.lineTo(10, 3);                            // Right wingtip
    ctx.lineTo(10, 4.5);
    ctx.lineTo(2.5, 2.5);                         // Right wing trailing edge
    ctx.lineTo(2, 6);                             // Rear fuselage right
    ctx.lineTo(5.5, 8.5);                         // Right tailtip
    ctx.lineTo(5.5, 9.5);
    ctx.lineTo(0.8, 8);                           // Tail center right
    ctx.lineTo(0, 10);                            // Tail cone
    ctx.lineTo(-0.8, 8);                          // Tail center left
    ctx.lineTo(-5.5, 9.5);
    ctx.lineTo(-5.5, 8.5);                        // Left tailtip
    ctx.lineTo(-2, 6);                            // Rear fuselage left
    ctx.lineTo(-2.5, 2.5);                        // Left wing trailing edge
    ctx.lineTo(-10, 4.5);
    ctx.lineTo(-10, 3);                           // Left wingtip
    ctx.lineTo(-2, 0);                            // Left fuselage
    ctx.bezierCurveTo(-2, -3, -1.5, -7, 0, -10);   // Left nose
    ctx.closePath();
    ctx.fill();

    // Twin jet engine nacelles under main wings
    ctx.beginPath();
    ctx.arc(3.5, 2, 1, 0, Math.PI * 2);
    ctx.arc(-3.5, 2, 1, 0, Math.PI * 2);
    ctx.fill();

    // White center beacon (transponder position fix accuracy)
    ctx.fillStyle = "#FFFFFF";
    ctx.beginPath();
    ctx.arc(0, 0, 1.2, 0, Math.PI * 2);
    ctx.fill();

    ctx.restore();
  }

  // Radar Scope Click Target Detection & Zooming
  if (canvas) {
    const handleScopeClick = (e) => {
      if (e.touches && e.touches.length > 1) return; // Ignore pinch multi-touch
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

    // Mouse Wheel Zoom (strictly zoom in/out with locked fixed station center)
    canvas.addEventListener("wheel", (e) => {
      e.preventDefault();
      const step = radarRangeKm > 100 ? 25 : (radarRangeKm > 50 ? 15 : 10);
      if (e.deltaY < 0) {
        setRadarRange(radarRangeKm - step);
      } else {
        setRadarRange(radarRangeKm + step);
      }
    }, { passive: false });

    // Touch Pinch-to-Zoom (Fixed center, mobile responsive)
    let lastTouchDist = 0;
    canvas.addEventListener("touchstart", (e) => {
      if (e.touches.length === 2) {
        e.preventDefault();
        lastTouchDist = Math.hypot(
          e.touches[0].clientX - e.touches[1].clientX,
          e.touches[0].clientY - e.touches[1].clientY
        );
      }
    }, { passive: false });

    canvas.addEventListener("touchmove", (e) => {
      if (e.touches.length === 2) {
        e.preventDefault();
        const dist = Math.hypot(
          e.touches[0].clientX - e.touches[1].clientX,
          e.touches[0].clientY - e.touches[1].clientY
        );
        if (lastTouchDist > 0) {
          const diff = dist - lastTouchDist;
          if (Math.abs(diff) > 8) {
            if (diff > 0) {
              // Pinch spread: Zoom in (decrease range)
              setRadarRange(radarRangeKm - (radarRangeKm > 60 ? 10 : 5));
            } else {
              // Pinch in: Zoom out (increase range)
              setRadarRange(radarRangeKm + (radarRangeKm > 60 ? 10 : 5));
            }
            lastTouchDist = dist;
          }
        }
      }
    }, { passive: false });

    canvas.addEventListener("touchend", (e) => {
      if (e.touches.length < 2) {
        lastTouchDist = 0;
      }
    });
  }

  // -------------------------------------------------------------
  // Realtime Data Ingestion & Live Feeds
  // -------------------------------------------------------------
  let isFetchingAircraft = false;
  let isFetchingStatus = false;
  let isPageVisible = !document.hidden;

  document.addEventListener("visibilitychange", () => {
    isPageVisible = !document.hidden;
    if (isPageVisible) {
      fetchAircraft();
      fetchStatus();
    }
  });

  async function fetchAircraft() {
    if (!isPageVisible || isFetchingAircraft) return;
    isFetchingAircraft = true;
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

      // Record Breadcrumb Trails + prune entries for aircraft no longer
      // in the feed so the Map can't grow unbounded over long sessions.
      const liveHexes = new Set(currentPlanes.map(p => p.hex));
      for (const hex of planeTrails.keys()) {
        if (!liveHexes.has(hex)) planeTrails.delete(hex);
      }
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
        setRadarRange(data.rangeKm, false);
      }

      if (data.lat && data.lon) {
        mapCenter = { lat: Number(data.lat), lon: Number(data.lon) };
        const coordsEl = document.getElementById("scopeCoordsText");
        if (coordsEl) coordsEl.textContent = `${Number(data.lat).toFixed(4)}° N, ${Number(data.lon).toFixed(4)}° E`;
      }

      updateAirspaceStats();
      if (activeCockpitTab === "traffic") {
        renderTrafficTable();
      }

      // Refresh selected aircraft telemetry
      if (selectedPlane) {
        const found = currentPlanes.find(p => p.hex === selectedPlane.hex);
        if (found) selectAircraft(found);
      }
    } catch (e) {
      console.warn("Realtime fetch error:", e);
    } finally {
      isFetchingAircraft = false;
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
      <tr class="${selectedPlane && selectedPlane.hex === p.hex ? "selected-row" : ""}" data-hex="${esc(p.hex)}">
        <td class="font-bold font-mono">${esc(p.flight)}</td>
        <td class="font-mono text-dim">${esc(p.hex)}</td>
        <td>${esc(p.type || "---")}</td>
        <td class="font-mono">${p.alt.toLocaleString()} ft</td>
        <td class="font-mono">${p.spd} kt</td>
        <td class="font-mono">${p.track}&deg;</td>
        <td class="font-mono font-bold">${p.dst} km</td>
        <td class="font-mono">${p.brg}&deg;</td>
        <td class="font-mono squawk-badge">${esc(p.squawk)}</td>
        <td>
          <button class="btn btn-secondary btn-sm track-row-btn" data-hex="${esc(p.hex)}">Inspect</button>
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
    if (!isPageVisible || isFetchingStatus) return;
    isFetchingStatus = true;
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
      const clockEl = document.getElementById("stationClock");
      if (clockEl) clockEl.textContent = s.time || "";

      // Weather strip (compact: temp + wind)
      const wxEl = document.getElementById("wxText");
      if (wxEl) {
        if (s.tempC !== undefined) {
          const dirs = ["N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"];
          const d = dirs[Math.round(((s.windDeg % 360) / 22.5)) % 16] || "N";
          wxEl.textContent = `${Math.round(s.tempC)}\u00B0C ${d} ${Math.round(s.windKt)}kt`;
        } else {
          wxEl.textContent = "--";
        }
      }
      if (wifiSsid) wifiSsid.textContent = s.staIp ? "Connected" : "AP Mode";
      if (wifiIpHeader) wifiIpHeader.textContent = s.staIp ? `(${s.staIp})` : "";
      if (wifiStateEl) wifiStateEl.textContent = s.wifiState || "Connected";
      if (wifiIpEl) wifiIpEl.textContent = s.staIp || "-";
      if (mdnsAddrEl) mdnsAddrEl.textContent = s.mdns || "flyradar32.local";

      if (wifiBars) {
        wifiBars.className = s.wifiState === "connected" ? "wifi-bars good" : "wifi-bars";
      }

      if ((s.wifiState === "ap_mode" || s.wifiState === "failed") && !window.captivePortalRedirected) {
        window.captivePortalRedirected = true;
        const settingsTabBtn = document.querySelector('.nav-tab[data-tab="station-settings"]');
        if (settingsTabBtn) settingsTabBtn.click();
        const wifiCard = document.getElementById("wifiSetupCard");
        if (wifiCard) {
          wifiCard.scrollIntoView({ behavior: "smooth" });
          wifiCard.style.outline = "2px solid #00FF00";
          wifiCard.style.boxShadow = "0 0 20px rgba(0,255,0,0.4)";
        }
      }

      // Update seconds ago in header
      const secAgo = Math.max(0, Math.round((Date.now() - lastDataUpdateMs) / 1000));
      const refreshText = document.getElementById("refreshTimerText");
      if (refreshText) {
        refreshText.textContent = secAgo <= 1 ? "LIVE" : `${secAgo}s AGO`;
      }
    } catch (e) {
      console.warn("Status fetch error:", e);
    } finally {
      isFetchingStatus = false;
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
  // Radar Range Zooming Function
  // -------------------------------------------------------------
  function setRadarRange(km, manual = true) {
    km = Math.round(km);
    if (km < 15) km = 15;
    if (km > 300) km = 300;
    radarRangeKm = km;
    if (manual) window.userManuallySelectedRange = true;

    const zoomBadge = document.getElementById("scopeZoomDisplay");
    if (zoomBadge) zoomBadge.textContent = `${radarRangeKm} km`;

    document.querySelectorAll(".range-btn").forEach(b => {
      b.classList.toggle("active", Number(b.dataset.range) === radarRangeKm);
    });
  }

  // Floating On-Canvas Zoom Buttons
  document.getElementById("scopeZoomInBtn")?.addEventListener("click", (e) => {
    e.stopPropagation();
    const step = radarRangeKm > 100 ? 25 : (radarRangeKm > 50 ? 15 : 10);
    setRadarRange(radarRangeKm - step);
  });

  document.getElementById("scopeZoomOutBtn")?.addEventListener("click", (e) => {
    e.stopPropagation();
    const step = radarRangeKm > 100 ? 25 : (radarRangeKm > 50 ? 15 : 10);
    setRadarRange(radarRangeKm + step);
  });

  // Range quick selector buttons on radar toolbar
  document.querySelectorAll(".range-btn").forEach(btn => {
    btn.addEventListener("click", () => {
      setRadarRange(Number(btn.dataset.range));
    });
  });

  // -------------------------------------------------------------
  // Provider Selection Management
  // -------------------------------------------------------------
  function updateProviderUI(activeProviderId) {
    const pId = Number(activeProviderId);
    const select = document.getElementById("primaryProviderSelect");
    if (select) select.value = pId;

    document.querySelectorAll(".provider-item").forEach(item => {
      const id = Number(item.dataset.provider);
      const isPrimary = (id === pId);
      item.classList.toggle("active", isPrimary);
      item.classList.toggle("standby", !isPrimary && (id === 0 || id === 1));
      item.classList.toggle("disabled", !isPrimary && id === 2);

      const badge = item.querySelector(".provider-badge");
      if (badge) {
        if (isPrimary) {
          badge.textContent = "PRIMARY";
          badge.className = "provider-badge";
        } else if (id === 0) {
          badge.textContent = "BACKUP";
          badge.className = "provider-badge backup";
        } else {
          badge.textContent = "TERTIARY";
          badge.className = "provider-badge off";
        }
      }
    });
  }

  async function setPrimaryProvider(providerId) {
    const id = Number(providerId);
    updateProviderUI(id);
    setSavingIndicator(true);
    const names = { 1: "adsb.lol", 0: "OpenSky Network", 2: "airplanes.live" };
    try {
      await apiPost("/api/settings/providers", { primaryProvider: id });
      setSavingIndicator(false, `Active: ${names[id] || "Provider"}`);
      showToast(`Primary telemetry feed switched to ${names[id] || "selected feed"}`);
      setTimeout(fetchAircraft, 300);
      setTimeout(fetchStatus, 500);
    } catch (e) {
      console.warn("Provider switch error:", e);
      setSavingIndicator(false, "Switch Failed");
      showToast("Failed to switch provider: " + e.message, true);
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
      if (s.showSweepAnim !== undefined) {
        showSweepAnim = Boolean(s.showSweepAnim);
      }
      if (sweepToggle) sweepToggle.checked = showSweepAnim;
      if (compassToggle) compassToggle.checked = s.showCompass;
      if (rangeLabelsToggle) rangeLabelsToggle.checked = s.showRangeLabels;
      if (trailToggle) trailToggle.checked = s.showTrail;
      const autoRangeToggle = document.getElementById("autoRangeToggle");
      if (autoRangeToggle) autoRangeToggle.checked = Boolean(s.autoRange);
      if (refreshSelect) refreshSelect.value = s.refreshInterval;

      if (s.primaryProvider !== undefined) {
        updateProviderUI(s.primaryProvider);
      }

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

  // Primary Provider Select and click listeners
  document.getElementById("primaryProviderSelect")?.addEventListener("change", (e) => {
    setPrimaryProvider(e.target.value);
  });

  document.querySelectorAll(".provider-item").forEach(item => {
    item.addEventListener("click", () => {
      const p = item.dataset.provider;
      if (p !== undefined) {
        setPrimaryProvider(p);
      }
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

  // Auto Range toggle (device-side auto zoom)
  document.getElementById("autoRangeToggle")?.addEventListener("change", (e) => {
    triggerAutoSaveDisplay({ autoRange: e.target.checked });
  });

  // Dropdowns auto-save
  document.getElementById("zoomSelect")?.addEventListener("change", (e) => {
    // Manual zoom pick disables auto-range on the device
    triggerAutoSaveDisplay({ zoomLevel: parseInt(e.target.value, 10) });
    const autoRangeToggle = document.getElementById("autoRangeToggle");
    if (autoRangeToggle) autoRangeToggle.checked = false;
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
    showSweepAnim = e.target.checked;
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
    if (!confirm("Forget Wi-Fi credentials? The device will restart in setup AP mode.")) return;
    const btn = document.getElementById("disconnectBtn");
    if (btn) { btn.disabled = true; btn.textContent = "Forgetting..."; }
    showToast("Forgetting Wi-Fi... Device will reboot into setup mode in ~3s");
    try {
      await apiPost("/api/wifi/clear", {});
    } catch (e) {
      // Expected: device reboots mid-request, fetch throws NetworkError — that's fine.
    }
    // Show countdown — device reboots within ~400ms of the request
    showToast("✓ Done! Connect to the FlyRadar32-XXXX Wi-Fi AP that appears in ~10s");
    if (btn) { btn.textContent = "Restarting..."; }
  });

  document.getElementById("resetBtn")?.addEventListener("click", async () => {
    if (!confirm("Factory Reset: ALL settings (Wi-Fi, location, providers) will be erased. Continue?")) return;
    const btn = document.getElementById("resetBtn");
    if (btn) { btn.disabled = true; btn.textContent = "Resetting..."; }
    showToast("Factory reset in progress... Device rebooting in ~3s");
    try {
      await apiPost("/api/factory-reset", {});
    } catch (e) {
      // Expected: device reboots and closes connection.
    }
    showToast("✓ Factory reset complete. Connect to new FlyRadar32-XXXX AP in ~10s");
    if (btn) { btn.textContent = "Reset Done"; }
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
    let scanActive = false;
    const pollScan = async () => {
      try {
        const res = await apiGet("/api/scan");
        if (res.scanning) {
          if (scanCount) scanCount.textContent = "(scanning...)";
          setTimeout(pollScan, 800);
          return;
        }
        const nets = Array.isArray(res) ? res : [];
        if (scanCount) scanCount.textContent = `(${nets.length})`;
        if (nets.length === 0) {
          if (netList) netList.innerHTML = "<p class='text-dim' style='padding:8px'>No networks found.</p>";
        } else {
          netList.innerHTML = nets.sort((a, b) => b.rssi - a.rssi).map(n => `
            <div class="net-item" data-ssid="${esc(n.ssid)}">
              <span class="net-name">${esc(n.ssid)}</span>
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
        if (scanCard) scanCard.style.display = "block";
      } finally {
        scanBtn.disabled = false;
        scanBtn.textContent = "Scan";
      }
    };

    scanBtn.addEventListener("click", async () => {
      if (scanActive) return;
      scanActive = true;
      scanBtn.disabled = true;
      scanBtn.textContent = "Scanning...";
      if (scanCard) scanCard.style.display = "none";
      if (netList) netList.innerHTML = "";
      try {
        await apiGet("/api/scan");   // kick off the async scan
        setTimeout(pollScan, 800);   // then poll until done
      } catch (e) {
        showToast("Wi-Fi scan failed: " + e.message, true);
        scanBtn.disabled = false;
        scanBtn.textContent = "Scan";
      } finally {
        scanActive = false;
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
        alert(`Success! Connecting to network '${ssid}'...\n\nESP32 is now switching to Station mode and closing setup AP.\n\nPlease reconnect your device to '${ssid}' and open:\nhttp://flyradar32.local`);
        showToast(`Wi-Fi saved! Connect to '${ssid}' and visit http://flyradar32.local`);
      } catch (e) {
        showToast("Connect failed: " + e.message, true);
      } finally {
        connectBtn.disabled = false;
        connectBtn.textContent = "Connect Wi-Fi";
      }
    });
  }

  // -------------------------------------------------------------
  // OpenSky Credentials & JSON Import Handlers
  // -------------------------------------------------------------
  const jsonFileInput = document.getElementById("openskyJsonFile");
  const jsonFileStatus = document.getElementById("jsonFileStatus");
  if (jsonFileInput) {
    jsonFileInput.addEventListener("change", (e) => {
      const file = e.target.files[0];
      if (!file) return;
      const reader = new FileReader();
      reader.onload = (evt) => {
        try {
          const data = JSON.parse(evt.target.result);
          const clientId = data.clientId || data.client_id || data.clientIdStr || "";
          const clientSecret = data.clientSecret || data.client_secret || data.secret || "";

          if (clientId) {
            const el = document.getElementById("osClientId");
            if (el) el.value = clientId;
          }
          if (clientSecret) {
            const el = document.getElementById("osClientSecret");
            if (el) el.value = clientSecret;
          }

          if (clientId && clientSecret) {
            if (jsonFileStatus) jsonFileStatus.textContent = `Loaded credentials for '${clientId}'`;
            showToast("OpenSky credentials loaded from JSON file!");
            apiPost("/api/settings/opensky", { clientId, clientSecret })
              .then(() => setSavingIndicator(false, "OpenSky Credentials Saved"))
              .catch(err => console.warn("OpenSky save error:", err));
          } else {
            showToast("JSON loaded, but clientId or clientSecret key was missing", true);
          }
        } catch (err) {
          showToast("Failed to parse credentials JSON file: " + err.message, true);
        }
      };
      reader.readAsText(file);
    });
  }

  const openSkyForm = document.getElementById("openSkyForm");
  if (openSkyForm) {
    openSkyForm.addEventListener("submit", async (e) => {
      e.preventDefault();
      const clientId = document.getElementById("osClientId")?.value.trim() || "";
      const clientSecret = document.getElementById("osClientSecret")?.value.trim() || "";
      try {
        await apiPost("/api/settings/opensky", { clientId, clientSecret });
        showToast("OpenSky credentials saved to ESP32!");
      } catch (err) {
        showToast("Failed to save OpenSky credentials: " + err.message, true);
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
    if (!lat || !lon || isNaN(lat) || isNaN(lon) || !rangeKm || rangeKm <= 0) return;
    try {

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
    const n = Math.pow(2, zoom);

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
          // FIFO cap so the cache can't grow unbounded when panning/zooming
          const keys = Object.keys(mapTileCache);
          if (keys.length > 80) { delete mapTileCache[keys[0]]; }
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
    } catch (err) {
      console.warn("Map overlay render error:", err);
    }
  }

  // -------------------------------------------------------------
  // Initialization & Continuous Realtime Loops
  // -------------------------------------------------------------
  loadSettings();
  fetchAircraft();
  fetchStatus();

  // Start continuous 60fps radar sweep
  requestAnimationFrame(renderRadar);

  // Automated Realtime Data Fetch (Every 3.5 seconds)
  setInterval(fetchAircraft, 3500);

  // Background Status Refresh (Every 6 seconds)
  setInterval(fetchStatus, 6000);

})();
