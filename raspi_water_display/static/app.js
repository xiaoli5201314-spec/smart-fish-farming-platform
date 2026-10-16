const metricDefs = [
    { key: "waterTemp", name: "水温", unit: "°C", max: 40, accent: "#2de7ff", digits: 1 },
    { key: "ph", name: "pH值", unit: "pH", max: 14, accent: "#38f5b1", digits: 2 },
    { key: "conductivity", name: "电导率", unit: "μS/cm", max: 2500, accent: "#9b8cff", digits: 0 },
    { key: "tds", name: "TDS", unit: "ppm", max: 1200, accent: "#2de7ff", digits: 0 },
    { key: "salinity", name: "盐度", unit: "ppt", max: 2, accent: "#ffd166", digits: 2 },
    { key: "cod", name: "COD", unit: "mg/L", max: 50, warn: 30, accent: "#ffd166", digits: 2 },
    { key: "ammonia", name: "氨氮", unit: "ppm", max: 2, warn: 0.5, accent: "#ff5a7a", digits: 3 },
    { key: "nitrate", name: "硝酸根", unit: "ppm", max: 40, warn: 25, accent: "#38f5b1", digits: 2 }
];

const riskDefs = [
    { key: "ph", name: "pH 偏离", warn: 1 },
    { key: "cod", name: "COD 负载", warn: 30 },
    { key: "ammonia", name: "氨氮压力", warn: 0.5 },
    { key: "nitrite", name: "亚硝酸根", warn: 0.1 },
    { key: "nitrate", name: "硝酸根", warn: 25 }
];

const state = {
    config: {
        gatewayUrl: "",
        pollIntervalMs: 2500
    },
    timer: null,
    history: []
};

const el = {};

document.addEventListener("DOMContentLoaded", () => {
    cacheElements();
    buildMetricCards();
    buildRiskItems();
    bindEvents();
    tickClock();
    setInterval(tickClock, 1000);
    loadConfig().then(() => {
        refreshData();
        restartPolling();
    });
    window.addEventListener("resize", drawTrend);
});

function cacheElements() {
    [
        "metric-grid", "risk-list", "health-ring", "health-score", "health-label",
        "update-time", "poll-rate", "online-count", "risk-level", "gateway-url",
        "gateway-state", "poll-cycles", "source-pill", "source-text", "clock",
        "settings-modal", "open-settings", "close-settings", "gateway-input",
        "poll-input", "test-gateway", "save-settings", "test-result",
        "trend-canvas"
    ].forEach(id => {
        el[toCamel(id)] = document.getElementById(id);
    });
}

function toCamel(id) {
    return id.replace(/-([a-z])/g, (_, c) => c.toUpperCase());
}

function buildMetricCards() {
    el.metricGrid.innerHTML = metricDefs.map(def => `
        <article class="metric-card" id="metric-${def.key}">
            <div class="metric-title">
                <span>${def.name}</span>
                <small id="quality-${def.key}">--</small>
            </div>
            <div class="metric-ring" id="ring-${def.key}" style="--accent:${def.accent};--pct:0%">
                <span id="value-${def.key}">--</span>
            </div>
            <div class="metric-unit">${def.unit}</div>
        </article>
    `).join("");
}

function buildRiskItems() {
    el.riskList.innerHTML = riskDefs.map(def => `
        <div class="risk-item">
            <div class="risk-top">
                <span>${def.name}</span>
                <strong id="risk-value-${def.key}">--</strong>
            </div>
            <div class="risk-track">
                <div class="risk-fill" id="risk-fill-${def.key}"></div>
            </div>
        </div>
    `).join("");
}

function bindEvents() {
    el.openSettings.addEventListener("click", openSettings);
    el.closeSettings.addEventListener("click", closeSettings);
    el.settingsModal.addEventListener("click", event => {
        if (event.target === el.settingsModal) closeSettings();
    });
    el.saveSettings.addEventListener("click", saveSettings);
    el.testGateway.addEventListener("click", testGateway);
}

function tickClock() {
    el.clock.textContent = new Date().toLocaleTimeString("zh-CN", { hour12: false });
}

async function loadConfig() {
    try {
        const payload = await fetchJson("/api/config");
        state.config = Object.assign(state.config, payload.config || {});
        applyConfigToForm();
    } catch (error) {
        setSourceState("offline", "配置失败");
    }
}

