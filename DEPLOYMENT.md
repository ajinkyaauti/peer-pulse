# Deployment Guide

This project can run in three ways:

| Mode | Best for | UI URL | Backend |
|------|----------|--------|---------|
| **Docker locally** | Development, full P2P (ports 8080/8081) | `http://localhost:5000` | Same container |
| **Native locally** | Development without Docker (recommended) | `http://localhost:5050` | Windows/native C++ + Flask |
| **Render only** | One public URL, simplest cloud demo | `https://YOUR-SERVICE.onrender.com` | Same container (Flask serves `web/`) |
| **GitHub Pages + Render** | "Pretty" static site + your API | `https://USER.github.io/REPO/` | Render Docker service |

> **Note:** Local development now uses port **5050** (not 5000) to avoid conflicts with Docker Desktop/WSL2 port shadowing on Windows machines.

Example GitHub remote for this repo: `https://github.com/ajinkyaauti/peer-pulse`  
→ Pages URL: `https://ajinkyaauti.github.io/peer-pulse/`

---

## What runs where

```
Browser
   │
   ├─ Same origin (localhost or Render URL)
   │     Flask :5000  →  REST /api/*, sessions, SQLite auth
   │     p2p_server   →  TCP 8080 (tracker), TLS 8081 (file bytes)
   │
   └─ Split (GitHub Pages + Render)
         Static: index.html, app.js, style.css  (github.io)
         API calls: fetch(..., credentials: 'include')  →  Render HTTPS
```

- **SQLite (`P2P_AUTH_DB`)** — user accounts only (register/login).
- **Sessions** — Flask cookie after login; required for connect/upload/list.
- **File list registry** — in-memory on the server; **uploaded files** — `uploads/` on disk.

---

## Option A — Native local development (recommended for Windows; no Docker)

Run the C++ tracker and Flask web server directly on your machine on separate ports.

### 1. Install dependencies

```bash
cd <project-root>
source venv/Scripts/activate
pip install -r requirements.txt
```

*(Replace `<project-root>` with your actual project directory)*

### 2. Create `.env` in project root

Already created during setup; contains:
```env
FLASK_SECRET_KEY=<your-secret-key-here>
P2P_WEB_PORT=5050
```

The `.env` file is auto-loaded by Flask via `python-dotenv` (no manual env var needed).

### 3. Build and start C++ server (Terminal 1)

```bash
cmake -S . -B build -G Ninja
cmake --build build --target p2p_server
build/p2p_server.exe
```

### 4. Start Flask web server (Terminal 2)

```bash
source venv/Scripts/activate
python web_server.py
```

Server listens on **http://127.0.0.1:5050** (avoids IPv6 shadowing by Docker/WSL).

### 5. Open browser

```
http://localhost:5050
```

Or use the batch file:
```bash
start.bat
```

---

## Option B — Docker on your machine (full containerized)

Works on Windows, macOS, and Linux. Exposes **5000**, **8080**, and **8081** (no port shadowing inside the container).

### 1. Create `.env` in the project root (if not exists)

```env
FLASK_SECRET_KEY=replace-with-a-long-random-string
P2P_WEB_PORT=5000
```

PowerShell (generate a hex secret):

```powershell
"FLASK_SECRET_KEY=$((1..32 | ForEach-Object { '{0:x2}' -f (Get-Random -Maximum 256) }) -join '')" | Out-File -Encoding ascii .env
```

### 2. Start

```powershell
docker compose up --build
```

Open **http://localhost:5000**. Leave `window.P2P_BACKEND_URL` unset in `web/index.html`.

Do **not** set `P2P_CORS_ORIGINS` for local same-origin use.

Data persists via Docker volumes: `authdb` (SQLite), `uploads` (files).

---

## Option C — Render only (one URL, no GitHub Pages)

The Docker image already includes the web UI. You do not need GitHub Pages unless you want the frontend on `github.io`.

### 1. Push `main` to GitHub

Render deploys from the branch you select (usually `main`). Merge deployment-related branches before relying on CI/Pages.

### 2. Create the service on Render

