const BUTTONS = [
  { index: 0, id: "btn-a", label: "A / Cross", short: "A" },
  { index: 1, id: "btn-b", label: "B / Circle", short: "B" },
  { index: 2, id: "btn-x", label: "X / Square", short: "X" },
  { index: 3, id: "btn-y", label: "Y / Triangle", short: "Y" },
  { index: 4, id: "btn-lb", label: "LB / L1", short: "LB" },
  { index: 5, id: "btn-rb", label: "RB / R1", short: "RB" },
  { index: 6, id: "btn-lt", label: "LT / L2", short: "LT" },
  { index: 7, id: "btn-rt", label: "RT / R2", short: "RT" },
  { index: 8, id: "btn-back", label: "Back / Share", short: "Back" },
  { index: 9, id: "btn-start", label: "Start / Options", short: "Start" },
  { index: 10, id: "btn-ls", label: "L3 (stick)", short: "L3" },
  { index: 11, id: "btn-rs", label: "R3 (stick)", short: "R3" },
  { index: 12, id: "btn-du", label: "D-pad arriba", short: "D↑" },
  { index: 13, id: "btn-dd", label: "D-pad abajo", short: "D↓" },
  { index: 14, id: "btn-dl", label: "D-pad izquierda", short: "D←" },
  { index: 15, id: "btn-dr", label: "D-pad derecha", short: "D→" },
  { index: 16, id: "btn-guide", label: "Guide / Home", short: "Guide" },
  { index: 17, id: "btn-touchpad", label: "Touchpad click", short: "Touchpad" },
];

const state = {
  gamepads: [],
  selectedIndex: null,
  tested: new Set(),
  drift: { active: false, started: 0, samples: [], rawMax: 0, filteredMax: 0 },
  stickDeadzone: 3,
  triggerDeadzone: 2,
  lastScan: 0,
  padSignature: "",
  lastCliLength: 0,
  cliRunning: false,
  lastLogLine: "",
};

const $ = (id) => document.getElementById(id);
const clamp = (value, min, max) => Math.min(max, Math.max(min, value));

function log(message) {
  const line = `[${new Date().toLocaleTimeString()}] ${message}`;
  if (line === state.lastLogLine) {
    return;
  }
  state.lastLogLine = line;
  const out = $("log");
  if (!out) {
    return;
  }
  out.textContent += `${line}\n`;
  const lines = out.textContent.split("\n");
  if (lines.length > 600) {
    out.textContent = lines.slice(-600).join("\n");
  }
  out.scrollTop = out.scrollHeight;
}

function activeGamepad() {
  if (!("getGamepads" in navigator)) {
    return null;
  }
  const pads = navigator.getGamepads();
  if (state.selectedIndex === null) {
    return null;
  }
  return pads[state.selectedIndex] || null;
}

function scanGamepads(force = false) {
  if (!("getGamepads" in navigator)) {
    $("browser-hint").textContent =
      "Este navegador no soporta la Gamepad API. Usa Chrome, Edge o Firefox reciente.";
    return;
  }

  const now = performance.now();
  if (!force && now - state.lastScan < 900) {
    return;
  }
  state.lastScan = now;

  const pads = Array.from(navigator.getGamepads()).filter(Boolean);
  const previous = state.selectedIndex;
  state.gamepads = pads;
  const signature = pads.map((pad) => `${pad.index}:${pad.id}:${pad.mapping || ""}`).join("|");

  const select = $("device-select");
  const wanted = state.selectedIndex;

  if (pads.length === 0) {
    state.selectedIndex = null;
    state.padSignature = "";
    select.innerHTML = '<option value="">Pulsa un botón del mando…</option>';
    $("connection-badge").textContent = "Sin mando";
    $("connection-badge").className = "badge off";
    $("device-id").textContent = "Sin dispositivo seleccionado";
    return;
  }

  if (signature !== state.padSignature) {
    state.padSignature = signature;
    select.innerHTML = "";
    for (const pad of pads) {
      const option = document.createElement("option");
      option.value = String(pad.index);
      option.textContent = `${pad.index}: ${pad.id} (${pad.mapping || "sin mapping"})`;
      select.appendChild(option);
    }
  }

  if (wanted !== null && pads.some((pad) => pad.index === wanted)) {
    state.selectedIndex = wanted;
  } else {
    state.selectedIndex = pads[0].index;
  }

  select.value = String(state.selectedIndex);
  const pad = activeGamepad();
  if (pad) {
    $("device-id").textContent = pad.id;
    $("connection-badge").textContent = "Conectado";
    $("connection-badge").className = "badge on";
    if (previous !== state.selectedIndex) {
      log(`Mando seleccionado: ${pad.id}`);
    }
  }
}