function applyConfigToForm() {
    el.gatewayInput.value = state.config.gatewayUrl || "";
    el.pollInput.value = state.config.pollIntervalMs || 2500;
    el.pollRate.textContent = `${Math.round((state.config.pollIntervalMs || 2500) / 1000)}s`;
    el.gatewayUrl.textContent = state.config.gatewayUrl || "未设置";
}

function restartPolling() {
    if (state.timer) clearInterval(state.timer);
    state.timer = setInterval(refreshData, state.config.pollIntervalMs || 2500);
}

async function refreshData() {
    try {
        const payload = await fetchJson("/api/water/latest");
        const normalized = normalizePayload(payload);
        updateDashboard(normalized, payload);
    } catch (error) {
        renderNoData("网关离线", "连接失败");
        setSourceState("offline", "网关离线");
    }
}

function normalizePayload(payload) {
    const data = payload && payload.data ? payload.data : payload || {};
    const metrics = data.metrics || data || {};
    const gateway = data.gateway || {};
    const source = (payload && payload._display && payload._display.source) || "esp32";

    return {
        source,
        ok: !!(payload && payload.ok),
        connected: !!data.connected || (source === "esp32" && !!payload.ok),
        updateTime: data.updateTime || (payload && payload._display && payload._display.fetchedAt) || "--",
        gateway,
        metrics: {
            waterTemp: readNumber(metrics.waterTemp),
            ph: readNumber(metrics.ph),
            conductivity: readNumber(metrics.conductivity != null ? metrics.conductivity : metrics.ecUsCm),
            tds: readNumber(metrics.tds != null ? metrics.tds : metrics.tdsPpm),
            salinity: readNumber(metrics.salinity != null ? metrics.salinity : metrics.salinityPpt),
            cod: readNumber(metrics.cod != null ? metrics.cod : metrics.codMgL),
            ammonia: readNumber(metrics.ammonia),
            nitrite: readNumber(metrics.nitrite),
            nitrate: readNumber(metrics.nitrate)
        }
    };
}

function updateDashboard(snapshot) {
    const metrics = snapshot.metrics;
    const health = calcHealth(metrics);
    const level = healthLevel(health);

    el.healthScore.textContent = Number.isFinite(health) ? String(health) : "--";
    el.healthLabel.textContent = level.label;
    el.riskLevel.textContent = level.risk;
    el.healthRing.style.setProperty("--score", `${Number.isFinite(health) ? Math.max(0, Math.min(100, health)) : 0}%`);
    el.healthRing.style.filter = `drop-shadow(0 0 22px ${level.glow})`;

    metricDefs.forEach(def => updateMetric(def, metrics[def.key]));
    riskDefs.forEach(def => updateRisk(def, metrics[def.key], metrics));

    const gateway = snapshot.gateway || {};
    const onlineSensors = gateway.onlineSensors != null ? gateway.onlineSensors : 0;
    el.onlineCount.textContent = snapshot.ok && snapshot.connected ? `${onlineSensors}/5` : "--";
    el.updateTime.textContent = shortTime(snapshot.updateTime);
    el.gatewayState.textContent = snapshot.ok && snapshot.connected ? "已连接" : "未连接";
    el.pollCycles.textContent = snapshot.ok && gateway.pollCycles != null ? gateway.pollCycles : "--";
    el.gatewayUrl.textContent = state.config.gatewayUrl || "未设置";

    if (snapshot.ok && snapshot.connected) {
        setSourceState("online", "ESP32 在线");
    } else {
        setSourceState("offline", "未连接");
    }

    pushHistory(metrics, health);
    drawTrend();
}

function renderNoData(sourceText, gatewayState) {
    metricDefs.forEach(def => updateMetric(def, NaN));
    riskDefs.forEach(def => updateRisk(def, NaN, {}));
    el.healthScore.textContent = "--";
    el.healthLabel.textContent = "等待";
    el.riskLevel.textContent = "--";
    el.healthRing.style.setProperty("--score", "0%");
    el.updateTime.textContent = "--";
    el.onlineCount.textContent = "--";
    el.gatewayState.textContent = gatewayState || "未连接";
    el.pollCycles.textContent = "--";
    el.gatewayUrl.textContent = state.config.gatewayUrl || "未设置";
    setSourceState("offline", sourceText || "未连接");
    drawTrend();
}

