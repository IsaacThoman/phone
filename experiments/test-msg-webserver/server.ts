import mqtt from "mqtt";

const ROOT = new URL(".", import.meta.url);
const PUBLIC_DIR = new URL("./public/", ROOT);
const DATA_DIR = new URL("./data/", ROOT);
const MESSAGE_FILE = new URL("./message.json", DATA_DIR);
const DEVICE_STATUS_FILE = new URL("./device-status.json", DATA_DIR);
const PORT = readPort();

const DISPLAY = {
  width: 122,
  height: 250,
  font: "phone-5x7-v1",
  glyphWidth: 5,
  glyphHeight: 7,
  characterAdvance: 6,
  lineAdvance: 13,
  horizontalAlign: "center",
  verticalAlign: "center",
} as const;

const DEFAULT_MESSAGE = "cellular link ready.";
const MAX_MESSAGE_LENGTH = 300;
const ALLOWED_MESSAGE = /^[\x20-\x7e\n]*$/;
const MQTT_BROKER_URL = "mqtts://broker.hivemq.com:8883";
const MQTT_NAMESPACE = "signal-note/cff796f9-d023-4fc0-beaf-4a9770018dcb";
const MQTT_MESSAGE_TOPIC = `${MQTT_NAMESPACE}/phone-01/message`;
const MQTT_STATUS_TOPIC = `${MQTT_NAMESPACE}/phone-01/status`;
const MQTT_KEEP_ALIVE_SECONDS = 120;
const HTTP_FALLBACK_SECONDS = 15 * 60;

type MessageState = {
  message: string;
  revision: number;
  updatedAt: string;
};

type DeviceStatus = {
  deviceId: string;
  firmwareVersion: string;
  uptimeSeconds: number;
  signalRssiDbm: number | null;
  signalPercent: number | null;
  operator: string;
  networkType: string;
  ipAddress: string;
  lastMessageRevision: number;
  displayUpdated: boolean;
  lastPollOk: boolean;
  lastError: string;
  reportedAt: string;
};

let state = await loadState();
let deviceStatus = await loadDeviceStatus();
let mqttConnected = false;
let retainedMessageSeen = false;

const mqttClient = mqtt.connect(MQTT_BROKER_URL, {
  clean: true,
  clientId: "signal-note-web-cff796f9",
  connectTimeout: 15_000,
  keepalive: MQTT_KEEP_ALIVE_SECONDS,
  reconnectPeriod: 5_000,
});

mqttClient.on("connect", () => {
  mqttConnected = true;
  retainedMessageSeen = false;
  console.log(`MQTT connected to ${MQTT_BROKER_URL}`);
  mqttClient.subscribe(
    [MQTT_MESSAGE_TOPIC, MQTT_STATUS_TOPIC],
    { qos: 1 },
    (error) => {
      if (error) {
        console.error("MQTT subscribe failed:", error);
        return;
      }
      setTimeout(() => {
        if (!retainedMessageSeen) void publishMessageState();
      }, 1_500);
    },
  );
});

mqttClient.on("message", (topic, payload) => {
  void handleMqttMessage(topic, payload.toString());
});

mqttClient.on("close", () => {
  mqttConnected = false;
});

mqttClient.on("error", (error) => {
  mqttConnected = false;
  console.error("MQTT error:", error.message);
});

Deno.serve({ port: PORT }, async (request) => {
  try {
    return await route(request);
  } catch (error) {
    console.error(error);
    return json({ error: "internal server error" }, 500);
  }
});

async function route(request: Request): Promise<Response> {
  const url = new URL(request.url);

  if (url.pathname === "/healthz" && request.method === "GET") {
    return json({ ok: true });
  }

  if (url.pathname === "/api/message") {
    if (request.method === "GET") {
      return messageResponse(request);
    }

    if (request.method === "PUT" || request.method === "POST") {
      return await updateMessage(request);
    }

    return methodNotAllowed("GET, PUT, POST");
  }

  if (url.pathname === "/api/device/status") {
    if (request.method === "GET") {
      return deviceStatusResponse();
    }

    if (request.method === "POST") {
      return await updateDeviceStatus(request);
    }

    return methodNotAllowed("GET, POST");
  }

  if (request.method !== "GET" && request.method !== "HEAD") {
    return methodNotAllowed("GET, HEAD");
  }

  const fileName = url.pathname === "/" ? "index.html" : url.pathname.slice(1);
  if (!["index.html", "app.js", "styles.css"].includes(fileName)) {
    return new Response("Not found", { status: 404 });
  }

  const file = await Deno.readFile(new URL(fileName, PUBLIC_DIR));
  const headers = new Headers({
    "content-type": contentType(fileName),
    // These assets use stable filenames, so browsers and the reverse proxy must
    // re-fetch them after each deploy instead of mixing old JS with new HTML.
    "cache-control": "no-store, no-cache, must-revalidate, max-age=0",
    "cdn-cache-control": "no-store",
    "cloudflare-cdn-cache-control": "no-store",
    "x-content-type-options": "nosniff",
  });
  return new Response(request.method === "HEAD" ? null : file, { headers });
}