function buttonPressed(pad, index) {
  const button = pad.buttons[index];
  return Boolean(button && (button.pressed || button.value > 0.5));
}

function triggerValue(pad, index) {
  const button = pad.buttons[index];
  if (button && Number.isFinite(button.value) && button.value > 0.01) {
    return clamp(button.value, 0, 1);
  }
  const analogIndex = index === 6 ? 4 : 5;
  if (pad.axes.length > analogIndex && !pad.mapping) {
    return clamp((pad.axes[analogIndex] + 1) / 2, 0, 1);
  }
  return button ? clamp(button.value, 0, 1) : 0;
}

function filterStick(x, y, deadzonePercent) {
  const deadzone = clamp(deadzonePercent, 0, 30) / 100;
  const magnitude = Math.hypot(x, y);
  if (deadzone <= 0) {
    return { x, y, magnitude, filteredMagnitude: magnitude };
  }
  if (magnitude <= deadzone) {
    return { x: 0, y: 0, magnitude, filteredMagnitude: 0 };
  }
  const clamped = Math.min(magnitude, 1);
  const filteredMagnitude = (clamped - deadzone) / (1 - deadzone);
  const scale = filteredMagnitude / magnitude;
  return { x: x * scale, y: y * scale, magnitude, filteredMagnitude };
}

function filterTrigger(value, deadzonePercent) {
  const deadzone = clamp(deadzonePercent, 0, 30) / 100;
  if (deadzone <= 0) {
    return value;
  }
  if (value <= deadzone) {
    return 0;
  }
  return (value - deadzone) / (1 - deadzone);
}

function setActive(id, active) {
  const element = $(id);
  if (element) {
    element.classList.toggle("active", active);
  }
}

function stickClass(side, active) {
  const sticks = document.querySelectorAll(".stick");
  if (sticks.length >= 2) {
    sticks[side === "left" ? 0 : 1].classList.toggle("active", active);
  }
}

function inferProfile(pad) {
  const id = (pad.id || "").toLowerCase();
  if (id.includes("steam deck")) return "steam_deck";
  if (id.includes("steam")) return "steam_controller";
  if (id.includes("dualsense") || id.includes("ps5")) return "dualsense / dualsense_edge";
  if (id.includes("dualshock") || id.includes("ps4") || id.includes("wireless controller")) return "dualshock4";
  if (id.includes("switch") || id.includes("nintendo") || id.includes("pro controller")) return "switch_pro";
  if (id.includes("xbox 360")) return "xbox_360";
  if (id.includes("xbox")) return "xbox_series / xbox_elite";
  return "generic / fallback";
}

