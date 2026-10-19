# ESP32 AC Gateway R&D

Small Node.js gateway for testing browser-to-server WebSocket communication before an ESP32-S3 is connected. The Drawbook is a communication test client; the same `/ws` layer can later route AC commands.

## Requirements

- Node.js 20 or newer
- npm

## Run locally

```sh
cp -n .env.example .env
# Replace both example values with separate outputs from: openssl rand -hex 32
npm install
npm run dev
```

Open [http://localhost:3000](http://localhost:3000); the root redirects to `/login`. After login, the server redirects to `/drawbook`. Opening `/drawbook` without a valid session also redirects to `/login`. Run `npm run dev` for development. Its Bash launcher stops the previous instance started by the launcher, then starts Node's watch mode, which restarts the server when source files change. Press `Ctrl+C` to stop it. For a production-style local start, use `npm start`.

Login submits the `SECRET_KEY` to the server over HTTP(S). A successful login sets a signed, HTTP-only, same-site session cookie that expires after eight hours. The Drawbook route and its browser WebSocket both require this session. On Railway, use HTTPS so the session cookie is Secure and the browser uses `wss://<railway-domain>/ws`; local HTTP uses `ws://localhost:3000/ws`.

## Check endpoints

- `GET http://localhost:3000/health` returns `{ "status": "ok", "uptime": ... }`.
- `GET http://localhost:3000/status` returns browser/device counts, device IDs, and uptime. It requires the secret key in the `x-secret-key` header:

  ```sh
  curl -H "x-secret-key: $SECRET_KEY" http://localhost:3000/status
  ```

- `GET /health` stays public for service health checks and returns no device details.

## Device registration

Set both values in `.env`. The browser authenticates through `/login`; the cookie session authenticates its WebSocket connection. A device connects to `/ws` and sends:

```json
{"type":"device_register","device_id":"esp32-ac-01","token":"your-device-token"}
```

The server rejects missing or incorrect keys/tokens. Keep `.env` and real secrets out of Git; `.env` is ignored. Draw frames contain 4096 ASCII `0`/`1` characters. Frames are sent when a stroke ends or when Send is clicked. The server forwards them to `esp32-ac-01`; while it is offline an authenticated browser receives `{"type":"error","message":"Device offline"}`.

## Deploy to Railway

1. Push this project to a GitHub repository.
2. In Railway, create a project and deploy the repository as a Node.js service.
3. Set `SECRET_KEY` and `DEVICE_TOKEN` in Railway's Variables panel to separate long random values. Do not commit `.env` or put secrets in source code. Railway supplies `PORT`; the server listens on that port at `0.0.0.0`.
4. Deploy with the default start command `npm start` (also set the service start command to `npm start` if Railway does not detect it automatically).
5. In the service's Networking settings, generate a public domain. Use that domain to open `https://<railway-domain>/` and check `https://<railway-domain>/health`. To check `/status`, send the `x-secret-key` header as shown above.
6. The WebSocket URL is `wss://<railway-domain>/ws`; Drawbook builds this URL from the current page automatically.

No database, Dockerfile, or firmware changes are required for this test stage.