1. [Render Dashboard](https://dashboard.render.com) → **New** → **Blueprint**.
2. Connect the repo (e.g. `ajinkyaauti/peer-pulse`).
3. Apply **`render.yaml`** (Docker, free plan).

Or **New → Web Service**, connect repo, set **Language: Docker**, Dockerfile path `./Dockerfile`.

### 3. Environment variables (Render → Environment)

| Variable | Required | Notes |
|----------|----------|--------|
| `FLASK_SECRET_KEY` | Yes | Auto-generated if using blueprint |
| `P2P_WEB_DEBUG` | No | Default `false` |
| `P2P_AUTH_DB` | No | Default `/app/data/p2p_auth.db` in container |
| `P2P_CORS_ORIGINS` | Only for Pages split | See Option C |

### 4. Use the app

Open `https://<your-service>.onrender.com` — same UI as local Docker.

**Free tier:** service sleeps after ~15 minutes without traffic; next request may take ~1 minute to wake. **Ephemeral filesystem:** SQLite and uploads are lost on redeploy/restart unless you upgrade to a **paid** instance and attach a [persistent disk](https://render.com/docs/disks) (not available on free).

---

## Option D — GitHub Pages (UI) + Render (API)

Use this when you want the public site on **github.io** and the API on **Render**.

### Step 1 — Deploy backend on Render

Complete **Option B** first. Copy your service URL, e.g. `https://p2p-file-transfer-xxxx.onrender.com`.

### Step 2 — Enable GitHub Pages

1. Repo → **Settings** → **Pages**.
2. **Build and deployment** → **Source: GitHub Actions**.
3. Ensure workflow `.github/workflows/deploy-pages.yml` exists on **`main`**.

The workflow runs when you push changes under `web/**` or the workflow file to **`main`**, or when you run it manually (**Actions** → **Deploy Frontend to GitHub Pages** → **Run workflow**).

Site URL pattern: `https://<github-user>.github.io/<repo-name>/`

### Step 3 — Point the UI at Render

Edit `web/index.html` (before `app.js`):

```html
<script>
    window.P2P_BACKEND_URL = 'https://YOUR-SERVICE.onrender.com';
</script>
```

Commit and push to **`main`**. Wait for the Pages workflow to finish.

When `P2P_BACKEND_URL` is set, the **Setup → Server Address** field is prefilled and all API calls (login, connect, upload) use Render, not `github.io`.

### Step 4 — CORS + cookies (required for login from Pages)

GitHub Pages and Render are different origins. The server must allow your Pages origin and send cross-site cookies.

In Render → **Environment**, set:

```text
P2P_CORS_ORIGINS=https://YOUR_GITHUB_USERNAME.github.io
```

Rules:

- **Origin only** — scheme + host, **no path**, no trailing slash.
- Example: site `https://ajinkyaauti.github.io/peer-pulse/` → origin `https://ajinkyaauti.github.io`.
- Multiple origins: comma-separated.

Redeploy after changing env vars.

Implementation (already in `web_server.py` when `P2P_CORS_ORIGINS` is set):

- `flask-cors` with `supports_credentials=True`
- Session cookie `SameSite=None; Secure`

The client uses `credentials: 'include'` on API requests (`web/app.js`).

### Step 5 — Verify

1. Open the **Pages** URL (not Render) in the browser.
2. **F12 → Network**: `/api/register`, `/api/login`, `/api/me` should go to `onrender.com`.
3. Register → Login → **Setup** → Connect (peer ID) → **Upload** → **Files** → Refresh.

If login succeeds but later calls return 401, check `P2P_CORS_ORIGINS` and that `FLASK_SECRET_KEY` is stable across redeploys (auto-generated once per service is fine).

---

## Environment reference

| Variable | Default | Purpose |
|----------|---------|---------|
| `FLASK_SECRET_KEY` | random (dev) | Signs session cookies |
| `P2P_WEB_HOST` | `0.0.0.0` | Flask bind address |
| `P2P_WEB_PORT` | `5000` | Flask port (Render maps HTTP to this) |
| `P2P_WEB_DEBUG` | `false` | Never `true` in production |
| `P2P_AUTH_DB` | `p2p_auth.db` (local) / `/app/data/p2p_auth.db` (Docker) | SQLite auth database path |
| `P2P_CORS_ORIGINS` | unset | Required for GitHub Pages + Render; comma-separated allowed origins |

---

## Known limitations

### Render free tier

- Sleep/spin-up delay after idle.
- **No persistent disk** — accounts and uploads do not survive redeploy or restart reliably.
- For durable auth/files on Render, use a **paid** web service + persistent disk, or external storage / Postgres (not built into this app today).

### Public TCP/TLS (8080 / 8081) on Render

Render’s public URL only proxies **HTTP to port 5000**. Other users’ browsers **cannot** open TCP/TLS to your container’s 8080/8081 on the internet.

What still works on Render:

- Web UI and REST API (login, connect, upload, list) through Flask.
- C++ tracker inside the container talking to `localhost:8080`.

What does **not** work as “true internet P2P” between two home PCs via Render alone:

- Direct peer-to-peer file bytes on 8081 between external clients.

For that, use **Docker locally** or a **VPS** with ports 8080/8081 published.

### GitHub Pages

- Deploy workflow targets branch **`main`** only (not `feature_ui` until merged).
- Public repo required for free Pages on personal accounts.
- Static hosting only — no Python, no SQLite, no uploads on GitHub.

### UI / API quirks (current code)

- Activity log panel is commented out in `index.html`; messages go to the browser console.
- Download uses a link navigation; cross-origin download may need a cookie-aware `fetch` + blob save if you hit auth errors on download from Pages.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|----------------|-----|
| `Failed to fetch` from Pages | Backend asleep, wrong URL, or CORS | Wake Render URL in a tab; check `P2P_BACKEND_URL`; set `P2P_CORS_ORIGINS` |
| CORS error in console | Missing/wrong origin | Match exact `https://user.github.io` in `P2P_CORS_ORIGINS` |
| Login OK, then 401 on connect | Cookie not sent cross-origin | CORS + `credentials: 'include'`; HTTPS only on both sides |
| API hits `github.io` | `P2P_BACKEND_URL` not set or Pages not redeployed | Set script in `index.html`, push `main`, wait for Actions |
| Pages 404 | Pages not using Actions or workflow failed | Settings → Pages → GitHub Actions; check Actions tab |
| Render build fails | Docker/CMake/OpenSSL | Read build logs; build locally with `docker compose build` |
| Blueprint rejects `disk` on free | Persistent disks need paid plan | Use current `render.yaml` (no disk) or upgrade plan |
| Accounts vanished after redeploy | Free tier ephemeral disk | Expected on free; use paid disk or self-host Docker |

---

## Updating production

**Frontend (Pages):**

```powershell
git add web/
git commit -m "Update web UI"
git push origin main
```

**Backend (Render):** push changes to the branch Render watches; Render rebuilds the Docker image automatically.

---

## Quick decision guide

- **Just want it working on your PC with full ports** → Option A (Docker Compose).
- **One link to share, okay with Render limits** → Option B (Render URL only).
- **Marketing site on github.io + API on Render** → Option C (Pages + `P2P_BACKEND_URL` + `P2P_CORS_ORIGINS`).

Further reading: [Render docs](https://render.com/docs), [GitHub Pages](https://docs.github.com/en/pages).