function updatePad(pad) {
  const leftRaw = { x: clamp(pad.axes[0] || 0, -1, 1), y: clamp(pad.axes[1] || 0, -1, 1) };
  const rightRaw = { x: clamp(pad.axes[2] || 0, -1, 1), y: clamp(pad.axes[3] || 0, -1, 1) };
  const left = filterStick(leftRaw.x, leftRaw.y, state.stickDeadzone);
  const right = filterStick(rightRaw.x, rightRaw.y, state.stickDeadzone);

  const leftMagnitude = Math.hypot(leftRaw.x, leftRaw.y);
  const rightMagnitude = Math.hypot(rightRaw.x, rightRaw.y);

  const leftDot = $("ls-dot");
  const rightDot = $("rs-dot");
  if (leftDot) {
    leftDot.setAttribute("cx", String(370 + leftRaw.x * 32));
    leftDot.setAttribute("cy", String(350 + leftRaw.y * 32));
  }
  if (rightDot) {
    rightDot.setAttribute("cx", String(630 + rightRaw.x * 32));
    rightDot.setAttribute("cy", String(350 + rightRaw.y * 32));
  }
  $("ls-x").textContent = left.x.toFixed(2);
  $("ls-y").textContent = left.y.toFixed(2);
  $("ls-mag").textContent = leftMagnitude.toFixed(2);
  $("rs-x").textContent = right.x.toFixed(2);
  $("rs-y").textContent = right.y.toFixed(2);
  $("rs-mag").textContent = rightMagnitude.toFixed(2);

  $("ls-raw").textContent = `${leftRaw.x.toFixed(2)}, ${leftRaw.y.toFixed(2)}`;
  $("ls-filtered").textContent = `${left.x.toFixed(2)}, ${left.y.toFixed(2)}`;
  $("rs-raw").textContent = `${rightRaw.x.toFixed(2)}, ${rightRaw.y.toFixed(2)}`;
  $("rs-filtered").textContent = `${right.x.toFixed(2)}, ${right.y.toFixed(2)}`;

  const lsPadDot = $("ls-pad-dot");
  const rsPadDot = $("rs-pad-dot");
  if (lsPadDot) {
    lsPadDot.style.left = `${50 + leftRaw.x * 42}%`;
    lsPadDot.style.top = `${50 + leftRaw.y * 42}%`;
  }
  if (rsPadDot) {
    rsPadDot.style.left = `${50 + rightRaw.x * 42}%`;
    rsPadDot.style.top = `${50 + rightRaw.y * 42}%`;
  }

  const ltRaw = triggerValue(pad, 6);
  const rtRaw = triggerValue(pad, 7);
  const lt = filterTrigger(ltRaw, state.triggerDeadzone);
  const rt = filterTrigger(rtRaw, state.triggerDeadzone);

  const ltFill = $("lt-fill");
  const rtFill = $("rt-fill");
  if (ltFill) ltFill.setAttribute("width", String(lt * 146));
  if (rtFill) rtFill.setAttribute("width", String(rt * 146));
  $("lt-value").textContent = `${Math.round(lt * 100)}%`;
  $("rt-value").textContent = `${Math.round(rt * 100)}%`;
  $("profile-hint").textContent = inferProfile(pad);

  stickClass("left", leftMagnitude > 0.08 || buttonPressed(pad, 10));
  stickClass("right", rightMagnitude > 0.08 || buttonPressed(pad, 11));

  const pressed = [];
  for (const button of BUTTONS) {
    const active = buttonPressed(pad, button.index);
    setActive(button.id, active);
    if (active) {
      pressed.push(button.short);
      state.tested.add(button.index);
    }
    const chip = document.querySelector(`.button-chip[data-index="${button.index}"]`);
    if (chip) {
      chip.classList.toggle("pressed", active);
      chip.classList.toggle("tested", state.tested.has(button.index));
    }
  }
  // LT/RT are analog, but still mark them as tested when pressed.
  if (ltRaw > 0.1) {
    state.tested.add(6);
  }
  if (rtRaw > 0.1) {
    state.tested.add(7);
  }

  const pressedList = $("pressed-list");
  pressedList.innerHTML = pressed.length
    ? pressed.map((label) => `<span class="chip">${label}</span>`).join("")
    : '<span class="muted">Nada</span>';

  const axes = [];
  for (let index = 0; index < pad.axes.length; index += 1) {
    axes.push(`<span>a${index}: ${pad.axes[index].toFixed(3)}</span>`);
  }
  $("axes-list").innerHTML = axes.join("") || '<span class="muted">Sin ejes</span>';

  const testedCount = BUTTONS.filter((button) => state.tested.has(button.index)).length;
  const progress = BUTTONS.length ? (testedCount / BUTTONS.length) * 100 : 0;
  $("button-progress-fill").style.width = `${progress}%`;
  $("button-progress-text").textContent = `${testedCount} / ${BUTTONS.length} botones probados`;

  if (state.drift.active) {
    const rawMagnitude = Math.max(
      Math.hypot(leftRaw.x, leftRaw.y),
      Math.hypot(rightRaw.x, rightRaw.y)
    );
    const filteredMagnitude = Math.max(
      Math.hypot(left.x, left.y),
      Math.hypot(right.x, right.y)
    );
    state.drift.rawMax = Math.max(state.drift.rawMax, rawMagnitude);
    state.drift.filteredMax = Math.max(state.drift.filteredMax, filteredMagnitude);
    state.drift.samples.push({ rawMagnitude, filteredMagnitude });

    $("drift-samples").textContent = String(state.drift.samples.length);
    $("drift-raw").textContent = state.drift.rawMax.toFixed(4);
    $("drift-filtered").textContent = state.drift.filteredMax.toFixed(4);

    const elapsed = (performance.now() - state.drift.started) / 1000;
    $("drift-btn").textContent = `Midiendo… ${Math.max(0, 5 - elapsed).toFixed(1)} s`;
    if (elapsed >= 5) {
      state.drift.active = false;
      $("drift-btn").textContent = "Test de drift (5 s)";
      const verdict = $("drift-verdict");
      if (state.drift.samples.length === 0) {
        verdict.textContent = "No se recibieron muestras.";
        verdict.className = "result neutral";
      } else if (state.drift.filteredMax <= 0.0015) {
        verdict.textContent = `PASS · drift filtrado ${state.drift.filteredMax.toFixed(4)} · sin movimiento fantasma.`;
        verdict.className = "result pass";
        log(`Drift PASS (raw ${state.drift.rawMax.toFixed(4)} -> filtrado ${state.drift.filteredMax.toFixed(4)})`);
      } else {
        verdict.textContent = `FAIL · drift filtrado ${state.drift.filteredMax.toFixed(4)}. Sube la deadzone o revisa el mando.`;
        verdict.className = "result fail";
        log(`Drift FAIL (filtrado ${state.drift.filteredMax.toFixed(4)})`);
      }
    }
  }
}

