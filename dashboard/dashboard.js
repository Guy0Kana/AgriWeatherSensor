/* ===================== 1. CONFIG ===================== */
  const CONFIG = {
    supabaseUrl: "https://wubufrivwsmjgqjnavew.supabase.co",
    supabaseKey: "sb_publishable_DUf8fAVckXvGMFwl7d5VeA_7pFBVNNn", // publishable key only, never service_role
    pollMs: 5000,
    initialRows: 200,
    rawMax: 4095,
    maxPoints: 60,
    maxLogRows: 400,
    staleAfterMs: 5 * 60 * 1000,
    offlineAfterMs: 15 * 60 * 1000,
  };
  const TH = {
    soil: { critLow: 25, low: 35, high: 70, critHigh: 85 },
    airT: { low: 10, high: 33, critHigh: 38 },
    hum: { low: 35, high: 85 },
  };

  /* ===================== 2. THEME ===================== */
  const $ = (id) => document.getElementById(id);
  const cssVar = (n) =>
    getComputedStyle(document.documentElement).getPropertyValue(n).trim();
  function toggleTheme() {
    const next =
      document.documentElement.getAttribute("data-theme") === "dark"
        ? "light"
        : "dark";
    document.documentElement.setAttribute("data-theme", next);
    try {
      localStorage.setItem("agrisense_theme", next);
    } catch (e) {}
    if (window.trendChart) themeChart();
  }
  $("themeBtn").onclick = toggleTheme;
  $("themeBtn2").onclick = toggleTheme;

  /* ===================== 3. AUTH (Supabase Auth REST) ===================== */
  const AUTH_KEY = "agrisense_session";
  function getSession() {
    try {
      return JSON.parse(localStorage.getItem(AUTH_KEY));
    } catch (e) {
      return null;
    }
  }
  function saveSession(s) {
    s.expires_at =
      s.expires_at ||
      Math.floor(Date.now() / 1000) + (s.expires_in || 3600);
    try {
      localStorage.setItem(AUTH_KEY, JSON.stringify(s));
    } catch (e) {}
    return s;
  }
  async function authCall(path, body) {
    const res = await fetch(`${CONFIG.supabaseUrl}/auth/v1/${path}`, {
      method: "POST",
      headers: {
        apikey: CONFIG.supabaseKey,
        "Content-Type": "application/json",
      },
      body: JSON.stringify(body),
    });
    const data = await res.json().catch(() => ({}));
    if (!res.ok)
      throw new Error(
        data.error_description ||
          data.msg ||
          data.message ||
          "Sign-in failed",
      );
    return data;
  }
  async function accessToken() {
    let s = getSession();
    if (!s) return null;
    if (s.expires_at - 60 < Date.now() / 1000) {
      try {
        s = saveSession(
          await authCall("token?grant_type=refresh_token", {
            refresh_token: s.refresh_token,
          }),
        );
      } catch (e) {
        logout();
        return null;
      }
    }
    return s.access_token;
  }
  /* Session timeout: leave the dashboard (tab hidden or closed) for 3 minutes and you must sign in again */
  const AWAY_MS = 3 * 60 * 1000;
  const SEEN_KEY = "agrisense_last_seen",
    NOTICE_KEY = "agrisense_notice";
  function touchSeen() {
    try {
      localStorage.setItem(SEEN_KEY, String(Date.now()));
    } catch (e) {}
  }
  function awayTooLong() {
    let t = 0;
    try {
      t = +localStorage.getItem(SEEN_KEY) || 0;
    } catch (e) {}
    return t > 0 && Date.now() - t >= AWAY_MS;
  }
  function expireSession() {
    try {
      localStorage.setItem(
        NOTICE_KEY,
        "You were away for more than 3 minutes, so you were signed out. Sign in again to continue.",
      );
    } catch (e) {}
    logout();
  }
  function watchAway() {
    touchSeen();
    // Heartbeat while the dashboard is on screen; while it is hidden, check if the 3 minutes are up
    setInterval(() => {
      if (!document.hidden) touchSeen();
      else if (awayTooLong()) expireSession();
    }, 10000);
    document.addEventListener("visibilitychange", () => {
      if (document.hidden) touchSeen();
      else if (awayTooLong()) expireSession();
      else touchSeen();
    });
    addEventListener("pagehide", touchSeen);
  }
  function logout() {
    try {
      localStorage.removeItem(AUTH_KEY);
      localStorage.removeItem(SEEN_KEY);
    } catch (e) {}
    location.reload();
  }
  $("logoutBtn").onclick = () => logout();
  $("loginForm").onsubmit = async (e) => {
    e.preventDefault();
    const btn = $("loginBtn"),
      err = $("loginErr");
    err.textContent = "";
    err.className = "";
    btn.disabled = true;
    btn.textContent = "Signing in…";
    try {
      saveSession(
        await authCall("token?grant_type=password", {
          email: $("email").value.trim(),
          password: $("password").value,
        }),
      );
      showApp();
    } catch (ex) {
      err.textContent =
        ex.message === "Invalid login credentials"
          ? "Email or password is incorrect. Check both and try again."
          : ex.message;
    } finally {
      btn.disabled = false;
      btn.textContent = "Sign in";
    }
  };

  /* ===================== 4. HELPERS ===================== */
  const TZ = "Africa/Nairobi";
  function fmtTime(ms, sec = true) {
    const o = {
      timeZone: TZ,
      hour: "2-digit",
      minute: "2-digit",
      hour12: false,
    };
    if (sec) o.second = "2-digit";
    return new Date(ms).toLocaleTimeString("en-GB", o);
  }
  function fmtDateTime(ms) {
    return new Date(ms).toLocaleString("en-GB", {
      timeZone: TZ,
      day: "2-digit",
      month: "short",
      year: "numeric",
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
      hour12: false,
    });
  }
  function fmtISOLocal(ms) {
    return (
      new Date(ms)
        .toLocaleString("sv-SE", { timeZone: TZ })
        .replace(" ", "T") + "+03:00"
    );
  }
  function fmtDateLocal(ms) {
    return new Date(ms).toLocaleDateString("sv-SE", { timeZone: TZ });
  }
  function esc(s) {
    return String(s).replace(
      /[&<>"']/g,
      (c) =>
        ({
          "&": "&amp;",
          "<": "&lt;",
          ">": "&gt;",
          '"': "&quot;",
          "'": "&#39;",
        })[c],
    );
  }
  function num(v) {
    return v === undefined || v === null || isNaN(v) ? null : Number(v);
  }
  function fix(v, d) {
    const n = num(v);
    return n === null ? "--" : n.toFixed(d);
  }

  /* ===================== 5. STATE + DATA ===================== */
  const state = {
    nodes: {},
    activeNode: null,
    log: [],
    advisories: [],
    lastId: 0,
    loaded: false,
  };
  function setConn(status, text) {
    $("connBadge").className =
      "badge " +
      (status === "live" ? "live" : status === "down" ? "down" : "");
    $("connText").textContent = text;
  }
  async function getDecryptedReadings(afterId, limit) {
    const token = await accessToken();
    if (!token) throw new Error("Not signed in");
    const res = await fetch(
      `${CONFIG.supabaseUrl}/functions/v1/decrypt-readings`,
      {
        method: "POST",
        headers: {
          apikey: CONFIG.supabaseKey,
          Authorization: "Bearer " + token,
          "Content-Type": "application/json",
        },
        body: JSON.stringify({ afterId, limit }),
      },
    );
    if (res.status === 401) {
      logout();
      throw new Error("Session expired");
    }
    const result = await res.json().catch(() => ({}));
    if (!res.ok)
      throw new Error(
        result.error || `Decryption service returned HTTP ${res.status}`,
      );
    if (!Array.isArray(result.rows))
      throw new Error("Decryption service returned an invalid response");
    return result.rows;
  }
  function mapRow(row) {
    const when = Date.parse(row.recorded_at || row.device_time);
    return {
      ...row,
      ts: isFinite(when)
        ? Math.floor(when / 1000)
        : Math.floor(Date.now() / 1000),
    };
  }
  let polling = false;
  async function poll() {
    if (polling) return;
    polling = true;
    try {
      const first = !state.loaded;
      const rows = await getDecryptedReadings(
        first ? null : state.lastId,
        first ? CONFIG.initialRows : 500,
      );
      state.loaded = true;
      setConn("live", "Live");
      if (rows.length) ingestRows(rows.map(mapRow));
      else if (first && !state.log.length)
        $("logBody").innerHTML =
          '<tr><td colspan="8" class="empty">No readings to show yet. If your sensor is sending data, ask the system administrator to check access settings.</td></tr>';
    } catch (e) {
      console.error("Supabase error:", e);
      setConn("down", "Connection problem");
    } finally {
      polling = false;
    }
  }
  function ingestRows(rows) {
    const newest = {};
    rows.forEach((r) => {
      const id = r.device_id || "unknown";
      if (!state.nodes[id])
        state.nodes[id] = { history: [], last: null, count: 0 };
      const node = state.nodes[id];
      node.last = r;
      node.count++;
      node.history.push(r);
      if (node.history.length > CONFIG.maxPoints) node.history.shift();
      state.log.unshift(r);
      if (state.log.length > CONFIG.maxLogRows) state.log.pop();
      if (r.id != null && r.id > state.lastId) state.lastId = r.id;
      newest[id] = r;
    });
    if (!state.activeNode)
      state.activeNode = Object.keys(state.nodes).sort()[0] || null;
    Object.values(newest).forEach(evaluateAdvisories);
    renderChips();
    renderActive();
    renderLog();
    renderLastBadge();
  }

  /* ===================== 6. ADVISORIES ===================== */
  function evaluateAdvisories(r) {
    const out = [],
      when = r.ts * 1000;
    const push = (level, emoji, title, detail) =>
      out.push({
        level,
        emoji,
        title,
        detail,
        ts: when,
        node: r.device_id,
      });
    const sm = num(r.soil_pct);
    if (sm !== null) {
      if (sm < TH.soil.critLow)
        push(
          "critical",
          "🚨",
          "Irrigate immediately",
          `Soil moisture at ${sm.toFixed(0)}% is below the ${TH.soil.critLow}% wilting threshold. Irrigate now, early morning or after 4 pm to cut evaporation losses.`,
        );
      else if (sm < TH.soil.low)
        push(
          "warning",
          "💧",
          "Irrigation recommended within 24 h",
          `Soil moisture is ${sm.toFixed(0)}%, approaching stress levels for maize at this growth stage. Prepare to irrigate.`,
        );
      else if (sm > TH.soil.critHigh)
        push(
          "critical",
          "🌊",
          "Waterlogging risk: check drainage",
          `Soil moisture at ${sm.toFixed(0)}% is above ${TH.soil.critHigh}%. Open drainage furrows; prolonged saturation causes root rot.`,
        );
      else
        push(
          "good",
          "✅",
          "Soil moisture optimal",
          `${sm.toFixed(0)}% is within the ideal 35–70% band. No irrigation needed.`,
        );
    }
    const at = num(r.air_temp);
    if (at !== null) {
      if (at >= TH.airT.critHigh)
        push(
          "critical",
          "🔥",
          "Heat stress alert",
          `Air temperature is ${at.toFixed(1)} °C. Heat stress reduces pollination in maize. Irrigate in the evening and consider mulching.`,
        );
      else if (at <= TH.airT.low)
        push(
          "warning",
          "🥶",
          "Cold stress risk",
          `Air temperature at ${at.toFixed(1)} °C may slow germination and early growth. Monitor young seedlings.`,
        );
    }
    const hum = num(r.humidity);
    if (
      hum !== null &&
      at !== null &&
      hum > TH.hum.high &&
      at >= 18 &&
      at <= 30
    )
      push(
        "warning",
        "🍄",
        "Fungal disease risk elevated",
        `Humidity ${hum.toFixed(0)}% with ${at.toFixed(0)} °C creates favourable conditions for late blight and leaf rust. Scout the field within 48 h.`,
      );
    out.forEach((a) => {
      const i = state.advisories.findIndex(
        (x) => x.node === a.node && x.title === a.title,
      );
      if (i >= 0) state.advisories[i] = a;
      else state.advisories.unshift(a);
    });
    state.advisories = state.advisories.slice(0, 40);
    renderAdvisories();
  }

  /* ===================== 7. RENDER ===================== */
  function nodeStatus(node) {
    if (!node.last) return "offline";
    const age = Date.now() - node.last.ts * 1000;
    return age > CONFIG.offlineAfterMs
      ? "offline"
      : age > CONFIG.staleAfterMs
        ? "stale"
        : "live";
  }
  function renderChips() {
    const row = $("chipRow"),
      ids = Object.keys(state.nodes).sort();
    if (!ids.length) return;
    row.innerHTML = ids
      .map(
        (id) =>
          `<div class="chip ${nodeStatus(state.nodes[id])} ${id === state.activeNode ? "active" : ""}" data-node="${esc(id)}"><span class="sdot"></span> ${esc(id)}</div>`,
      )
      .join("");
    row.querySelectorAll(".chip[data-node]").forEach((el) => {
      el.onclick = () => {
        state.activeNode = el.dataset.node;
        renderChips();
        renderActive();
      };
    });
  }
  function renderLastBadge() {
    const all = Object.values(state.nodes)
      .map((n) => n.last)
      .filter(Boolean);
    if (!all.length) return;
    const newest = all.reduce((a, b) => (b.ts > a.ts ? b : a));
    $("lastText").textContent =
      "Last reading " + fmtDateTime(newest.ts * 1000);
  }
  function setKPI(elId, value, decimals, colorFn) {
    const el = $(elId),
      unit = el.querySelector(".unit"),
      u = unit ? unit.outerHTML : "";
    if (value === null) {
      el.innerHTML = "--" + u;
      return;
    }
    el.innerHTML = value.toFixed(decimals) + u;
    el.className = "value " + (colorFn ? colorFn(value) : "");
  }
  function renderActive() {
    const id = state.activeNode,
      node = state.nodes[id];
    $("chartNode").textContent = id || "no device";
    if (!node || !node.last) return;
    const r = node.last;

    const sm = num(r.soil_pct),
      C = 2 * Math.PI * 78,
      arc = $("gaugeArc");
    if (sm !== null) {
      arc.setAttribute(
        "stroke-dashoffset",
        C - (C * Math.max(0, Math.min(100, sm))) / 100,
      );
      const col =
        sm < TH.soil.critLow
          ? "var(--red)"
          : sm < TH.soil.low
            ? "var(--amber)"
            : sm > TH.soil.critHigh
              ? "var(--blue)"
              : "var(--green)";
      arc.style.stroke = col;
      $("gaugeVal").textContent = sm.toFixed(0);
      $("gaugeVal").style.color = col;
    }
    setKPI("kSoil", sm, 0, (v) =>
      v < TH.soil.critLow
        ? "bad"
        : v < TH.soil.low
          ? "warn"
          : v > TH.soil.critHigh
            ? "info"
            : "ok",
    );
    $("bSoil").style.width = (sm === null ? 0 : Math.min(100, sm)) + "%";
    const at = num(r.air_temp);
    setKPI("kAirT", at, 1, (v) =>
      v >= TH.airT.critHigh ? "bad" : v >= TH.airT.high ? "warn" : "ok",
    );
    $("bAirT").style.width =
      (at === null ? 0 : Math.min(100, (at / 50) * 100)) + "%";
    const hm = num(r.humidity);
    setKPI("kHum", hm, 1, (v) =>
      v > TH.hum.high || v < TH.hum.low ? "warn" : "ok",
    );
    $("bHum").style.width = (hm === null ? 0 : Math.min(100, hm)) + "%";

    // Vapour pressure deficit: how strongly the air pulls water out of the crop
    const vpd =
      at !== null && hm !== null
        ? 0.6108 * Math.exp((17.27 * at) / (at + 237.3)) * (1 - hm / 100)
        : null;
    setKPI("kVpd", vpd, 2, (v) =>
      v < 0.4 ? "info" : v <= 1.2 ? "ok" : v <= 1.6 ? "warn" : "bad",
    );
    $("bVpd").style.width =
      (vpd === null ? 0 : Math.min(100, (vpd / 3) * 100)) + "%";

    $("plant").textContent =
      sm === null
        ? "🌱"
        : sm < TH.soil.critLow
          ? "🥀"
          : sm < TH.soil.low
            ? "🌾"
            : sm > TH.soil.critHigh
              ? "🌊"
              : "🌽";
    if (sm !== null)
      $("zoneMark").style.left = Math.max(0, Math.min(100, sm)) + "%";

    // One-line field status for the farmer
    let st = [
      "ok",
      "🌽",
      "Crop is comfortable",
      "Soil water, temperature and humidity are in a good range.",
    ];
    if (sm !== null && sm < TH.soil.critLow)
      st = [
        "bad",
        "🥀",
        "Irrigate now",
        `Soil water is ${sm.toFixed(0)}%, below the wilting threshold.`,
      ];
    else if (sm !== null && sm > TH.soil.critHigh)
      st = [
        "info",
        "🌊",
        "Check drainage",
        `Soil water is ${sm.toFixed(0)}%. Roots risk rotting if it stays this wet.`,
      ];
    else if (at !== null && at >= TH.airT.critHigh)
      st = [
        "bad",
        "🔥",
        "Heat stress",
        `${at.toFixed(1)} °C can hurt pollination. Irrigate in the evening.`,
      ];
    else if (sm !== null && sm < TH.soil.low)
      st = [
        "warn",
        "💧",
        "Irrigate within 24 hours",
        `Soil water is ${sm.toFixed(0)}% and falling toward stress levels.`,
      ];
    else if (
      hm !== null &&
      at !== null &&
      hm > TH.hum.high &&
      at >= 18 &&
      at <= 30
    )
      st = [
        "warn",
        "🍄",
        "Scout for fungal disease",
        "Warm, humid air favours blight and leaf rust.",
      ];
    $("status").className = "status " + st[0];
    $("sIco").textContent = st[1];
    $("sTitle").textContent = st[2];
    $("sText").textContent = st[3];

    // "Today in the field" card and range tiles (from the last 60 readings)
    const vals = (k) =>
      node.history.map((x) => num(x[k])).filter((v) => v !== null);
    const tv = vals("air_temp"),
      sv = vals("soil_pct");
    $("tMin").textContent = tv.length
      ? Math.min(...tv).toFixed(0) + "°"
      : "--";
    $("tMax").textContent = tv.length
      ? Math.max(...tv).toFixed(0) + "°"
      : "--";
    $("tSoil").textContent = sv.length
      ? (sv.reduce((a, b) => a + b, 0) / sv.length).toFixed(0) + "%"
      : "--";
    $("tIdeal").textContent = sv.length
      ? Math.round(
          (sv.filter((v) => v >= TH.soil.low && v <= TH.soil.high).length /
            sv.length) *
            100,
        ) + "%"
      : "--";
    $("wxT").textContent = at === null ? "--" : at.toFixed(0) + "°C";
    $("wxH").textContent = hm === null ? "--" : hm.toFixed(0) + "%";
    $("wxV").textContent = vpd === null ? "--" : vpd.toFixed(2) + " kPa";
    $("wxD").textContent =
      at === null
        ? "No data yet"
        : (at >= TH.airT.high
            ? "Hot"
            : at <= TH.airT.low
              ? "Cold"
              : "Mild") +
          (hm === null
            ? ""
            : hm > TH.hum.high
              ? " and humid"
              : hm < TH.hum.low
                ? " and dry"
                : "");

    const h = node.history;
    trendChart.data.labels = h.map((x) => fmtTime(x.ts * 1000, false));
    trendChart.data.datasets[0].data = h.map((x) => num(x.air_temp));
    trendChart.data.datasets[1].data = h.map((x) => num(x.humidity));
    trendChart.update("none");
    soilChart.data.labels = trendChart.data.labels;
    soilChart.data.datasets[0].data = h.map((x) => num(x.soil_pct));
    soilChart.update("none");
  }
  function renderAdvisories() {
    if (!state.advisories.length) return;
    $("advList").innerHTML = state.advisories
      .map(
        (a) => `
<div class="adv ${a.level}"><div class="em">${a.emoji}</div><div>
  <strong>${a.title}</strong><span>${a.detail}</span>
  <span class="ts">${esc(a.node)} · ${fmtDateTime(a.ts)}</span></div></div>`,
      )
      .join("");
  }
  function renderLog() {
    if (!state.log.length) return;
    $("logBody").innerHTML = state.log
      .slice(0, 120)
      .map((r) => {
        const sm = num(r.soil_pct);
        const cls =
          sm === null
            ? ""
            : sm < TH.soil.critLow
              ? "bad"
              : sm < TH.soil.low
                ? "warn"
                : "ok";
        const dt = r.device_time ? Date.parse(r.device_time) : NaN;
        return `<tr><td class="mono">${r.id ?? "--"}</td><td>${esc(r.device_id ?? "--")}</td>
  <td>${fmtDateTime(r.ts * 1000)}</td><td>${isFinite(dt) ? fmtDateTime(dt) : '<span class="nul">null</span>'}</td>
  <td>${fix(r.air_temp, 1)}</td><td>${fix(r.humidity, 1)}</td><td>${fix(r.soil_raw, 0)}</td>
  <td class="${cls}">${fix(r.soil_pct, 0)}</td></tr>`;
      })
      .join("");
  }

  /* ===================== 8. CHARTS (re-themed on toggle) ===================== */
  window.trendChart = null;
  window.soilChart = null;
  // Shades wilting (red), ideal (green) and waterlogged (blue) zones behind the soil line
  const bandPlugin = {
    id: "band",
    beforeDatasetsDraw(c) {
      const {
        ctx,
        chartArea: a,
        scales: { y },
      } = c;
      [
        [0, TH.soil.critLow, "--red"],
        [TH.soil.low, TH.soil.high, "--green"],
        [TH.soil.critHigh, 100, "--blue"],
      ].forEach(([lo, hi, v]) => {
        ctx.fillStyle = cssVar(v) + "26";
        const t = y.getPixelForValue(hi),
          b = y.getPixelForValue(lo);
        ctx.fillRect(a.left, t, a.right - a.left, b - t);
      });
    },
  };
  function themeChart() {
    const grid = cssVar("--line"),
      muted = cssVar("--muted"),
      text = cssVar("--text");
    Chart.defaults.color = muted;
    soilChart.data.datasets[0].borderColor = cssVar("--blue");
    soilChart.data.datasets[0].backgroundColor = cssVar("--blue") + "33";
    trendChart.data.datasets[0].borderColor = cssVar("--sun");
    trendChart.data.datasets[1].borderColor = cssVar("--purple");
    [soilChart, trendChart].forEach((ch) => {
      Object.values(ch.options.scales).forEach((sc) => {
        sc.ticks.color = muted;
        if (sc.title) sc.title.color = muted;
      });
      ch.options.scales.x.grid.color = grid;
      ch.options.scales.y.grid.color = grid;
      ch.options.plugins.legend.labels.color = muted;
      Object.assign(ch.options.plugins.tooltip, {
        backgroundColor: cssVar("--panel"),
        borderColor: grid,
        titleColor: text,
        bodyColor: text,
      });
      ch.update("none");
    });
  }
  function initChart() {
    Chart.defaults.font.family = "'Segoe UI', system-ui, sans-serif";
    Chart.defaults.font.size = 11;
    const base = (legend) => ({
      responsive: true,
      maintainAspectRatio: false,
      animation: false,
      interaction: { mode: "index", intersect: false },
      plugins: {
        legend: {
          display: legend,
          labels: { usePointStyle: true, boxWidth: 7, padding: 14 },
        },
        tooltip: { borderWidth: 1, padding: 10 },
      },
    });
    const line = { borderWidth: 2, tension: 0.35, pointRadius: 0 };
    window.soilChart = new Chart($("soilChart"), {
      type: "line",
      plugins: [bandPlugin],
      data: {
        labels: [],
        datasets: [
          { label: "Soil water (%)", data: [], fill: true, ...line },
        ],
      },
      options: {
        ...base(false),
        scales: {
          x: { grid: {}, ticks: { maxTicksLimit: 8 } },
          y: {
            min: 0,
            max: 100,
            title: { display: true, text: "% soil water" },
            grid: {},
            ticks: {},
          },
        },
      },
    });
    window.trendChart = new Chart($("trendChart"), {
      type: "line",
      data: {
        labels: [],
        datasets: [
          { label: "Air temp (°C)", data: [], yAxisID: "y", ...line },
          {
            label: "Humidity (%)",
            data: [],
            yAxisID: "y1",
            borderDash: [5, 4],
            ...line,
          },
        ],
      },
      options: {
        ...base(true),
        scales: {
          x: { grid: {}, ticks: { maxTicksLimit: 8 } },
          y: {
            position: "left",
            min: 0,
            max: 50,
            title: { display: true, text: "°C" },
            grid: {},
            ticks: {},
          },
          y1: {
            position: "right",
            min: 0,
            max: 100,
            title: { display: true, text: "% humidity" },
            grid: { drawOnChartArea: false },
            ticks: {},
          },
        },
      },
    });
    themeChart();
  }

  /* ===================== 9. CSV EXPORT ===================== */
  $("csvBtn").onclick = () => {
    if (!state.log.length) return alert("No data to export yet.");
    const cols = [
      "id",
      "device_id",
      "recorded_at",
      "device_time",
      "air_temp",
      "humidity",
      "soil_raw",
      "soil_pct",
    ];
    const lines = state.log.map((r) =>
      cols
        .map((c) => {
          if (c === "recorded_at") return fmtISOLocal(r.ts * 1000);
          if (c === "device_time")
            return r.device_time
              ? fmtISOLocal(Date.parse(r.device_time))
              : "";
          const v = r[c];
          return v === undefined || v === null ? "" : v;
        })
        .join(","),
    );
    const blob = new Blob([cols.join(",") + "\n" + lines.join("\n")], {
      type: "text/csv;charset=utf-8;",
    });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = `busia_agrisense_readings_${fmtDateLocal(Date.now())}.csv`;
    a.click();
    URL.revokeObjectURL(a.href);
  };

  /* ===================== 9c. PROFILE ===================== */
  function currentUser() {
    const x = getSession();
    return (x && x.user) || {};
  }
  function displayName(u) {
    const m = u.user_metadata || {};
    if (m.full_name) return m.full_name;
    return (u.email || "user")
      .split("@")[0]
      .replace(/[._-]+/g, " ")
      .replace(/\b\w/g, (c) => c.toUpperCase());
  }
  function initials(n) {
    const p = n.trim().split(/\s+/);
    return (
      (p[0] || "?")[0] + (p[1] ? p[1][0] : p[0][1] || "")
    ).toUpperCase();
  }
  function renderUser() {
    const u = currentUser(),
      n = displayName(u),
      i = initials(n);
    $("avA").textContent = i;
    $("avB").textContent = i;
    $("uName").textContent = n;
    $("dName").textContent = n;
    $("uMail").textContent = u.email || "";
    $("dMail").textContent = u.email || "";
    const since = u.created_at
      ? new Date(u.created_at).toLocaleDateString("en-GB", {
          timeZone: TZ,
          day: "2-digit",
          month: "short",
          year: "numeric",
        })
      : "--";
    $("dMeta").textContent =
      "Member since " +
      since +
      " · Last sign-in " +
      (u.last_sign_in_at
        ? fmtDateTime(Date.parse(u.last_sign_in_at))
        : "--");
  }
  async function updateUser(body) {
    const token = await accessToken();
    if (!token) throw new Error("Your session has ended. Sign in again.");
    const res = await fetch(`${CONFIG.supabaseUrl}/auth/v1/user`, {
      method: "PUT",
      body: JSON.stringify(body),
      headers: {
        apikey: CONFIG.supabaseKey,
        Authorization: "Bearer " + token,
        "Content-Type": "application/json",
      },
    });
    const data = await res.json().catch(() => ({}));
    if (!res.ok)
      throw new Error(
        data.error_description ||
          data.msg ||
          data.message ||
          "Could not save. Try again.",
      );
    return data;
  }
  function setMsg(id, text, ok) {
    const e = $(id);
    e.textContent = text;
    e.className = "msg " + (ok ? "ok" : "err");
  }
  function openProfile() {
    renderUser();
    $("nameIn").value = displayName(currentUser());
    $("emailIn").value = currentUser().email || "";
    setMsg("nameMsg", "", true);
    $("profile").showModal();
  }
  $("userBtn").onclick = openProfile;
  $("dClose").onclick = () => $("profile").close();
  $("profile").addEventListener("click", (e) => {
    if (e.target === e.currentTarget) $("profile").close();
  });
  $("saveName").onclick = async () => {
    const v = $("nameIn").value.trim();
    if (!v) return setMsg("nameMsg", "Enter a name first.", false);
    try {
      const u = await updateUser({ data: { full_name: v } });
      const x = getSession();
      x.user = u;
      saveSession(x);
      renderUser();
      setMsg("nameMsg", "Name saved.", true);
    } catch (e) {
      setMsg("nameMsg", e.message, false);
    }
  };

  /* ===================== 10. START ===================== */
  async function showApp() {
    $("login").hidden = true;
    $("app").hidden = false;
    if (!window.trendChart) initChart();
    renderUser();
    poll();
    setInterval(poll, CONFIG.pollMs);
    setInterval(renderChips, 30000);
    watchAway();
  }
  if (getSession() && awayTooLong()) expireSession();
  else if (getSession()) showApp();
  else {
    $("login").hidden = false;
    $("email").focus();
    try {
      const n = localStorage.getItem(NOTICE_KEY);
      if (n) {
        $("loginErr").textContent = n;
        $("loginErr").className = "notice";
        localStorage.removeItem(NOTICE_KEY);
      }
    } catch (e) {}
  }
