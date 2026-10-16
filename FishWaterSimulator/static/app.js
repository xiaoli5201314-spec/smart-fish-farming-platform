const pondIds = ["pond_1", "pond_2", "pond_3"];
const cards = new Map();
let toastTimer = null;
let interactionActive = false;

function isInteractiveElement(element) {
  return element instanceof HTMLElement && element.matches("input, select, textarea, button");
}

function markDraftDirty(element) {
  const card = element.closest(".pond-card");
  if (!card || !element.dataset.field) return;
  const settingsFields = ["sensorMode", "publishIntervalSeconds", "ph", "doMgL", "doSaturation", "waterTemp", "chipTempC"];
  if (settingsFields.includes(element.dataset.field)) {
    card.dataset.settingsDirty = "true";
  } else if (["controlMode", "scheduleStart", "scheduleEnd"].includes(element.dataset.field)) {
    card.dataset.controlDirty = "true";
  }
}

document.addEventListener("focusin", (event) => {
  if (isInteractiveElement(event.target)) interactionActive = true;
});

document.addEventListener("focusout", (event) => {
  if (!isInteractiveElement(event.target)) return;
  setTimeout(() => {
    interactionActive = isInteractiveElement(document.activeElement);
    if (!interactionActive) refresh(true);
  }, 0);
});

document.addEventListener("change", (event) => {
  if (!isInteractiveElement(event.target)) return;
  markDraftDirty(event.target);
  interactionActive = true;
});

document.addEventListener("input", (event) => {
  if (!isInteractiveElement(event.target)) return;
  markDraftDirty(event.target);
  interactionActive = true;
});

function showToast(message, error = false) {
  const toast = document.getElementById("toast");
  toast.textContent = message;
  toast.className = `toast visible${error ? " error" : ""}`;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { toast.className = "toast"; }, 2800);
}

async function api(path, options = {}) {
  const response = await fetch(path, {
    headers: { "Content-Type": "application/json" },
    ...options,
  });
  const data = await response.json().catch(() => ({}));
  if (!response.ok || data.ok === false) {
    throw new Error(data.message || `请求失败 (${response.status})`);
  }
  return data;
}

function createCard(pondId) {
  const fragment = document.getElementById("pond-template").content.cloneNode(true);
  const card = fragment.querySelector(".pond-card");
  card.dataset.pondId = pondId;
  card.querySelector(".pond-title").textContent = pondId.replace("pond_", "鱼塘 ");
  card.querySelector(".start-pond").addEventListener("click", () => runAction(`/api/pond/${pondId}/start`, "已启动"));
  card.querySelector(".stop-pond").addEventListener("click", () => runAction(`/api/pond/${pondId}/stop`, "已停止"));
  card.querySelector(".publish-now").addEventListener("click", () => runAction(`/api/pond/${pondId}/publish-now`, "已请求立即发布"));
  card.querySelector(".save-settings").addEventListener("click", () => saveSettings(card));
  card.querySelector(".apply-control").addEventListener("click", () => applyControl(card));
  document.getElementById("pond-grid").appendChild(fragment);
  cards.set(pondId, document.querySelector(`[data-pond-id="${pondId}"]`));
}

function setIfNotFocused(element, value) {
  if (document.activeElement !== element) element.value = value ?? "";
}

function field(card, name) {
  return card.querySelector(`[data-field="${name}"]`);
}