function resetPadVisuals() {
  for (const button of BUTTONS) {
    setActive(button.id, false);
    const chip = document.querySelector(`.button-chip[data-index="${button.index}"]`);
    if (chip) {
      chip.classList.remove("pressed");
    }
  }
  if ($("lt-fill")) $("lt-fill").setAttribute("width", "0");
  if ($("rt-fill")) $("rt-fill").setAttribute("width", "0");
  $("lt-value").textContent = "0%";
  $("rt-value").textContent = "0%";
  $("ls-x").textContent = "0.00";
  $("ls-y").textContent = "0.00";
  $("rs-x").textContent = "0.00";
  $("rs-y").textContent = "0.00";
  $("ls-mag").textContent = "0.00";
  $("rs-mag").textContent = "0.00";
  $("pressed-list").innerHTML = '<span class="muted">Nada</span>';
  $("axes-list").innerHTML = "";
  $("profile-hint").textContent = "—";
  stickClass("left", false);
  stickClass("right", false);
}

function tick() {
  scanGamepads();
  const pad = activeGamepad();
  if (pad) {
    updatePad(pad);
  } else {
    resetPadVisuals();
    if (state.drift.active) {
      state.drift.active = false;
      $("drift-btn").textContent = "Test de drift (5 s)";
      $("drift-verdict").textContent = "El mando se desconectó durante la prueba.";
      $("drift-verdict").className = "result fail";
      log("Drift cancelado: mando desconectado.");
    }
  }
  requestAnimationFrame(tick);
}

function buildButtonGrid() {
  const grid = $("button-grid");
  grid.innerHTML = "";
  for (const button of BUTTONS) {
    const chip = document.createElement("div");
    chip.className = "button-chip";
    chip.dataset.index = String(button.index);
    chip.innerHTML = `<span>${button.label}</span>`;
    grid.appendChild(chip);
  }
}