function updateMetric(def, value) {
    const valueEl = document.getElementById(`value-${def.key}`);
    const ringEl = document.getElementById(`ring-${def.key}`);
    const qualityEl = document.getElementById(`quality-${def.key}`);
    const pct = Number.isFinite(value) ? Math.max(0, Math.min(100, value / def.max * 100)) : 0;

    valueEl.textContent = formatValue(value, def.digits);
    ringEl.style.setProperty("--pct", `${pct}%`);
    qualityEl.textContent = metricQuality(def.key, value);
}

function updateRisk(def, value, metrics) {
    const labelEl = document.getElementById(`risk-value-${def.key}`);
    const fillEl = document.getElementById(`risk-fill-${def.key}`);
    const percent = riskPercent(def.key, value, metrics);
    labelEl.textContent = Number.isFinite(value) ? `${Math.round(percent)}%` : "--";
    fillEl.style.width = `${Number.isFinite(percent) ? Math.max(0, Math.min(100, percent)) : 0}%`;
}

function calcHealth(m) {
    if (!Object.values(m || {}).some(Number.isFinite)) return NaN;

    let score = 100;
    const ph = m.ph;
    if (Number.isFinite(ph)) {
        if (ph < 6.5) score -= (6.5 - ph) * 18;
        if (ph > 8.5) score -= (ph - 8.5) * 18;
    }

    score -= riskPercent("cod", m.cod, m) * 0.16;
    score -= riskPercent("ammonia", m.ammonia, m) * 0.18;
    score -= riskPercent("nitrite", m.nitrite, m) * 0.20;
    score -= riskPercent("nitrate", m.nitrate, m) * 0.10;

    const temp = m.waterTemp;
    if (Number.isFinite(temp)) {
        if (temp < 20) score -= (20 - temp) * 2.5;
        if (temp > 32) score -= (temp - 32) * 3.5;
    }

    return Math.max(0, Math.min(100, Math.round(score)));
}

function healthLevel(score) {
    if (!Number.isFinite(score)) return { label: "等待", risk: "--", glow: "rgba(45,231,255,0.24)" };
    if (score >= 86) return { label: "优秀", risk: "低风险", glow: "rgba(56,245,177,0.48)" };
    if (score >= 72) return { label: "稳定", risk: "轻负载", glow: "rgba(45,231,255,0.46)" };
    if (score >= 58) return { label: "关注", risk: "中风险", glow: "rgba(255,209,102,0.46)" };
    return { label: "异常", risk: "高风险", glow: "rgba(255,90,122,0.52)" };
}

function riskPercent(key, value, metrics) {
    if (!Number.isFinite(value)) return 0;
    if (key === "ph") {
        const offset = Math.abs(value - 7.5);
        return Math.max(0, Math.min(100, offset / 1.8 * 100));
    }
    const def = riskDefs.find(item => item.key === key);
    const warn = def ? def.warn : 1;
    return Math.max(0, Math.min(100, value / warn * 100));
}

function metricQuality(key, value) {
    if (!Number.isFinite(value)) return "--";
    const percent = riskPercent(key, value, {});
    if (key === "waterTemp" || key === "conductivity" || key === "tds" || key === "salinity") {
        return "实时";
    }
    if (percent < 45) return "正常";
    if (percent < 80) return "关注";
    return "偏高";
}

function pushHistory(metrics, health) {
    if (!Number.isFinite(health) && !Number.isFinite(metrics.ph) && !Number.isFinite(metrics.cod)) return;
    state.history.push({
        ts: Date.now(),
        health,
        ph: metrics.ph,
        cod: metrics.cod
    });
    if (state.history.length > 72) state.history.shift();
}

function drawTrend() {
    const canvas = el.trendCanvas;
    if (!canvas) return;
    const rect = canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.max(1, Math.floor(rect.width * dpr));
    canvas.height = Math.max(1, Math.floor(rect.height * dpr));

    const ctx = canvas.getContext("2d");
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    const w = rect.width;
    const h = rect.height;
    ctx.clearRect(0, 0, w, h);

    drawGrid(ctx, w, h);
    drawLine(ctx, w, h, state.history.map(item => item.health), 0, 100, "#2de7ff", true);
    drawLine(ctx, w, h, state.history.map(item => item.ph), 5, 10, "#38f5b1", false);
    drawLine(ctx, w, h, state.history.map(item => item.cod), 0, 30, "#ffd166", false);
}

