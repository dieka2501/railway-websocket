'use strict';

require('dotenv').config();
const http = require('http');
const path = require('path');
const { createHmac, timingSafeEqual } = require('crypto');
const express = require('express');
const WebSocket = require('ws');

const PORT = Number(process.env.PORT) || 3000;
const DEFAULT_DEVICE_ID = 'esp32-ac-01';
const FRAME_SIZE = 128 * 32;
const SESSION_COOKIE = 'gateway_session';
const SESSION_TTL_MS = 8 * 60 * 60 * 1000;

const app = express();
app.set('trust proxy', 1);
app.use(express.urlencoded({ extended: false, limit: '2kb' }));
const server = http.createServer(app);
const wss = new WebSocket.Server({ noServer: true, maxPayload: 16 * 1024 });

// Keep browser and device connections separate so the transport layer can serve
// both Drawbook messages today and AC commands later.
const browsers = new Set();
const devices = new Map();

app.use((req, res, next) => {
  console.log(`[HTTP] ${req.method} ${req.path}`);
  next();
});
app.get('/', (req, res) => {
  res.redirect(302, '/login');
});
app.get('/login', (req, res) => {
  if (readSession(req)) {
    res.redirect(302, '/drawbook');
    return;
  }
  res.sendFile(path.join(__dirname, 'public', 'login.html'));
});
app.post('/login', (req, res) => {
  if (!safeSecretMatch(req.body.secret_key, process.env.SECRET_KEY)) {
    res.redirect(303, '/login?error=1');
    return;
  }
  const token = createSession();
  res.cookie(SESSION_COOKIE, token, {
    httpOnly: true,
    secure: req.secure,
    sameSite: 'strict',
    maxAge: SESSION_TTL_MS,
    path: '/'
  });
  res.redirect(303, '/drawbook');
});
app.post('/logout', (req, res) => {
  res.clearCookie(SESSION_COOKIE, {
    httpOnly: true,
    secure: req.secure,
    sameSite: 'strict',
    path: '/'
  });
  res.redirect(303, '/login');
});
app.get('/drawbook', (req, res) => {
  if (!readSession(req)) {
    res.redirect(302, '/login');
    return;
  }
  res.sendFile(path.join(__dirname, 'public', 'drawbook.html'));
});
app.get('/health', (req, res) => {
  res.json({ status: 'ok', uptime: process.uptime() });
});
app.get('/status', (req, res) => {
  if (!safeSecretMatch(req.get('x-secret-key'), process.env.SECRET_KEY)) {
    res.status(401).json({ status: 'error', message: 'Unauthorized' });
    return;
  }
  res.json({
    status: 'ok',
    browsers: browsers.size,
    devices: devices.size,
    device_ids: Array.from(devices.keys()),
    uptime: process.uptime()
  });
});

function safeSecretMatch(provided, expected) {
  if (typeof provided !== 'string' || typeof expected !== 'string' || !expected) return false;
  const providedBuffer = Buffer.from(provided, 'utf8');
  const expectedBuffer = Buffer.from(expected, 'utf8');
  return providedBuffer.length === expectedBuffer.length && timingSafeEqual(providedBuffer, expectedBuffer);
}

function signSession(payload) {
  return createHmac('sha256', process.env.SECRET_KEY || '').update(payload).digest('base64url');
}

function createSession() {
  const payload = String(Date.now() + SESSION_TTL_MS);
  return `${payload}.${signSession(payload)}`;
}

function verifySession(token) {
  if (typeof token !== 'string') return false;
  const [payload, signature, extra] = token.split('.');
  if (!payload || !signature || extra !== undefined || !/^\d+$/.test(payload)) return false;
  const expectedSignature = Buffer.from(signSession(payload));
  const providedSignature = Buffer.from(signature);
  if (providedSignature.length !== expectedSignature.length || !timingSafeEqual(providedSignature, expectedSignature)) return false;
  return Number(payload) > Date.now();
}

function parseCookies(header = '') {
  const cookies = new Map();
  for (const part of header.split(';')) {
    const separator = part.indexOf('=');
    if (separator === -1) continue;
    const name = part.slice(0, separator).trim();
    const value = part.slice(separator + 1).trim();
    if (name) cookies.set(name, value);
  }
  return cookies;
}

function readSession(request) {
  try {
    return verifySession(parseCookies(request.headers.cookie).get(SESSION_COOKIE));
  } catch (_error) {
    return false;
  }
}

function hasSameOrigin(request) {
  if (!request.headers.origin) return false;
  const forwardedProtocol = request.headers['x-forwarded-proto'];
  const protocol = (forwardedProtocol ? forwardedProtocol.split(',')[0].trim() : (request.socket.encrypted ? 'https' : 'http'));
  try {
    return new URL(request.headers.origin).origin === `${protocol}://${request.headers.host}`;
  } catch (_error) {
    return false;
  }
}

