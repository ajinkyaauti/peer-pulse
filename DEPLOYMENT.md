# Deployment Guide: GitHub Pages (Frontend) + Render (Backend)

This guide explains how to deploy the P2P File Transfer app across two services:
- **Frontend**: Static HTML/CSS/JS hosted on GitHub Pages (free)
- **Backend**: Flask API + C++ server hosted on Render (free tier available)

## Architecture

```
GitHub Pages (Frontend)                    Render (Backend)
  index.html                               web_server.py (Flask)
  app.js ──────── fetch() ───────────────> Port 5000 (HTTP)
  style.css                                 
                                           build/p2p_server (C++)
                                           Ports 8080/8081 (TCP)
```

When deployed:
- Frontend is at: `https://YOUR_USERNAME.github.io/p2p-file-transfer/`
- Backend is at: `https://your-render-app.onrender.com/`
- Frontend JS calls backend via CORS-enabled API endpoints

## Prerequisites

1. GitHub account with a repository
2. Render account (free tier: https://render.com)
3. Repository pushed to GitHub

## Step 1: Push to GitHub

```bash
git remote add origin https://github.com/YOUR_USERNAME/p2p-file-transfer.git
git branch -M main
git push -u origin main
```

## Step 2: Deploy Frontend to GitHub Pages

### Option A: Automatic (GitHub Actions)

The `.github/workflows/deploy-pages.yml` workflow automatically deploys the `web/` folder when you push changes to `main`.

1. Go to your GitHub repo → **Settings** → **Pages**
2. Under "Build and deployment", select:
   - **Source**: GitHub Actions
   - Accept the default settings
3. Push changes to trigger the workflow:
   ```bash
   git add .
   git commit -m "Deploy frontend"
   git push
   ```
4. Frontend is now live at: `https://YOUR_USERNAME.github.io/p2p-file-transfer/`

### Option B: Manual

If you prefer, deploy the `web/` folder manually:
1. Create an orphan `gh-pages` branch
2. Copy only the `web/` folder contents to the root
3. Push to `gh-pages` branch

## Step 3: Deploy Backend to Render

### 1. Create a Render Account & Link GitHub

- Sign up at https://render.com
- Click "New +" → "Web Service"
- Connect your GitHub account and select this repository

### 2. Configure the Service

Render will detect `render.yaml` and auto-configure. Verify these settings:

| Field | Value |
|-------|-------|
| Name | `p2p-file-transfer` |
| Environment | Docker |
| Branch | `main` |
| Dockerfile Path | `./Dockerfile` |
| Plan | Free (or paid if you need better uptime/performance) |

### 3. Environment Variables

Render auto-generates:
- `FLASK_SECRET_KEY` — random, secure key for Flask sessions
- `P2P_WEB_DEBUG` — set to `false` (production safe)

Add any custom vars if needed in the Render dashboard.

### 4. Deploy

Click **Create Web Service**. Render will:
- Build the Docker image (compile C++ server, install Python deps)
- Start the container
- Expose the Flask app on port 5000 (HTTPS)

Your backend is now at: `https://your-render-app.onrender.com/`

⚠️ **Note**: Free tier on Render spins down after 15 minutes of inactivity. First request after spin-down takes ~30s.

## Step 4: Connect Frontend to Backend

### For Split Deployment (GitHub Pages + Render)

Edit `web/index.html` and uncomment the backend URL:

```javascript
<script>
    window.P2P_BACKEND_URL = 'https://your-render-app.onrender.com';
</script>
```

Replace `your-render-app.onrender.com` with your actual Render service URL.

### For Same-Origin Deployment (Docker Compose locally)

Leave `window.P2P_BACKEND_URL` commented out. The app will default to `window.location.origin` (same server).

## Testing

### Local Testing (Docker Compose)

```bash
echo "FLASK_SECRET_KEY=$(openssl rand -hex 32)" > .env
docker compose up --build -d
# Visit http://localhost:5000
```

### Production Testing (GitHub Pages + Render)

1. Wait for GitHub Actions workflow to complete (check Actions tab)
2. Open `https://YOUR_USERNAME.github.io/p2p-file-transfer/` in your browser
3. Check browser console (F12) for CORS errors or failed API calls
4. Login/register and test upload functionality

## Troubleshooting

### "Failed to fetch" errors in browser console

**Cause**: Backend not reachable or CORS misconfigured  
**Fix**:
1. Verify `window.P2P_BACKEND_URL` is correct in `web/index.html`
2. Check that Render service is running (Dashboard → Services)
3. Verify `flask-cors` is installed (`pip list | grep flask-cors`)

### Render service fails to start

**Cause**: Docker build error or missing dependencies  
**Fix**:
1. Check Render build logs (Dashboard → Service → Logs)
2. Verify `requirements.txt` has all Python deps
3. Ensure `CMakeLists.txt` has OpenSSL linkage (already fixed in this repo)

### GitHub Pages shows 404

**Cause**: Files not deployed or wrong path  
**Fix**:
1. Check GitHub Actions workflow ran successfully (Actions tab)
2. Verify settings: **Settings** → **Pages** → Source is "GitHub Actions"
3. Check repo is public (Pages requires public repos on free accounts)

### Session/Auth not persisting across requests

**Cause**: `FLASK_SECRET_KEY` environment variable not set  
**Fix**: Render dashboard → Service → Environment → check `FLASK_SECRET_KEY` exists

## Redeployment & Updates

### Update Frontend

```bash
# Edit web/index.html, app.js, style.css
git add web/
git commit -m "Update frontend"
git push origin main
```
GitHub Actions automatically redeploys within 1–2 minutes.

### Update Backend

```bash
# Edit src/, web_server.py, Dockerfile, etc.
git add .
git commit -m "Update backend"
git push origin main
```
Render automatically rebuilds & redeploys.

## Known Limitations

1. **Peer-to-peer TCP transfer (ports 8080/8081) on Render**  
   - Render's free/standard web services only expose port 5000 (HTTP) publicly
   - Raw TCP/TLS peer transfer between external clients won't work
   - Internal server logic works fine (file registry, API endpoints)
   - Solution: Use a VPS for full P2P support, or accept that peer transfers happen only within the container

2. **Render Free Tier Spin-Down**  
   - Services without traffic spin down after 15 minutes
   - First request after spin-down takes ~30s to start
   - Solution: Upgrade to Paid plan for instant response

3. **GitHub Pages size limit**  
   - GitHub Pages repos limited to 1GB
   - Your `web/` folder is tiny, so no issue here

## Next Steps

- **Scale**: Upgrade Render plan for better uptime / faster startups
- **Domain**: Point a custom domain to Render backend + GitHub Pages frontend
- **HTTPS**: Both Render and GitHub Pages provide free HTTPS by default
- **Monitoring**: Set up Render alerts for uptime
- **Backup**: Regularly back up SQLite DB from Render's persistent disk

---

For questions: Check Render docs (https://render.com/docs) and GitHub Pages docs (https://docs.github.com/en/pages).
