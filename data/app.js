(() => {
  "use strict";

  let currentPlanes = [];
  let selectedPlane = null;
  let radarRangeKm = 300;
  let showTrails = true;
  let showVectors = false;
  let showSweepAnim = true;
  let sweepAngle = 0;
  let activeTheme = 0; // 0: Green, 1: Cyan, 2: Amber
  let lastDataUpdateMs = Date.now();
  let mapTilesEnabled = true;
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
      } else if (targetTab === "weather") {
        renderWeatherPanel();
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
          ctx.arc(px, py, 14, 0, Math.PI * 2);
          ctx.stroke();
          ctx.restore();

          // Reticle Stems
          ctx.strokeStyle = "rgba(255, 255, 255, 0.4)";
          ctx.lineWidth = 1;
          ctx.setLineDash([2, 2]);
          ctx.beginPath();
          ctx.moveTo(px, py - 18); ctx.lineTo(px, py + 18);
          ctx.moveTo(px - 18, py); ctx.lineTo(px + 18, py);
          ctx.stroke();
          ctx.setLineDash([]);
        }

        // Callsign Tag
        ctx.font = `bold ${Math.max(9, Math.round(w * 0.02))}px 'JetBrains Mono', monospace`;
        ctx.fillStyle = "#FFFFFF";
        ctx.textAlign = "left";
        ctx.fillText(p.flight || p.hex, px + 13, py - 5);
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
    ctx.scale(1.3, 1.3);
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

      // Web dashboard preserves regional 300km scope

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

      if (providerVal) providerVal.textContent = s.lastProvider || s.primaryProvider || "OpenSky";
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
        // Auto-navigate to Settings → WiFi card after a brief delay so the DOM settles
        setTimeout(() => {
          const settingsTabBtn = document.querySelector('.nav-tab[data-tab="settings"]');
          if (settingsTabBtn) settingsTabBtn.click();
          setTimeout(() => {
            const wifiCard = document.getElementById("wifiSetupCard");
            if (wifiCard) {
              wifiCard.scrollIntoView({ behavior: "smooth", block: "start" });
              wifiCard.style.transition = "box-shadow 0.4s ease";
              wifiCard.style.boxShadow = "0 0 0 2px var(--accent), 0 0 32px var(--accent-glow)";
              setTimeout(() => { wifiCard.style.boxShadow = ""; }, 3500);
              // Auto-trigger scan when opening captive portal
              const sb = document.getElementById("scanBtn");
              if (sb && !sb.disabled) setTimeout(() => sb.click(), 600);
            }
          }, 300);
        }, 400);
      }

      // Update seconds ago in header
      const secAgo = Math.max(0, Math.round((Date.now() - lastDataUpdateMs) / 1000));
      const refreshText = document.getElementById("refreshTimerText");
      if (refreshText) {
        refreshText.textContent = secAgo <= 1 ? "LIVE" : `${secAgo}s AGO`;
      }

      renderWeatherPanel(s);
    } catch (e) {
      console.warn("Status fetch error:", e);
    } finally {
      isFetchingStatus = false;
    }
  }

  // -------------------------------------------------------------
  // Station Aerodrome Weather Panel
  // -------------------------------------------------------------
  let lastWeatherStatus = null;

  function renderWeatherPanel(status) {
    const s = status || lastWeatherStatus;
    if (!s) return;
    lastWeatherStatus = s;

    if (s.tempC === undefined) {
      const condLarge = document.getElementById("wxConditionLarge");
      if (condLarge) condLarge.textContent = "SYNCHRONIZING WITH OPEN-METEO...";
      return;
    }

    // Temperature & Condition
    const tempLarge = document.getElementById("wxTempLarge");
    if (tempLarge) tempLarge.textContent = Math.round(s.tempC);

    const feelsEl = document.getElementById("wxFeelsText");
    if (feelsEl) feelsEl.textContent = `${Math.round(s.feelsC !== undefined ? s.feelsC : s.tempC)}\u00B0C`;

    // WMO Condition Decoder & Dynamic SVG Icon
    const wmoMap = {
      0: "CLEAR SKY",
      1: "MAINLY CLEAR",
      2: "PARTLY CLOUDY",
      3: "OVERCAST",
      45: "FOG / MIST",
      48: "RIME FOG",
      51: "LIGHT DRIZZLE",
      53: "MODERATE DRIZZLE",
      55: "DENSE DRIZZLE",
      61: "SLIGHT RAIN",
      63: "MODERATE RAIN",
      65: "HEAVY RAIN",
      71: "SLIGHT SNOW",
      73: "MODERATE SNOW",
      75: "HEAVY SNOW",
      80: "SLIGHT SHOWERS",
      81: "MODERATE SHOWERS",
      82: "VIOLENT SHOWERS",
      95: "THUNDERSTORM",
      96: "THUNDERSTORM & HAIL",
      99: "HEAVY THUNDERSTORM"
    };
    const condition = wmoMap[s.wmo] || (s.cloud > 60 ? "CLOUDY" : "FAIR");
    const condLarge = document.getElementById("wxConditionLarge");
    if (condLarge) condLarge.textContent = condition;

    // Dynamic Weather SVG Icon
    const iconWrap = document.getElementById("wxDynamicIcon");
    if (iconWrap) {
      const wmo = s.wmo || 0;
      if (wmo >= 95) {
        iconWrap.innerHTML = `<svg width="46" height="46" viewBox="0 0 24 24" fill="none" stroke="#FFD600" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
          <path d="M19 16.9A5 5 0 0 0 18 7h-1.26a8 8 0 1 0-11.62 9"/>
          <polygon points="13 11 9 17 15 17 11 23" fill="#FFD600"/>
        </svg>`;
      } else if ((wmo >= 51 && wmo <= 86) || (s.precip && s.precip > 0.1)) {
        iconWrap.innerHTML = `<svg width="46" height="46" viewBox="0 0 24 24" fill="none" stroke="#29B6F6" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
          <path d="M4 14.899A7 7 0 1 1 15.71 8h1.79a4.5 4.5 0 0 1 2.5 8.242"/>
          <path d="M16 14v6M8 14v6M12 16v6"/>
        </svg>`;
      } else if (wmo === 2 || wmo === 3 || wmo === 45 || wmo === 48 || (s.cloud && s.cloud > 50)) {
        iconWrap.innerHTML = `<svg width="46" height="46" viewBox="0 0 24 24" fill="none" stroke="#90CAF9" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
          <path d="M17.5 19H9a7 7 0 1 1 6.71-9h1.79a4.5 4.5 0 1 1 0 9Z"/>
        </svg>`;
      } else {
        iconWrap.innerHTML = `<svg width="46" height="46" viewBox="0 0 24 24" fill="none" stroke="#FFA726" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
          <circle cx="12" cy="12" r="4"/>
          <path d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M6.34 17.66l-1.41 1.41M19.07 4.93l-1.41 1.41"/>
        </svg>`;
      }
    }

    // Flight Category (VFR / MVFR / IFR / LIFR)
    const flightCat = document.getElementById("wxFlightCat");
    const flightDesc = document.getElementById("wxFlightCatDesc");
    const rh = s.humidity || 50;
    const dew = Math.round(s.tempC - ((100 - rh) / 5));

    if (flightCat) {
      let cat = "VFR";
      let desc = "Visual Flight Rules \u2022 Ceiling > 3,000 FT, Vis > 5 SM";
      let color = "#00E676";
      let bg = "rgba(0, 230, 118, 0.15)";
      let border = "rgba(0, 230, 118, 0.4)";
      if (s.wmo >= 95 || s.precip > 5.0) {
        cat = "LIFR";
        desc = "Low Instrument Flight Rules \u2022 Severe Weather Alert";
        color = "#FF1744";
        bg = "rgba(255, 23, 68, 0.15)";
        border = "rgba(255, 23, 68, 0.4)";
      } else if (s.cloud >= 85 || s.precip > 1.5) {
        cat = "IFR";
        desc = "Instrument Flight Rules \u2022 Ceiling 500-1,000 FT / Low Vis";
        color = "#FFB300";
        bg = "rgba(255, 179, 0, 0.15)";
        border = "rgba(255, 179, 0, 0.4)";
      } else if (s.cloud >= 50) {
        cat = "MVFR";
        desc = "Marginal VFR \u2022 Ceiling 1,000-3,000 FT AGL";
        color = "#00E5FF";
        bg = "rgba(0, 229, 255, 0.15)";
        border = "rgba(0, 229, 255, 0.4)";
      }
      flightCat.textContent = cat;
      flightCat.style.color = color;
      flightCat.style.background = bg;
      flightCat.style.borderColor = border;
      if (flightDesc) flightDesc.textContent = desc;
    }

    // Dewpoint Spread & Estimated Cloud Ceiling AGL
    const dewSpreadEl = document.getElementById("wxDewSpread");
    if (dewSpreadEl) {
      dewSpreadEl.textContent = `${Math.abs(s.tempC - dew).toFixed(1)}\u00B0C`;
    }

    const ceilingEl = document.getElementById("wxCloudCeiling");
    if (ceilingEl) {
      if ((s.cloud || 0) < 15) {
        ceilingEl.textContent = "UNLIMITED";
      } else {
        const estCeil = Math.max(500, Math.round((s.tempC - dew) * 400));
        ceilingEl.textContent = `~${estCeil.toLocaleString()} FT AGL`;
      }
    }

    // Wind Compass & Stats
    const dirs = ["N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"];
    const cardIdx = Math.round(((s.windDeg % 360) / 22.5)) % 16;
    const cardinal = dirs[cardIdx] || "N";

    const windSpeedEl = document.getElementById("wxWindSpeedText");
    if (windSpeedEl) windSpeedEl.textContent = `${Math.round(s.windKt)} kt (${Math.round(s.windKt * 1.852)} km/h)`;

    const windGustEl = document.getElementById("wxWindGustText");
    if (windGustEl) windGustEl.textContent = `${Math.round(s.gustKt || 0)} kt`;

    const windDirEl = document.getElementById("wxWindDirText");
    if (windDirEl) windDirEl.textContent = `${Math.round(s.windDeg || 0)}\u00B0`;

    const windCardEl = document.getElementById("wxWindCardText");
    if (windCardEl) windCardEl.textContent = cardinal;

    const arrow = document.getElementById("wxWindArrow");
    if (arrow) {
      arrow.style.transform = `rotate(${Math.round(s.windDeg || 0)}deg)`;
    }

    // Runway Crosswind & Headwind Computer
    if (window.selectedRunwayHdg === undefined) window.selectedRunwayHdg = 90;
    const windRad = ((s.windDeg || 0) - window.selectedRunwayHdg) * (Math.PI / 180);
    const speed = s.windKt || 0;
    const xwind = speed * Math.sin(windRad);
    const hwind = speed * Math.cos(windRad);

    const xwVal = document.getElementById("wxCrosswindVal");
    if (xwVal) {
      const xwAbs = Math.abs(xwind).toFixed(1);
      const xwSide = xwind >= 0.5 ? "LEFT" : (xwind <= -0.5 ? "RIGHT" : "DIRECT");
      xwVal.textContent = `${xwAbs} KT [${xwSide}]`;
      xwVal.style.color = Math.abs(xwind) > 15 ? "#FF1744" : (Math.abs(xwind) > 8 ? "#FFB300" : "#00E676");
    }

    const hwVal = document.getElementById("wxHeadwindVal");
    if (hwVal) {
      const hwAbs = Math.abs(hwind).toFixed(1);
      const hwType = hwind >= 0 ? "HEAD" : "TAIL";
      hwVal.textContent = `${hwAbs} KT ${hwType}`;
      hwVal.style.color = hwind < -5 ? "#FF1744" : "#00E676";
    }

    const rwyLine = document.getElementById("wxRunwayLine");
    if (rwyLine) {
      rwyLine.style.transform = `rotate(${window.selectedRunwayHdg}deg)`;
    }

    // Atmospheric Telemetry
    const pressEl = document.getElementById("wxPressureVal");
    if (pressEl) pressEl.textContent = `${Math.round(s.pressure || 1013)} hPa`;

    const pressInHg = document.getElementById("wxPressureInHg");
    if (pressInHg) {
      const p = s.pressure || 1013;
      const isaDiff = Math.round(p - 1013.25);
      const diffStr = isaDiff >= 0 ? `+${isaDiff}` : `${isaDiff}`;
      pressInHg.textContent = `${(p * 0.02953).toFixed(2)} inHg \u2022 ${diffStr} ISA`;
    }

    const humEl = document.getElementById("wxHumidityVal");
    if (humEl) humEl.textContent = `${Math.round(s.humidity || 0)}%`;

    const dewPointEl = document.getElementById("wxDewPoint");
    if (dewPointEl) dewPointEl.textContent = `Dewpoint: ${dew}\u00B0C`;

    const cloudEl = document.getElementById("wxCloudVal");
    if (cloudEl) cloudEl.textContent = `${Math.round(s.cloud || 0)}%`;

    const cloudCovEl = document.getElementById("wxCloudCoverage");
    if (cloudCovEl) {
      const c = s.cloud || 0;
      cloudCovEl.textContent = c < 15 ? "SKC (Clear Sky 0/8)" : (c < 35 ? "FEW (Few Clouds 2/8)" : (c < 65 ? "SCT (Scattered 4/8)" : (c < 85 ? "BKN (Broken 6/8)" : "OVC (Overcast 8/8)")));
    }

    const precipEl = document.getElementById("wxPrecipVal");
    if (precipEl) precipEl.textContent = `${(s.precip || 0).toFixed(1)} mm`;

    const precipRateEl = document.getElementById("wxPrecipRate");
    if (precipRateEl) {
      const pr = s.precip || 0;
      precipRateEl.textContent = pr > 2.5 ? "Heavy Hourly Accum" : (pr > 0.5 ? "Moderate Hourly Accum" : (pr > 0 ? "Light Precipitation" : "Nil Hourly Accumulation"));
    }

    // Raw METAR generation
    const rawMetar = document.getElementById("wxRawMetar");
    if (rawMetar) {
      const wDeg = String(Math.round(s.windDeg || 0)).padStart(3, '0');
      const wKt = String(Math.round(s.windKt || 0)).padStart(2, '0');
      const tSign = s.tempC < 0 ? "M" : "";
      const tVal = String(Math.abs(Math.round(s.tempC))).padStart(2, '0');
      const dSign = dew < 0 ? "M" : "";
      const dVal = String(Math.abs(dew)).padStart(2, '0');
      const qnh = String(Math.round(s.pressure || 1013)).padStart(4, '0');
      rawMetar.textContent = `METAR FLYR32 AUTO ${wDeg}${wKt}KT ${tSign}${tVal}/${dSign}${dVal} Q${qnh}=`;
    }

    const desc = document.getElementById("wxStationDesc");
    if (desc && s.time) {
      desc.textContent = `Station METAR observation synchronized at ${s.time} via Open-Meteo.`;
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

    const shortNames = { 0: "OpenSky", 1: "adsb.lol", 2: "airplanes.live" };
    const providerVal = document.getElementById("activeProvider");
    if (providerVal && shortNames[pId]) {
      providerVal.textContent = shortNames[pId];
    }

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

      if (s.brightness !== undefined) {
        const br = Number(s.brightness);
        const brSlider = document.getElementById("brightnessSlider");
        const brVal = document.getElementById("brightnessVal");
        if (brSlider) brSlider.value = br;
        if (brVal) brVal.textContent = `${br}%`;
      }

      if (s.primaryProvider !== undefined) {
        updateProviderUI(s.primaryProvider);
      }

      // Sync Active Theme Swatch
      activeTheme = s.theme || 0;
      applyTheme(activeTheme);

      // Station Timezone
      const tzSelect = document.getElementById("tzSelect");
      const customTzGroup = document.getElementById("customTzGroup");
      const customTzInput = document.getElementById("customTzInput");
      if (tzSelect && s.timezone) {
        let found = false;
        for (let opt of tzSelect.options) {
          if (opt.value === s.timezone) {
            tzSelect.value = s.timezone;
            found = true;
            break;
          }
        }
        if (!found) {
          tzSelect.value = "CUSTOM";
          if (customTzGroup) customTzGroup.style.display = "block";
          if (customTzInput) customTzInput.value = s.timezone;
        } else {
          if (customTzGroup) customTzGroup.style.display = "none";
        }
      }
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



  // Hardware Backlight Brightness slider instant auto-save
  const brightnessSlider = document.getElementById("brightnessSlider");
  const brightnessVal = document.getElementById("brightnessVal");
  if (brightnessSlider) {
    brightnessSlider.addEventListener("input", (e) => {
      const val = Number(e.target.value);
      if (brightnessVal) brightnessVal.textContent = `${val}%`;
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

  // Timezone Auto-Save
  const tzSelect = document.getElementById("tzSelect");
  const customTzGroup = document.getElementById("customTzGroup");
  const customTzInput = document.getElementById("customTzInput");

  async function saveTimezoneValue(tz) {
    if (!tz) return;
    try {
      await apiPost("/api/settings/timezone", { timezone: tz });
      showToast(`Station Timezone updated: ${tz}`);
    } catch (e) {
      showToast("Failed to save timezone: " + e.message, true);
    }
  }

  tzSelect?.addEventListener("change", (e) => {
    if (e.target.value === "CUSTOM") {
      if (customTzGroup) customTzGroup.style.display = "block";
    } else {
      if (customTzGroup) customTzGroup.style.display = "none";
      saveTimezoneValue(e.target.value);
    }
  });

  let tzDebounce = null;
  customTzInput?.addEventListener("input", (e) => {
    clearTimeout(tzDebounce);
    tzDebounce = setTimeout(() => {
      saveTimezoneValue(e.target.value.trim());
    }, 600);
  });

  // Danger Zone Actions
  document.getElementById("disconnectBtn")?.addEventListener("click", async () => {
    showConfirmModal("Forget Wi-Fi credentials? The device will restart in setup AP mode.", async () => {
      const btn = document.getElementById("disconnectBtn");
      if (btn) { btn.disabled = true; btn.textContent = "Forgetting..."; }
      showToast("Forgetting Wi-Fi... Device will reboot into setup mode in ~3s");
      try {
        await apiPost("/api/wifi/clear", {});
      } catch (e) {
        // Expected: device reboots mid-request
      }
      showToast("✓ Done! Connect to the FlyRadar32-XXXX Wi-Fi AP that appears in ~10s");
      if (btn) { btn.textContent = "Restarting..."; }
    });
  });

  document.getElementById("resetBtn")?.addEventListener("click", async () => {
    showConfirmModal("Factory Reset: ALL settings (Wi-Fi, location, providers) will be permanently erased. This cannot be undone.", async () => {
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
  });


  // -------------------------------------------------------------
  // -------------------------------------------------------------
  // Verified WiFi Connection Flow & Modal (Live testing + Status polling)
  // -------------------------------------------------------------
  function startWifiConnectFlow(ssid, pass) {
    const existing = document.getElementById("wifiStatusModal");
    if (existing) existing.remove();

    const modal = document.createElement("div");
    modal.id = "wifiStatusModal";
    modal.style.cssText = `
      position:fixed; inset:0; z-index:9999;
      background:rgba(0,0,0,0.85); backdrop-filter:blur(8px);
      display:flex; align-items:center; justify-content:center;
      animation:fadeInModal 0.25s ease;
    `;

    modal.innerHTML = `
      <style>
        @keyframes fadeInModal { from{opacity:0;transform:scale(0.94)} to{opacity:1;transform:scale(1)} }
        @keyframes spinRing { to{transform:rotate(360deg)} }
        .wm-card {
          background: linear-gradient(145deg, #0a1628, #060f1e);
          border: 1px solid var(--accent, #00ff00);
          border-radius: 16px;
          box-shadow: 0 0 40px var(--accent-glow, rgba(0,255,0,0.2)), 0 20px 60px rgba(0,0,0,0.5);
          padding: 36px 38px;
          max-width: 480px;
          width: 90vw;
          text-align: center;
          font-family: 'Chakra Petch', 'Inter', sans-serif;
          position: relative;
        }
        .wm-icon-wrap {
          width:68px; height:68px; margin:0 auto 16px;
          display:flex; align-items:center; justify-content:center;
          border-radius:50%; position:relative;
        }
        .wm-icon-spin {
          width:100%; height:100%; position:absolute; inset:0;
          border: 3px solid rgba(0,229,255,0.15);
          border-top-color: var(--accent-cyan, #00e5ff);
          border-radius: 50%;
          animation: spinRing 1s linear infinite;
        }
        .wm-title { font-size:1.35rem; font-weight:700; color:var(--accent, #00ff00); margin-bottom:8px; letter-spacing:0.06em; }
        .wm-sub { font-size:0.92rem; color:#b0ffc0; margin-bottom:20px; }
        .wm-steps { text-align:left; background:rgba(0,255,0,0.04); border:1px solid rgba(0,255,0,0.12); border-radius:8px; padding:14px 18px; margin-bottom:20px; }
        .wm-step { font-size:0.82rem; color:#8bc; font-family:'JetBrains Mono',monospace; margin:5px 0; display:flex; align-items:center; gap:8px; }
        .wm-step-dot { width:8px; height:8px; border-radius:50%; background:var(--accent,#00ff00); flex-shrink:0; }
        .wm-step-dot.spin { background:transparent; border:2px solid var(--accent-cyan,#00e5ff); border-top-color:transparent; animation:spinRing 0.8s linear infinite; }
        .wm-step-dot.fail { background:#ff4444; }
        .wm-notice { font-size:0.75rem; color:#678; font-family:'JetBrains Mono',monospace; margin-bottom:18px; line-height:1.4; }
        .wm-btn-row { display:flex; gap:10px; justify-content:center; }
        .wm-action-btn {
          background:var(--accent,#00ff00); color:#000; font-weight:700; font-size:0.9rem;
          border:none; border-radius:8px; padding:11px 32px; cursor:pointer; letter-spacing:0.06em;
          transition:opacity 0.2s;
        }
        .wm-action-btn:hover { opacity:0.85; }
        .wm-retry-btn {
          background:rgba(255,255,255,0.08); border:1px solid rgba(255,255,255,0.2); color:#fff;
          border-radius:8px; padding:11px 26px; cursor:pointer; font-weight:600; font-size:0.88rem;
        }
        .wm-retry-btn:hover { background:rgba(255,255,255,0.14); }
      </style>
      <div class="wm-card" id="wmCard">
        <div class="wm-icon-wrap" style="background:rgba(0,229,255,0.08); border:2px solid rgba(0,229,255,0.3);">
          <div class="wm-icon-spin"></div>
          <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="var(--accent-cyan,#00e5ff)" stroke-width="2">
            <path d="M5 12.55a11 11 0 0 1 14.08 0M1.42 9a16 16 0 0 1 21.16 0M8.53 16.11a6 6 0 0 1 6.95 0M12 20h.01"/>
          </svg>
        </div>
        <div class="wm-title" style="color:var(--accent-cyan,#00e5ff);">CONNECTING...</div>
        <div class="wm-sub" id="wmSub">Attempting link to <strong>${esc(ssid)}</strong>...</div>
        <div class="wm-steps">
          <div class="wm-step" id="wmStep1"><span class="wm-step-dot"></span> Initiating handshake with router</div>
          <div class="wm-step" id="wmStep2"><span class="wm-step-dot spin"></span> Negotiating DHCP &amp; verifying credentials</div>
          <div class="wm-step" id="wmStep3"><span class="wm-step-dot spin"></span> Keeping FlyRadar32 Setup AP alive</div>
        </div>
        <div class="wm-notice">Please do not disconnect. Testing connectivity in background...</div>
      </div>
    `;
    document.body.appendChild(modal);

    apiPost("/api/wifi/connect", { ssid, password: pass }).then(() => {
      pollWifiStatus(modal, ssid);
    }).catch(err => {
      showWifiFailState(modal, ssid, err.message || "Failed to trigger connect");
    });
  }

  function pollWifiStatus(modal, ssid) {
    let attempts = 0;
    const maxAttempts = 22;
    const interval = setInterval(async () => {
      attempts++;
      try {
        const s = await apiGet("/api/wifi/status");
        if (s.state === "connected") {
          clearInterval(interval);
          showWifiConnectedState(modal, ssid, s);
        } else if (s.state === "failed") {
          clearInterval(interval);
          showWifiFailState(modal, ssid, s.error || "Incorrect password or network unreachable.");
        } else if (attempts >= maxAttempts) {
          clearInterval(interval);
          showWifiFailState(modal, ssid, "Connection timed out. Check password or signal range.");
        }
      } catch (err) {
        if (attempts >= maxAttempts) {
          clearInterval(interval);
          showWifiFailState(modal, ssid, "Network unreachable. Please check credentials.");
        }
      }
    }, 1000);
  }

  function showWifiConnectedState(modal, ssid, s) {
    const card = modal.querySelector("#wmCard");
    if (!card) return;
    card.style.border = "1px solid var(--accent, #00ff00)";
    card.style.boxShadow = "0 0 40px var(--accent-glow, rgba(0,255,0,0.25)), 0 20px 60px rgba(0,0,0,0.5)";

    const staIp = s.staIp && s.staIp !== "0.0.0.0" ? s.staIp : "Acquiring IP...";
    let countdownSec = s.apRemainingSec > 0 ? s.apRemainingSec : 4;

    card.innerHTML = `
      <div class="wm-icon-wrap" style="background:rgba(0,255,0,0.1); border:2px solid var(--accent,#00ff00);">
        <svg width="34" height="34" viewBox="0 0 24 24" fill="none" stroke="var(--accent,#00ff00)" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
          <polyline points="20 6 9 17 4 12"/>
        </svg>
      </div>
      <div class="wm-title">✓ CONNECTED!</div>
      <div class="wm-sub">Successfully linked to <strong>${esc(ssid)}</strong></div>
      <div class="wm-steps">
        <div class="wm-step"><span class="wm-step-dot"></span> Verified credentials &amp; saved to hardware</div>
        <div class="wm-step"><span class="wm-step-dot"></span> Station IP: <strong style="color:var(--accent,#00ff00);">${esc(staIp)}</strong></div>
        <div class="wm-step"><span class="wm-step-dot"></span> Setup AP closing in <span id="wmCountdown" style="color:var(--accent,#00ff00);font-weight:bold;">${countdownSec}s</span></div>
      </div>
      <div style="text-align:left;margin-bottom:18px;">
        <strong style="display:block;font-size:0.75rem;color:#8bc;margin-bottom:6px;text-transform:uppercase;letter-spacing:0.08em;">Reconnect device to <em>${esc(ssid)}</em>, then open:</strong>
        <code style="font-size:0.95rem;font-family:'JetBrains Mono',monospace;color:var(--accent,#00ff00);background:rgba(0,255,0,0.07);border:1px solid rgba(0,255,0,0.3);border-radius:6px;padding:8px 14px;display:block;word-break:break-all;">http://flyradar32.local</code>
      </div>
      <div class="wm-btn-row">
        <button class="wm-action-btn" id="wmDoneBtn">Got it</button>
      </div>
    `;

    const doneBtn = card.querySelector("#wmDoneBtn");
    if (doneBtn) doneBtn.addEventListener("click", () => modal.remove());

    const cdEl = card.querySelector("#wmCountdown");
    const cdTimer = setInterval(() => {
      countdownSec--;
      if (cdEl) cdEl.textContent = `${Math.max(0, countdownSec)}s`;
      if (countdownSec <= 0) {
        clearInterval(cdTimer);
        if (cdEl) cdEl.textContent = "Switched to Station mode";
      }
    }, 1000);
  }

  function showWifiFailState(modal, ssid, errorMsg) {
    const card = modal.querySelector("#wmCard");
    if (!card) return;
    card.style.border = "1px solid #ff4444";
    card.style.boxShadow = "0 0 40px rgba(255,68,68,0.25), 0 20px 60px rgba(0,0,0,0.5)";

    card.innerHTML = `
      <div class="wm-icon-wrap" style="background:rgba(255,68,68,0.1); border:2px solid #ff4444;">
        <svg width="34" height="34" viewBox="0 0 24 24" fill="none" stroke="#ff4444" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
          <line x1="18" y1="6" x2="6" y2="18"/>
          <line x1="6" y1="6" x2="18" y2="18"/>
        </svg>
      </div>
      <div class="wm-title" style="color:#ff4444;">CONNECTION FAILED</div>
      <div class="wm-sub" style="color:#ffb0b0;">Could not connect to <strong>${esc(ssid)}</strong></div>
      <div class="wm-steps" style="background:rgba(255,68,68,0.04); border-color:rgba(255,68,68,0.2);">
        <div class="wm-step" style="color:#ffb0b0;"><span class="wm-step-dot fail"></span> ${esc(errorMsg)}</div>
        <div class="wm-step" style="color:#8bc;"><span class="wm-step-dot" style="background:#00e5ff;"></span> Check password accuracy and 2.4 GHz range</div>
        <div class="wm-step" style="color:#8bc;"><span class="wm-step-dot" style="background:#00e5ff;"></span> FlyRadar32 Setup AP is still active</div>
      </div>
      <div class="wm-btn-row">
        <button class="wm-retry-btn" id="wmRetryBtn">Try Again</button>
      </div>
    `;

    const retryBtn = card.querySelector("#wmRetryBtn");
    if (retryBtn) {
      retryBtn.addEventListener("click", () => {
        modal.remove();
        const passInput = document.getElementById("passInput");
        if (passInput) {
          passInput.focus();
          passInput.select();
        }
      });
    }
  }

  // -------------------------------------------------------------
  // Themed Confirm Dialog (replaces window.confirm)
  // -------------------------------------------------------------
  function showConfirmModal(message, onConfirm) {
    const existing = document.getElementById("confirmModal");
    if (existing) existing.remove();
    const modal = document.createElement("div");
    modal.id = "confirmModal";
    modal.style.cssText = `position:fixed;inset:0;z-index:9999;background:rgba(0,0,0,0.75);backdrop-filter:blur(6px);display:flex;align-items:center;justify-content:center;`;
    modal.innerHTML = `
      <div style="background:linear-gradient(145deg,#0c1a2e,#060f1e);border:1px solid rgba(255,100,100,0.4);border-radius:14px;padding:28px 32px;max-width:400px;width:88vw;font-family:'Chakra Petch','Inter',sans-serif;">
        <div style="font-size:1.05rem;font-weight:700;color:#ff6b6b;margin-bottom:12px;">⚠ Confirm Action</div>
        <div style="font-size:0.9rem;color:#cde;line-height:1.5;margin-bottom:22px;">${esc(message)}</div>
        <div style="display:flex;gap:10px;justify-content:flex-end;">
          <button id="cModalCancel" style="background:rgba(255,255,255,0.08);border:1px solid rgba(255,255,255,0.15);color:#aaa;border-radius:7px;padding:8px 22px;cursor:pointer;font-size:0.88rem;">Cancel</button>
          <button id="cModalOk" style="background:#ff4444;border:none;color:#fff;border-radius:7px;padding:8px 22px;cursor:pointer;font-weight:700;font-size:0.88rem;">Confirm</button>
        </div>
      </div>
    `;
    document.body.appendChild(modal);
    document.getElementById("cModalCancel").addEventListener("click", () => modal.remove());
    document.getElementById("cModalOk").addEventListener("click", () => { modal.remove(); onConfirm(); });
    modal.addEventListener("click", (e) => { if (e.target === modal) modal.remove(); });
  }

  // Shared WiFi UI refs
  const scanBtn = document.getElementById("scanBtn");
  const connectBtn = document.getElementById("connectBtn");
  const ssidInput = document.getElementById("ssidInput");
  const passInput = document.getElementById("passInput");
  const scanCard = document.getElementById("scanCard");
  const netList = document.getElementById("netList");
  const scanCount = document.getElementById("scanCount");

  if (scanBtn) {
    let scanActive = false;
    let scanAttempts = 0;

    const updateScanStatus = (msg, count = null) => {
      if (scanBtn) { scanBtn.disabled = true; scanBtn.textContent = msg; }
      if (count !== null && scanCount) scanCount.textContent = `(${count})`;
    };

    const renderScanResults = (nets) => {
      if (!netList) return;
      if (nets.length === 0) {
        netList.innerHTML = `<p class='text-dim' style='padding:10px 0;text-align:center;'>No networks found nearby.</p>`;
        return;
      }
      netList.innerHTML = nets.sort((a, b) => b.rssi - a.rssi).map(n => {
        const strength = n.rssi > -55 ? 4 : n.rssi > -67 ? 3 : n.rssi > -75 ? 2 : 1;
        const bars = Array.from({length:4},(_,i)=>`<span style="width:3px;height:${5+i*4}px;background:${i<strength?'var(--accent,#00ff00)':'rgba(255,255,255,0.12)'};border-radius:1px;display:inline-block;vertical-align:bottom;margin:0 1px;"></span>`).join('');
        return `
          <div class="net-item" data-ssid="${esc(n.ssid)}" style="display:flex;align-items:center;gap:8px;">
            <div style="display:flex;align-items:flex-end;gap:1px;">${bars}</div>
            <span class="net-name" style="flex:1;">${esc(n.ssid)}</span>
            <span class="net-rssi" style="font-size:0.78rem;color:#667;">${n.rssi} dBm ${n.secure ? '🔒' : ''}</span>
          </div>`;
      }).join("");
      netList.querySelectorAll(".net-item").forEach(item => {
        item.addEventListener("click", () => {
          if (ssidInput) ssidInput.value = item.dataset.ssid;
          if (passInput) passInput.focus();
          if (scanCard) scanCard.style.display = "none";
        });
      });
    };

    const pollScan = async () => {
      try {
        updateScanStatus("Reading results...");
        const res = await apiGet("/api/scan");
        if (res && res.scanning) {
          scanAttempts++;
          if (scanAttempts > 20) {
            showToast("Scan timed out — try again", true);
            if (scanCard) scanCard.style.display = "block";
            if (netList) netList.innerHTML = `<p class='text-dim' style='padding:12px;text-align:center;'>Scan timed out. Please try again.</p>`;
            scanBtn.disabled = false;
            scanBtn.textContent = "Scan Again";
            scanActive = false;
            return;
          }
          updateScanStatus(`Scanning... (${scanAttempts})`);
          if (netList) {
            netList.innerHTML = `
              <div style="display:flex;flex-direction:column;align-items:center;gap:10px;padding:24px 10px;color:var(--text-dim);font-family:'JetBrains Mono',monospace;font-size:0.82rem;">
                <div style="width:28px;height:28px;border:2px solid var(--accent);border-top-color:transparent;border-radius:50%;animation:spin 0.8s linear infinite;"></div>
                <div style="color:var(--accent);font-weight:600;">Searching 2.4 GHz spectrum... (${scanAttempts})</div>
                <div style="font-size:0.75rem;color:#889;">Scanning all 14 wireless channels for AP beacons</div>
              </div>`;
          }
          setTimeout(pollScan, 900);
          return;
        }
        const nets = Array.isArray(res) ? res : [];
        if (scanCount) scanCount.textContent = `(${nets.length})`;
        renderScanResults(nets);
        if (scanCard) scanCard.style.display = "block";
        scanBtn.disabled = false;
        scanBtn.textContent = "Scan Again";
        scanActive = false;
      } catch (e) {
        showToast("Wi-Fi scan failed: " + e.message, true);
        if (scanCard) scanCard.style.display = "block";
        if (netList) netList.innerHTML = `<p class='text-dim' style='padding:12px;text-align:center;'>Scan error: ${esc(e.message)}</p>`;
        scanBtn.disabled = false;
        scanBtn.textContent = "Scan";
        scanActive = false;
      }
    };

    scanBtn.addEventListener("click", async () => {
      if (scanActive) return;
      scanActive = true;
      scanAttempts = 0;
      if (scanCard) scanCard.style.display = "block";
      if (netList) {
        netList.innerHTML = `
          <div style="display:flex;flex-direction:column;align-items:center;gap:10px;padding:24px 10px;color:var(--text-dim);font-family:'JetBrains Mono',monospace;font-size:0.82rem;">
            <div style="width:28px;height:28px;border:2px solid var(--accent);border-top-color:transparent;border-radius:50%;animation:spin 0.8s linear infinite;"></div>
            <div style="color:var(--accent);font-weight:600;">Initializing Wi-Fi Scan...</div>
            <div style="font-size:0.75rem;color:#889;">Requesting radio channel sweep from ESP32</div>
          </div>`;
      }
      updateScanStatus("Starting scan...", "...");
      try {
        await apiGet("/api/scan"); // kick off async scan
        updateScanStatus("Scanning... (1)");
        setTimeout(pollScan, 1100);
      } catch (e) {
        showToast("Wi-Fi scan failed: " + e.message, true);
        if (netList) netList.innerHTML = `<p class='text-dim' style='padding:12px;text-align:center;'>Scan error: ${esc(e.message)}</p>`;
        scanBtn.disabled = false;
        scanBtn.textContent = "Scan";
        scanActive = false;
      }
    });
  }

  if (connectBtn) {
    connectBtn.addEventListener("click", () => {
      const ssid = ssidInput?.value.trim();
      const pass = passInput?.value || "";
      if (!ssid) { showToast("Enter an SSID first", true); return; }
      startWifiConnectFlow(ssid, pass);
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
  function initRunwayButtons() {
    const pills = document.getElementById("runwayPills");
    if (!pills) return;
    pills.querySelectorAll(".rwy-btn").forEach(btn => {
      btn.addEventListener("click", () => {
        pills.querySelectorAll(".rwy-btn").forEach(b => b.classList.remove("active"));
        btn.classList.add("active");
        window.selectedRunwayHdg = parseInt(btn.dataset.hdg, 10) || 90;
        if (lastWeatherStatus) updateWeatherUI(lastWeatherStatus);
      });
    });
  }

  initRunwayButtons();

  // Immediate Captive Portal Detection & Setup Auto-Navigation
  const isCaptiveHost = 
    window.location.hostname === "192.168.4.1" ||
    window.location.hostname.includes("connecttest") ||
    window.location.hostname.includes("gstatic") ||
    window.location.hostname.includes("apple.com") ||
    window.location.search.includes("setup");

  if (isCaptiveHost && !window.captivePortalRedirected) {
    window.captivePortalRedirected = true;
    setTimeout(() => {
      const settingsTabBtn = document.querySelector('.nav-tab[data-tab="settings"]');
      if (settingsTabBtn) settingsTabBtn.click();
      setTimeout(() => {
        const wifiCard = document.getElementById("wifiSetupCard");
        if (wifiCard) {
          wifiCard.scrollIntoView({ behavior: "smooth", block: "start" });
          wifiCard.style.transition = "box-shadow 0.4s ease";
          wifiCard.style.boxShadow = "0 0 0 2px var(--accent), 0 0 32px var(--accent-glow)";
          setTimeout(() => { wifiCard.style.boxShadow = ""; }, 3500);
          const sb = document.getElementById("scanBtn");
          if (sb && !sb.disabled) setTimeout(() => sb.click(), 400);
        }
      }, 250);
    }, 200);
  }

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
