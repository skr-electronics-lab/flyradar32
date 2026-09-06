(() => {
  "use strict";

  let sessionPin = "";

  async function apiGet(url) {
    const res = await fetch(url, { headers: pinHeaders() });
    if (res.status === 401) {
      const ok = await askForPin();
      if (!ok) throw new Error("PIN required");
      return apiGet(url);
    }
    return res.json();
  }

  async function apiPost(url, body) {
    const res = await fetch(url, {
      method: "POST",
      headers: Object.assign({ "Content-Type": "application/json" }, pinHeaders()),
      body: JSON.stringify(body || {})
    });
    if (res.status === 401) {
      const ok = await askForPin();
      if (!ok) throw new Error("PIN required");
      return apiPost(url, body);
    }
    return res.json();
  }

  function pinHeaders() {
    return sessionPin ? { "X-Config-Pin": sessionPin } : {};
  }

  function askForPin() {
    return new Promise(resolve => {
      const p = window.prompt("Enter the device configuration PIN:");
      if (p === null) { resolve(false); return; }   // cancel: stop retrying, no 401 loop
      sessionPin = p;
      resolve(true);
    });
  }

  // Toast Notifications
  const toastEl = document.getElementById("toast");
  let toastTimeout;
  function showToast(message, type = "success") {
    toastEl.textContent = message;
    toastEl.className = type === "error" ? "error show" : "show";
    clearTimeout(toastTimeout);
    toastTimeout = setTimeout(() => {
      toastEl.classList.remove("show");
    }, 3000);
  }

  // Tabs Logic
  document.querySelectorAll(".tab").forEach(tab => {
    tab.addEventListener("click", () => {
      document.querySelectorAll(".tab").forEach(t => t.classList.remove("active"));
      document.querySelectorAll(".panel").forEach(p => p.classList.remove("active"));
      tab.classList.add("active");
      document.getElementById("panel-" + tab.dataset.tab).classList.add("active");
    });
  });

  const wifiStateEl = document.getElementById("wifiState");
  const wifiIpEl = document.getElementById("wifiIp");
  const wifiErrorRow = document.getElementById("wifiErrorRow");
  const wifiErrorEl = document.getElementById("wifiError");
  const mdnsAddrEl = document.getElementById("mdnsAddr");
  const fwNameEl = document.getElementById("fwName");
  const fwVersionEl = document.getElementById("fwVersion");
  const dataStatusEl = document.getElementById("dataStatus");

  async function refreshStatus() {
    try {
      const s = await apiGet("/api/status");

      wifiStateEl.textContent = {
        "ap_mode": "Setup mode (AP)",
        "connecting": "Connecting...",
        "connected": "Connected",
        "failed": "Failed"
      }[s.wifiState] || s.wifiState;
      
      wifiStateEl.className = "status-value " + (s.wifiState === "connected" ? "connected" : "disconnected");
      wifiIpEl.textContent = s.wifiState === "connected" ? s.staIp : s.apIp;
      mdnsAddrEl.textContent = s.mdns || "-";
      fwNameEl.textContent = s.fwName || "-";
      fwVersionEl.textContent = s.fwVersion || "-";
      
      dataStatusEl.textContent = s.lastFetchOk ? "Receiving Data" : s.fetchInProgress ? "Fetching..." : "Offline";
      dataStatusEl.className = "status-value " + (s.lastFetchOk ? "connected" : "disconnected");

      if (s.lastError) {
        wifiErrorRow.style.display = "flex";
        wifiErrorEl.textContent = s.lastError;
      } else {
        wifiErrorRow.style.display = "none";
      }

      const wifiSetupCards = document.getElementById("wifiSetupCards");
      const showWifiSetupBtnRow = document.getElementById("showWifiSetupBtnRow");
      if (s.wifiState === "connected" && !window.wifiSetupForcedOpen) {
          wifiSetupCards.style.display = "none";
          showWifiSetupBtnRow.style.display = "flex";
      } else {
          wifiSetupCards.style.display = "block";
          showWifiSetupBtnRow.style.display = "none";
      }
    } catch (e) {
      wifiStateEl.textContent = "Unreachable";
      wifiStateEl.className = "status-value disconnected";
    }
  }

  refreshStatus();
  setInterval(refreshStatus, 4000);

  const netList = document.getElementById("netList");
  const ssidInput = document.getElementById("ssidInput");
  const passInput = document.getElementById("passInput");
  const scanCount = document.getElementById("scanCount");

  async function doScan() {
    netList.innerHTML = '<div style="color:var(--text-muted); font-size:14px;">Scanning...</div>';
    scanCount.textContent = "";
    try {
      const nets = await apiGet("/api/scan");
      netList.innerHTML = "";
      nets.sort((a, b) => b.rssi - a.rssi);
      scanCount.textContent = `(${nets.length} found)`;
      if (nets.length === 0) {
        netList.innerHTML = '<div style="color:var(--text-muted); font-size:14px;">No networks found</div>';
        return;
      }
      nets.forEach(n => {
        const div = document.createElement("div");
        div.style = "padding: 10px; border-bottom: 1px solid var(--border); cursor: pointer; display: flex; justify-content: space-between;";
        div.innerHTML = `<span style="font-weight:500;">${escapeHtml(n.ssid)}</span>
          <span style="color:var(--text-muted); font-size:13px;">${n.rssi} dBm ${n.secure ? "\u{1F512}" : ""}</span>`;
        div.addEventListener("click", () => {
          ssidInput.value = n.ssid;
          passInput.focus();
        });
        netList.appendChild(div);
      });
    } catch (e) {
      netList.innerHTML = '<div style="color:var(--danger); font-size:14px;">Scan failed</div>';
      scanCount.textContent = "";
    }
  }

  document.getElementById("scanBtn").addEventListener("click", doScan);
  
  // Show Wi-Fi setup if they click the button
  document.getElementById("showWifiSetupBtn").addEventListener("click", () => {
      window.wifiSetupForcedOpen = true;
      document.getElementById("wifiSetupCards").style.display = "block";
      document.getElementById("showWifiSetupBtnRow").style.display = "none";
      doScan();
  });

  // NB: no auto-scan on page load — a scan blocks the device's network stack
  // for 2-4s and briefly disrupts the radar data feed. Scan is explicit.

  document.getElementById("connectBtn").addEventListener("click", async () => {
    const ssid = ssidInput.value.trim();
    if (!ssid) { showToast("Enter a network name.", "error"); return; }
    try {
      await apiPost("/api/wifi/connect", { ssid, password: passInput.value });
      showToast("Connecting... check status above.");
      setTimeout(refreshStatus, 3000);
    } catch (e) {
      showToast("Request failed.", "error");
    }
  });

  const latInput = document.getElementById("latInput");
  const lonInput = document.getElementById("lonInput");
  const pasteCoords = document.getElementById("pasteCoords");

  // Handle Google Maps Paste (e.g. 23.1841228, 87.8666717)
  pasteCoords.addEventListener("input", (e) => {
      const val = e.target.value.trim();
      const parts = val.split(",");
      if (parts.length === 2) {
          const lat = parseFloat(parts[0]);
          const lon = parseFloat(parts[1]);
          if (!isNaN(lat) && !isNaN(lon)) {
              latInput.value = lat.toFixed(6);
              lonInput.value = lon.toFixed(6);
              showToast("Coordinates auto-filled!");
              e.target.value = ""; // Clear after paste
          }
      }
  });

  document.getElementById("geoBtn").addEventListener("click", async () => {
    try {
      const res = await fetch("http://ip-api.com/json/");
      const d = await res.json();
      if (d.status === "success") {
        latInput.value = d.lat;
        lonInput.value = d.lon;
        showToast(`Location found: ${d.city}, ${d.countryCode}. Click Save.`);
      } else {
        showToast("IP lookup failed: " + d.message, "error");
      }
    } catch (e) {
      showToast("Could not get location: " + e.message, "error");
    }
  });

  document.getElementById("saveLocationBtn").addEventListener("click", async () => {
    const lat = parseFloat(latInput.value);
    const lon = parseFloat(lonInput.value);
    if (isNaN(lat) || isNaN(lon) || lat < -90 || lat > 90 || lon < -180 || lon > 180) {
      showToast("Enter valid latitude and longitude.", "error");
      return;
    }
    try {
      await apiPost("/api/settings/location", { lat, lon });
      showToast("Location saved successfully.");
    } catch (e) {
      showToast("Save failed.", "error");
    }
  });

  const zoomSelect = document.getElementById("zoomSelect");
  const refreshSelect = document.getElementById("refreshSelect");
  const labelsSelect = document.getElementById("labelsSelect");
  const iconSelect = document.getElementById("iconSelect");
  const sweepToggle = document.getElementById("sweepToggle");

  document.getElementById("saveDisplayBtn").addEventListener("click", async () => {
    try {
      await apiPost("/api/settings/display", {
        zoomLevel: +zoomSelect.value,
        labelsMode: +labelsSelect.value,
        aircraftIcon: +iconSelect.value,
        showSweepAnim: sweepToggle.checked
      });
      showToast("Display settings saved.");
    } catch (e) {
      showToast("Save failed.", "error");
    }
  });

  document.getElementById("saveRefreshBtn").addEventListener("click", async () => {
    try {
      await apiPost("/api/settings/providers", {
        refreshInterval: +refreshSelect.value
      });
      showToast("Refresh interval saved.");
    } catch (e) {
      showToast("Save failed.", "error");
    }
  });

  // Theme picker (was dead UI — no handlers at all)
  const themeRow = document.getElementById("themeRow");
  let selectedTheme = 0;
  themeRow.querySelectorAll(".theme-swatch").forEach(sw => {
    sw.addEventListener("click", () => {
      selectedTheme = +sw.dataset.theme;
      themeRow.querySelectorAll(".theme-swatch").forEach(x =>
        x.classList.toggle("selected", x === sw));
    });
  });

  document.getElementById("saveThemeBtn").addEventListener("click", async () => {
    try {
      await apiPost("/api/settings/display", { theme: selectedTheme });
      showToast("Theme saved.");
    } catch (e) {
      showToast("Save failed.", "error");
    }
  });

  // Radar customization toggles (were dead UI — no handlers at all)
  const compassToggle = document.getElementById("compassToggle");
  const rangeLabelsToggle = document.getElementById("rangeLabelsToggle");
  const trailToggle = document.getElementById("trailToggle");

  document.getElementById("saveCustomizationBtn").addEventListener("click", async () => {
    try {
      await apiPost("/api/settings/display", {
        showCompass: compassToggle.checked,
        showRangeLabels: rangeLabelsToggle.checked,
        showTrail: trailToggle.checked
      });
      showToast("Customization saved.");
    } catch (e) {
      showToast("Save failed.", "error");
    }
  });

  document.getElementById("savePinBtn").addEventListener("click", async () => {
    const pin = document.getElementById("pinInput").value.trim();
    try {
      await apiPost("/api/settings/pin", { pin });
      sessionPin = pin;
      showToast(pin ? "PIN updated successfully." : "PIN disabled.");
    } catch (e) {
      showToast("Save failed.", "error");
    }
  });

  document.getElementById("disconnectBtn").addEventListener("click", async () => {
    if (!window.confirm("Forget the current Wi-Fi network and restart? The device will boot in setup mode.")) return;
    try {
      await apiPost("/api/wifi/disconnect", {});
      showToast("Disconnecting and restarting...");
    } catch (e) {
      showToast("Request failed.", "error");
    }
  });

  document.getElementById("resetBtn").addEventListener("click", async () => {
    if (!window.confirm("This erases all stored settings and restarts the device. Continue?")) return;
    try {
      await apiPost("/api/factory-reset", {});
      showToast("Resetting device to factory defaults...");
    } catch (e) {
      showToast("Reset request failed.", "error");
    }
  });

  async function loadSettings() {
    try {
      const s = await apiGet("/api/settings");
      latInput.value = s.lat;
      lonInput.value = s.lon;
      zoomSelect.value = s.zoomLevel;
      if (s.labelsMode !== undefined) labelsSelect.value = s.labelsMode;
      if (s.aircraftIcon !== undefined) iconSelect.value = s.aircraftIcon;
      if(s.refreshInterval) refreshSelect.value = s.refreshInterval;
      sweepToggle.checked = s.showSweepAnim;
      if (s.theme !== undefined) {
        selectedTheme = s.theme;
        themeRow.querySelectorAll(".theme-swatch").forEach(x =>
          x.classList.toggle("selected", +x.dataset.theme === s.theme));
      }
      if (s.showCompass !== undefined) compassToggle.checked = s.showCompass;
      if (s.showRangeLabels !== undefined) rangeLabelsToggle.checked = s.showRangeLabels;
      if (s.showTrail !== undefined) trailToggle.checked = s.showTrail;
    } catch (e) {}
  }
  loadSettings();

  function escapeHtml(str) {
    return str.replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  }
})();