function setupTabs() {
  const tabs = document.querySelectorAll(".tab");
  const panels = document.querySelectorAll(".tab-panel");
  const activate = (name) => {
    tabs.forEach((tab) => tab.classList.toggle("active", tab.dataset.tab === name));
    panels.forEach((panel) => panel.classList.toggle("active", panel.id === `tab-${name}`));
  };
  tabs.forEach((tab) => tab.addEventListener("click", () => activate(tab.dataset.tab)));
  window.addEventListener("keydown", (event) => {
    const map = { "1": "live", "2": "buttons", "3": "sticks", "4": "profiles", "5": "log" };
    if (map[event.key]) {
      activate(map[event.key]);
    }
  });
  activate("live");
}

function setDriftNeutral() {
  $("drift-verdict").textContent = "Sin datos.";
  $("drift-verdict").className = "result neutral";
  $("drift-raw").textContent = "—";
  $("drift-filtered").textContent = "—";
  $("drift-samples").textContent = "0";
}

function startDrift() {
  const pad = activeGamepad();
  if (!pad) {
    $("drift-verdict").textContent = "Conecta y selecciona un mando primero.";
    $("drift-verdict").className = "result fail";
    return;
  }
  state.drift = {
    active: true,
    started: performance.now(),
    samples: [],
    rawMax: 0,
    filteredMax: 0,
  };
  $("drift-btn").textContent = "Midiendo… 5.0 s";
  $("drift-verdict").textContent = "Midiendo. No toques los sticks.";
  $("drift-verdict").className = "result neutral";
  $("drift-raw").textContent = "0.0000";
  $("drift-filtered").textContent = "0.0000";
  $("drift-samples").textContent = "0";
  log("Inicio del test de drift (5 s)");
}

function asyncApi(path, options = {}) {
  return fetch(path, {
    headers: { "Content-Type": "application/json" },
    ...options,
  }).then(async (response) => {
    const body = await response.json().catch(() => ({}));
    if (!response.ok) {
      throw new Error(body.error || body.message || `HTTP ${response.status}`);
    }
    return body;
  });
}

function setCliBadge(running, kind = "") {
  state.cliRunning = running;
  const badge = $("cli-badge");
  if (!badge) return;
  badge.textContent = running ? `Ejecutando ${kind || ""}`.trim() : "CLI inactivo";
  badge.className = running ? "badge on" : "badge off";
}

function cliPayload(kind) {
  return {
    kind,
    source: $("cli-source").value,
    index: Number($("cli-index").value || 0),
    profile: $("cli-profile").value,
    seconds: Number($("cli-seconds").value || 60),
  };
}

async function cliStart(kind) {
  try {
    const response = await asyncApi("/api/start", {
      method: "POST",
      body: JSON.stringify(cliPayload(kind)),
    });
    log(`CLI: ${response.message || "iniciado"}`);
    setCliBadge(true, kind);
    setTimeout(() => scanGamepads(true), 1200);
  } catch (error) {
    log(`CLI error: ${error.message}`);
    $("cli-output").textContent = `Error: ${error.message}`;
  }
}

async function cliStop() {
  try {
    const response = await asyncApi("/api/stop", { method: "POST", body: "{}" });
    log(`CLI: ${response.message || "detenido"}`);
    setCliBadge(false);
  } catch (error) {
    log(`CLI error: ${error.message}`);
  }
}

async function cliList() {
  try {
    $("cli-output").textContent = "Consultando dispositivos…";
    const response = await asyncApi("/api/devices");
    $("cli-output").textContent = response.text || "(sin salida)";
    log("Lista CLI actualizada.");
    scanGamepads(true);
  } catch (error) {
    $("cli-output").textContent = `Error: ${error.message}`;
    log(`CLI error: ${error.message}`);
  }
}