function updateCard(pond) {
  const card = cards.get(pond.pondId);
  if (!card) return;
  const connected = Boolean(pond.connected);
  const running = Boolean(pond.running);
  card.querySelector(".pond-status").textContent = connected ? "MQTT 在线" : (running ? "连接中" : "已停止");
  card.querySelector(".pond-status").className = `pond-status ${connected ? "online" : (running ? "connecting" : "offline")}`;
  card.querySelector(".connection-label").textContent = connected ? "模拟网关在线" : "模拟网关离线";
  card.querySelector(".topic-label").textContent = `water/latest · ${pond.publishIntervalSeconds}s`;
  card.querySelector(".gateway-id").textContent = pond.gatewayId;
  card.querySelector(".error-text").textContent = pond.lastError || "";
  card.querySelector("[data-value=" + "published" + "]").textContent = pond.published;
  card.querySelector("[data-value=" + "lastPublishAt" + "]").textContent = pond.lastPublishAt || "--";
  const values = { ph: pond.ph, doMgL: pond.doMgL, waterTemp: pond.waterTemp, chipTempC: pond.chipTempC };
  for (const [name, value] of Object.entries(values)) {
    card.querySelector(`[data-value="${name}"]`).textContent = value ?? "--";
  }
  const settingsDirty = card.dataset.settingsDirty === "true";
  if (!settingsDirty) {
    for (const name of ["sensorMode", "publishIntervalSeconds", "ph", "doMgL", "doSaturation", "waterTemp", "chipTempC"]) {
      setIfNotFocused(field(card, name), pond[name]);
    }
  }
  const controlDirty = card.dataset.controlDirty === "true";
  if (!controlDirty) {
    setIfNotFocused(field(card, "controlMode"), pond.mode);
    setIfNotFocused(field(card, "scheduleStart"), pond.scheduleStart);
    setIfNotFocused(field(card, "scheduleEnd"), pond.scheduleEnd);
  }
  card.querySelector(".control-label").textContent = pond.collecting ? "采集中" : "已暂停";
  card.querySelector(".start-pond").disabled = running;
  card.querySelector(".stop-pond").disabled = !running;
  card.querySelector(".publish-now").disabled = !connected;
}

async function refresh(force = false) {
  if (interactionActive && !force) return;
  try {
    const status = await api("/api/status");
    document.getElementById("broker-text").textContent = status.mqtt.broker;
    const connected = Boolean(status.mqtt.connected);
    document.getElementById("mqtt-text").textContent = connected ? "MQTT 已连接" : "MQTT 未连接";
    document.getElementById("mqtt-dot").className = `status-dot ${connected ? "online" : "offline"}`;
    status.ponds.forEach(updateCard);
  } catch (error) {
    document.getElementById("mqtt-text").textContent = "本地服务未连接";
    document.getElementById("mqtt-dot").className = "status-dot offline";
  }
}

async function runAction(path, message) {
  try {
    await api(path, { method: "POST", body: "{}" });
    showToast(message);
    await refresh(true);
  } catch (error) {
    showToast(error.message, true);
  }
}

async function saveSettings(card) {
  const body = {
    sensorMode: field(card, "sensorMode").value,
    publishIntervalSeconds: Number(field(card, "publishIntervalSeconds").value),
    ph: Number(field(card, "ph").value),
    doMgL: Number(field(card, "doMgL").value),
    doSaturation: Number(field(card, "doSaturation").value),
    waterTemp: Number(field(card, "waterTemp").value),
    chipTempC: Number(field(card, "chipTempC").value),
  };
  try {
    await api(`/api/pond/${card.dataset.pondId}/settings`, { method: "POST", body: JSON.stringify(body) });
    showToast(`${card.dataset.pondId} 模拟数据已保存`);
    card.dataset.settingsDirty = "false";
    await refresh(true);
  } catch (error) {
    showToast(error.message, true);
  }
}

async function applyControl(card) {
  const body = {
    mode: field(card, "controlMode").value,
    scheduleStart: field(card, "scheduleStart").value,
    scheduleEnd: field(card, "scheduleEnd").value,
  };
  try {
    await api(`/api/pond/${card.dataset.pondId}/control`, { method: "POST", body: JSON.stringify(body) });
    showToast(`${card.dataset.pondId} 控制状态已应用`);
    card.dataset.controlDirty = "false";
    await refresh(true);
  } catch (error) {
    showToast(error.message, true);
  }
}

document.getElementById("start-all").addEventListener("click", () => runAction("/api/all/start", "三个模拟网关已启动"));
document.getElementById("stop-all").addEventListener("click", () => runAction("/api/all/stop", "三个模拟网关已停止"));
pondIds.forEach(createCard);
refresh();
setInterval(() => refresh(), 1200);