function deviceStatusResponse(): Response {
  if (!deviceStatus) {
    return json({ status: null, online: false, staleAfterSeconds: 75 });
  }

  const ageSeconds = Math.max(0, (Date.now() - Date.parse(deviceStatus.reportedAt)) / 1000);
  return json({
    status: deviceStatus,
    online: ageSeconds <= 75,
    ageSeconds: Math.round(ageSeconds),
    staleAfterSeconds: 75,
  });
}

async function updateDeviceStatus(request: Request): Promise<Response> {
  if (!request.headers.get("content-type")?.toLowerCase().includes("application/json")) {
    return json({ error: "content-type must be application/json" }, 415);
  }

  let body: unknown;
  try {
    body = await request.json();
  } catch {
    return json({ error: "request body must be valid JSON" }, 400);
  }

  const validationError = validateDeviceStatus(body);
  if (validationError) return json({ error: validationError }, 422);

  await storeDeviceStatus(body as Omit<DeviceStatus, "reportedAt">);
  return deviceStatusResponse();
}

async function storeDeviceStatus(report: Omit<DeviceStatus, "reportedAt">): Promise<void> {
  deviceStatus = {
    ...report,
    deviceId: cleanText(report.deviceId, 32),
    firmwareVersion: cleanText(report.firmwareVersion, 32),
    operator: cleanText(report.operator, 64),
    networkType: cleanText(report.networkType, 32),
    ipAddress: cleanText(report.ipAddress, 64),
    lastError: cleanText(report.lastError, 160),
    reportedAt: new Date().toISOString(),
  };
  await saveJson(DEVICE_STATUS_FILE, deviceStatus);
}

function validateDeviceStatus(value: unknown): string | undefined {
  if (!isRecord(value)) return "request body must be an object";

  const strings = [
    "deviceId",
    "firmwareVersion",
    "operator",
    "networkType",
    "ipAddress",
    "lastError",
  ];
  if (strings.some((key) => typeof value[key] !== "string")) {
    return `fields ${strings.join(", ")} must be strings`;
  }

  const integers = ["uptimeSeconds", "lastMessageRevision"];
  if (integers.some((key) => !Number.isInteger(value[key]) || (value[key] as number) < 0)) {
    return `fields ${integers.join(", ")} must be non-negative integers`;
  }

  if (typeof value.displayUpdated !== "boolean" || typeof value.lastPollOk !== "boolean") {
    return "fields displayUpdated and lastPollOk must be booleans";
  }

  if (!isNullableNumber(value.signalRssiDbm) || !isNullableNumber(value.signalPercent)) {
    return "signal fields must be numbers or null";
  }

  if (
    typeof value.signalPercent === "number" &&
    (value.signalPercent < 0 || value.signalPercent > 100)
  ) {
    return "signalPercent must be between 0 and 100";
  }
}

function messageResponse(request: Request): Response {
  const etag = `"message-${state.revision}"`;
  if (request.headers.get("if-none-match") === etag) {
    return new Response(null, { status: 304, headers: { etag, "cache-control": "no-cache" } });
  }

  return json(
    {
      ...state,
      delivery: deliveryState(),
      httpFallbackAfterSeconds: HTTP_FALLBACK_SECONDS,
      display: DISPLAY,
    },
    200,
    { etag, "cache-control": "no-cache" },
  );
}

async function updateMessage(request: Request): Promise<Response> {
  if (!request.headers.get("content-type")?.toLowerCase().includes("application/json")) {
    return json({ error: "content-type must be application/json" }, 415);
  }

  let body: unknown;
  try {
    body = await request.json();
  } catch {
    return json({ error: "request body must be valid JSON" }, 400);
  }

  if (!isRecord(body) || typeof body.message !== "string") {
    return json({ error: 'request body must be {"message":"..."}' }, 400);
  }

  const message = body.message.replaceAll("\r\n", "\n").replaceAll("\r", "\n").trim();
  const validationError = validateMessage(message);
  if (validationError) return json({ error: validationError }, 422);

  if (message === state.message) {
    await publishMessageState();
    return messageResponse(request);
  }

  state = {
    message,
    revision: state.revision + 1,
    updatedAt: new Date().toISOString(),
  };
  await saveState(state);
  const published = await publishMessageState();
  return json({
    ...state,
    delivery: { ...deliveryState(), published },
    httpFallbackAfterSeconds: HTTP_FALLBACK_SECONDS,
    display: DISPLAY,
  });
}

function deliveryState() {
  return {
    primary: "mqtt",
    connected: mqttConnected,
    keepAliveSeconds: MQTT_KEEP_ALIVE_SECONDS,
  };
}