async function pollCli() {
  try {
    const payload = await asyncApi("/api/status");
    const output = payload.output || [];
    if (output.length !== state.lastCliLength) {
      const newLines = output.slice(state.lastCliLength);
      state.lastCliLength = output.length;
      for (const line of newLines) {
        log(`CLI: ${line}`);
      }
      $("cli-output").textContent = output.slice(-80).join("\n") || "Sin salida todavía.";
      $("cli-output").scrollTop = $("cli-output").scrollHeight;
    }
    setCliBadge(payload.running, payload.kind);
    if (!payload.running && state.cliRunning) {
      scanGamepads(true);
    }
  } catch (error) {
    // Server may be down; do not spam the log.
  }
}

function setupCli() {
  $("cli-list").addEventListener("click", cliList);
  $("cli-bridge").addEventListener("click", () => cliStart("bridge"));
  $("cli-selftest").addEventListener("click", () => cliStart("selftest"));
  $("cli-verify").addEventListener("click", () => cliStart("verify"));
  $("cli-drift").addEventListener("click", () => cliStart("drift"));
  $("cli-stop").addEventListener("click", cliStop);
  setInterval(pollCli, 900);
   cliList();
}

function setupDeviceControls() {
  $("device-select").addEventListener("change", (event) => {
    const value = event.target.value;
    state.selectedIndex = value === "" ? null : Number(value);
    state.tested.clear();
    scanGamepads(true);
    log(`Selección manual: ${value}`);
  });
  $("refresh-devices").addEventListener("click", () => {
    scanGamepads(true);
    log("Búsqueda de mandos actualizada.");
  });
  window.addEventListener("gamepadconnected", (event) => {
    log(`Mando conectado: ${event.gamepad.id}`);
    scanGamepads(true);
  });
  window.addEventListener("gamepaddisconnected", (event) => {
    log(`Mando desconectado: ${event.gamepad.id}`);
    scanGamepads(true);
  });
}

function setupPrecision() {
  const stick = $("dz-stick");
  const trigger = $("dz-trigger");
  const update = () => {
    state.stickDeadzone = Number(stick.value);
    state.triggerDeadzone = Number(trigger.value);
    $("dz-label").textContent = `${state.stickDeadzone}%`;
    $("tdz-label").textContent = `${state.triggerDeadzone}%`;
  };
  stick.addEventListener("input", update);
  trigger.addEventListener("input", update);
  update();
}

function setupRumble() {
  $("rumble-btn").addEventListener("click", () => {
    const pad = activeGamepad();
    if (!pad) {
      log("Rumble: no hay mando seleccionado.");
      return;
    }
    const actuator = pad.vibrationActuator;
    if (!actuator || !actuator.playEffect) {
      log("Rumble: este navegador o mando no expone vibrationActuator.");
      $("drift-verdict").textContent =
        "Este navegador o mando no expone rumble. Prueba en Chrome/Edge o usa el tester CLI.";
      $("drift-verdict").className = "result neutral";
      return;
    }
    actuator
      .playEffect("dual-rumble", {
        startDelay: 0,
        duration: 700,
        weakMagnitude: 0.7,
        strongMagnitude: 0.9,
      })
      .then(() => log("Rumble enviado al mando."))
      .catch((error) => log(`Rumble error: ${error.message || error}`));
  });
}

function setupButtons() {
  buildButtonGrid();
  $("reset-buttons").addEventListener("click", () => {
    state.tested.clear();
    setDriftNeutral();
    log("Pruebas de botones reiniciadas.");
  });
  $("drift-btn").addEventListener("click", startDrift);
  $("clear-log").addEventListener("click", () => {
    $("log").textContent = "";
  });
}

async function boot() {
  setupTabs();
  setupDeviceControls();
  setupPrecision();
  setupRumble();
  setupButtons();
  setupCli();
  setDriftNeutral();
  buildButtonGrid();
  scanGamepads(true);
  requestAnimationFrame(tick);
  log("ApolloInput Nova Visual Tester listo.");
  if (!("getGamepads" in navigator)) {
    log("Gamepad API no disponible en este navegador.");
  }
}

window.addEventListener("DOMContentLoaded", boot);