function drawGrid(ctx, w, h) {
    ctx.save();
    ctx.strokeStyle = "rgba(45,231,255,0.10)";
    ctx.lineWidth = 1;
    for (let i = 1; i < 5; i += 1) {
        const y = (h / 5) * i;
        ctx.beginPath();
        ctx.moveTo(16, y);
        ctx.lineTo(w - 16, y);
        ctx.stroke();
    }
    for (let i = 1; i < 7; i += 1) {
        const x = (w / 7) * i;
        ctx.beginPath();
        ctx.moveTo(x, 12);
        ctx.lineTo(x, h - 14);
        ctx.stroke();
    }
    ctx.restore();
}

function drawLine(ctx, w, h, values, min, max, color, fill) {
    const valid = values.filter(Number.isFinite);
    if (valid.length < 2) return;

    const padX = 18;
    const padY = 24;
    const data = values.slice(-60);
    const step = (w - padX * 2) / Math.max(1, data.length - 1);

    ctx.save();
    ctx.beginPath();
    data.forEach((value, index) => {
        const safe = Number.isFinite(value) ? value : min;
        const ratio = 1 - Math.max(0, Math.min(1, (safe - min) / (max - min)));
        const x = padX + index * step;
        const y = padY + ratio * (h - padY * 2);
        if (index === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    });

    if (fill) {
        const gradient = ctx.createLinearGradient(0, 0, 0, h);
        gradient.addColorStop(0, "rgba(45,231,255,0.24)");
        gradient.addColorStop(1, "rgba(45,231,255,0)");
        const lastX = padX + (data.length - 1) * step;
        ctx.lineTo(lastX, h - padY);
        ctx.lineTo(padX, h - padY);
        ctx.closePath();
        ctx.fillStyle = gradient;
        ctx.fill();
        ctx.beginPath();
        data.forEach((value, index) => {
            const safe = Number.isFinite(value) ? value : min;
            const ratio = 1 - Math.max(0, Math.min(1, (safe - min) / (max - min)));
            const x = padX + index * step;
            const y = padY + ratio * (h - padY * 2);
            if (index === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        });
    }

    ctx.strokeStyle = color;
    ctx.lineWidth = fill ? 3 : 2;
    ctx.shadowColor = color;
    ctx.shadowBlur = fill ? 12 : 8;
    ctx.stroke();
    ctx.restore();
}

function openSettings() {
    applyConfigToForm();
    el.testResult.textContent = "等待设置";
    el.settingsModal.classList.add("show");
    el.settingsModal.setAttribute("aria-hidden", "false");
    el.gatewayInput.focus();
}

function closeSettings() {
    el.settingsModal.classList.remove("show");
    el.settingsModal.setAttribute("aria-hidden", "true");
}

async function saveSettings() {
    const body = {
        gatewayUrl: el.gatewayInput.value.trim(),
        pollIntervalMs: Number(el.pollInput.value) || 2500
    };

    try {
        const payload = await fetchJson("/api/config", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(body)
        });
        state.config = Object.assign(state.config, payload.config || {});
        applyConfigToForm();
        restartPolling();
        closeSettings();
        refreshData();
    } catch (error) {
        el.testResult.textContent = `保存失败：${error.message}`;
    }
}

async function testGateway() {
    const gatewayUrl = el.gatewayInput.value.trim();
    el.testResult.textContent = "正在测试连接...";
    try {
        const payload = await fetchJson("/api/config/test", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ gatewayUrl })
        });
        el.testResult.textContent = `连接成功：${payload.gatewayUrl}`;
    } catch (error) {
        el.testResult.textContent = `连接失败：${error.message}`;
    }
}

async function fetchJson(url, options) {
    const response = await fetch(url, options || {});
    const payload = await response.json().catch(() => ({}));
    if (!response.ok || payload.ok === false) {
        throw new Error(payload.message || response.statusText || "请求失败");
    }
    return payload;
}

function readNumber(value) {
    const n = Number(value);
    return Number.isFinite(n) ? n : NaN;
}

function formatValue(value, digits) {
    if (!Number.isFinite(value)) return "--";
    return Number(value).toFixed(digits);
}

function shortTime(value) {
    if (!value || value === "--") return "--";
    const parts = String(value).split(" ");
    return parts.length > 1 ? parts[1] : String(value);
}

function setSourceState(kind, text) {
    el.sourceText.textContent = text;
    el.sourcePill.classList.remove("online", "offline", "demo");
    if (kind) el.sourcePill.classList.add(kind);
}