server.on('upgrade', (request, socket, head) => {
  const pathname = new URL(request.url, 'http://localhost').pathname;
  if (pathname !== '/ws') {
    socket.write('HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n');
    socket.destroy();
    return;
  }

  const browserCookie = parseCookies(request.headers.cookie).get(SESSION_COOKIE);
  const browserSession = verifySession(browserCookie);
  if (request.headers.origin && (!browserSession || !hasSameOrigin(request))) {
    socket.write('HTTP/1.1 401 Unauthorized\r\nConnection: close\r\n\r\n');
    socket.destroy();
    return;
  }
  if (browserSession && !hasSameOrigin(request)) {
    socket.write('HTTP/1.1 401 Unauthorized\r\nConnection: close\r\n\r\n');
    socket.destroy();
    return;
  }
  request.browserSessionValid = browserSession;

  wss.handleUpgrade(request, socket, head, (ws) => {
    wss.emit('connection', ws, request);
  });
});

function sendJson(ws, message) {
  if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(message));
  }
}

function broadcastToBrowsers(message) {
  const payload = JSON.stringify(message);
  for (const browser of browsers) {
    if (browser.readyState === WebSocket.OPEN) browser.send(payload);
  }
}

function removeDevice(ws) {
  if (!ws.deviceId) return;
  const deviceId = ws.deviceId;
  // A reconnect may have replaced this socket; only remove the current owner.
  if (devices.get(deviceId) === ws) {
    devices.delete(deviceId);
    console.log(`[WS] Device disconnected: ${deviceId}`);
    broadcastToBrowsers({ type: 'device_status', device_id: deviceId, status: 'offline' });
  }
  delete ws.deviceId;
}

function handleMessage(ws, raw) {
  let message;
  try {
    message = JSON.parse(raw.toString());
  } catch (_error) {
    sendJson(ws, { type: 'error', message: 'Invalid JSON' });
    return;
  }

  if (!message || typeof message !== 'object' || Array.isArray(message)) {
    sendJson(ws, { type: 'error', message: 'Invalid message' });
    return;
  }

  if (message.type === 'device_register') {
    const expectedToken = process.env.DEVICE_TOKEN;
    if (!safeSecretMatch(message.token, expectedToken)) {
      sendJson(ws, { type: 'error', message: 'Invalid device token' });
      ws.close(1008, 'Invalid device token');
      return;
    }
    if (typeof message.device_id !== 'string' || !/^[a-zA-Z0-9_-]{1,64}$/.test(message.device_id)) {
      sendJson(ws, { type: 'error', message: 'Invalid device_id' });
      ws.close(1008, 'Invalid device_id');
      return;
    }

    if (browsers.has(ws)) {
      sendJson(ws, { type: 'error', message: 'Browser connection cannot register as device' });
      ws.close(1008, 'Connection role already authenticated');
      return;
    }
    clearTimeout(ws.authTimeout);
    const deviceId = message.device_id;
    const previous = devices.get(deviceId);
    if (previous && previous !== ws) previous.close(1000, 'Replaced by a new connection');
    ws.deviceId = deviceId;
    devices.set(deviceId, ws);
    console.log(`[WS] Device registered: ${deviceId}`);
    broadcastToBrowsers({ type: 'device_status', device_id: deviceId, status: 'online' });
    sendJson(ws, { type: 'device_status', device_id: deviceId, status: 'online' });
    return;
  }

  if (ws.deviceId) {
    if (message.type === 'device_status') {
      broadcastToBrowsers({
        type: 'device_status',
        device_id: ws.deviceId,
        status: typeof message.status === 'string' ? message.status : 'online'
      });
    } else {
      console.log(`[WS] Device message received: ${message.type || 'unknown'}`);
      broadcastToBrowsers(message);
    }
    return;
  }

  if (!browsers.has(ws)) {
    sendJson(ws, { type: 'error', message: 'Authentication required' });
    ws.close(1008, 'Authentication required');
    return;
  }

  if (message.type === 'draw') {
    if (typeof message.data !== 'string' || message.data.length !== FRAME_SIZE || !/^[01]+$/.test(message.data)) {
      sendJson(ws, { type: 'error', message: 'Draw data must be a 4096-character pixel string' });
      return;
    }
    console.log('[WS] Draw message received');
    const device = devices.get(DEFAULT_DEVICE_ID);
    if (!device || device.readyState !== WebSocket.OPEN) {
      sendJson(ws, { type: 'error', message: 'Device offline' });
      return;
    }
    sendJson(device, message);
    console.log(`[WS] Draw forwarded to ${DEFAULT_DEVICE_ID}`);
    return;
  }

  sendJson(ws, { type: 'error', message: `Unsupported message type: ${String(message.type || 'unknown')}` });
}

wss.on('connection', (ws, request) => {
  if (request.browserSessionValid) {
    ws.isBrowser = true;
    browsers.add(ws);
    console.log('[WS] Browser connected');
    sendJson(ws, { type: 'auth_success' });
    for (const deviceId of devices.keys()) {
      sendJson(ws, { type: 'device_status', device_id: deviceId, status: 'online' });
    }
  }
  ws.authTimeout = setTimeout(() => {
    if (!ws.deviceId && !ws.isBrowser) {
      sendJson(ws, { type: 'error', message: 'Authentication required' });
      ws.close(1008, 'Authentication required');
    }
  }, 5000);
  ws.on('message', (raw) => handleMessage(ws, raw));
  ws.on('close', () => {
    clearTimeout(ws.authTimeout);
    browsers.delete(ws);
    removeDevice(ws);
  });
  ws.on('error', (error) => console.error('[WS] Connection error:', error.message));
});

server.listen(PORT, '0.0.0.0', () => {
  console.log(`[HTTP] Server listening on 0.0.0.0:${PORT}`);
});