async function publishMessageState(): Promise<boolean> {
  if (!mqttConnected) return false;

  return await new Promise<boolean>((resolve) => {
    mqttClient.publish(
      MQTT_MESSAGE_TOPIC,
      JSON.stringify(state),
      { qos: 1, retain: true },
      (error) => {
        if (error) console.error("MQTT message publish failed:", error);
        resolve(!error);
      },
    );
  });
}

async function handleMqttMessage(topic: string, payload: string): Promise<void> {
  let parsed: unknown;
  try {
    parsed = JSON.parse(payload);
  } catch {
    console.error(`Ignored invalid JSON from MQTT topic ${topic}`);
    return;
  }

  if (topic === MQTT_MESSAGE_TOPIC) {
    retainedMessageSeen = true;
    if (!isMessageState(parsed)) {
      console.error("Ignored invalid retained MQTT message");
      return;
    }
    if (parsed.revision >= state.revision) {
      if (parsed.message !== state.message || parsed.revision !== state.revision) {
        state = parsed;
        await saveState(state);
      }
    } else {
      await publishMessageState();
    }
    return;
  }

  if (topic === MQTT_STATUS_TOPIC) {
    const validationError = validateDeviceStatus(parsed);
    if (validationError) {
      console.error(`Ignored invalid MQTT telemetry: ${validationError}`);
      return;
    }
    await storeDeviceStatus(parsed as Omit<DeviceStatus, "reportedAt">);
  }
}

function isMessageState(value: unknown): value is MessageState {
  return isRecord(value) &&
    typeof value.message === "string" &&
    Number.isInteger(value.revision) &&
    (value.revision as number) > 0 &&
    typeof value.updatedAt === "string" &&
    !validateMessage(value.message);
}

function validateMessage(message: string): string | undefined {
  if (!message) return "message cannot be empty";
  if (message.length > MAX_MESSAGE_LENGTH) {
    return `message cannot exceed ${MAX_MESSAGE_LENGTH} characters`;
  }
  if (!ALLOWED_MESSAGE.test(message)) {
    return "message may only contain printable ASCII characters and new lines";
  }
}

async function loadState(): Promise<MessageState> {
  try {
    const parsed: unknown = JSON.parse(await Deno.readTextFile(MESSAGE_FILE));
    if (
      isRecord(parsed) &&
      typeof parsed.message === "string" &&
      typeof parsed.revision === "number" &&
      typeof parsed.updatedAt === "string" &&
      !validateMessage(parsed.message)
    ) {
      return parsed as MessageState;
    }
  } catch (error) {
    if (!(error instanceof Deno.errors.NotFound)) {
      console.error("Could not load message state:", error);
    }
  }

  return {
    message: DEFAULT_MESSAGE,
    revision: 1,
    updatedAt: new Date(0).toISOString(),
  };
}

async function saveState(nextState: MessageState): Promise<void> {
  await saveJson(MESSAGE_FILE, nextState);
}

async function loadDeviceStatus(): Promise<DeviceStatus | null> {
  try {
    const parsed: unknown = JSON.parse(await Deno.readTextFile(DEVICE_STATUS_FILE));
    if (
      isRecord(parsed) &&
      typeof parsed.reportedAt === "string" &&
      !validateDeviceStatus(parsed)
    ) {
      return parsed as DeviceStatus;
    }
  } catch (error) {
    if (!(error instanceof Deno.errors.NotFound)) {
      console.error("Could not load device status:", error);
    }
  }
  return null;
}

async function saveJson(file: URL, value: unknown): Promise<void> {
  await Deno.mkdir(DATA_DIR, { recursive: true });
  const temporaryFile = new URL(`./${file.pathname.split("/").at(-1)}.tmp`, DATA_DIR);
  await Deno.writeTextFile(temporaryFile, `${JSON.stringify(value, null, 2)}\n`);
  await Deno.rename(temporaryFile, file);
}

function json(body: unknown, status = 200, extraHeaders: HeadersInit = {}): Response {
  return Response.json(body, {
    status,
    headers: {
      "cache-control": "no-store",
      ...Object.fromEntries(new Headers(extraHeaders)),
    },
  });
}

function methodNotAllowed(allow: string): Response {
  return new Response("Method not allowed", { status: 405, headers: { allow } });
}

function contentType(fileName: string): string {
  if (fileName.endsWith(".html")) return "text/html; charset=utf-8";
  if (fileName.endsWith(".css")) return "text/css; charset=utf-8";
  return "text/javascript; charset=utf-8";
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null;
}

function isNullableNumber(value: unknown): value is number | null {
  return value === null || (typeof value === "number" && Number.isFinite(value));
}

function cleanText(value: string, maxLength: number): string {
  return [...value]
    .filter((character) => {
      const code = character.charCodeAt(0);
      return code >= 32 && code !== 127;
    })
    .join("")
    .slice(0, maxLength);
}

function readPort(): number {
  const value = Deno.env.get("PORT") ?? "8000";
  const port = Number(value);
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error(`Invalid PORT: ${value}`);
  }
  return port;
}
