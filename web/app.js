// P2P File Transfer Web Client
class P2PClient {
    constructor() {
        // Use window.P2P_BACKEND_URL if set (for split frontend/backend), else default to same origin
        this.serverUrl = window.P2P_BACKEND_URL || window.location.origin;
        this.connected = false;
        this.authenticated = false;
        this.username = '';
        this.peerId = '';
        this.myFiles = [];
        
        this.initElements();
        this.initEventListeners();
        this.loadPeerId();
        this.restoreSession();
    }
    
    initElements() {
        this.elements = {
            username: document.getElementById('username'),
            password: document.getElementById('password'),
            loginBtn: document.getElementById('loginBtn'),
            registerBtn: document.getElementById('registerBtn'),
            logoutBtn: document.getElementById('logoutBtn'),
            authStatus: document.getElementById('authStatus'),
            peerId: document.getElementById('peerId'),
            serverAddress: document.getElementById('serverAddress'),
            connectBtn: document.getElementById('connectBtn'),
            disconnectBtn: document.getElementById('disconnectBtn'),
            uploadArea: document.getElementById('uploadArea'),
            fileInput: document.getElementById('fileInput'),
            uploadBtn: document.getElementById('uploadBtn'),
            uploadProgress: document.getElementById('uploadProgress'),
            progressBar: document.getElementById('progressBar'),
            progressText: document.getElementById('progressText'),
            refreshBtn: document.getElementById('refreshBtn'),
            filesList: document.getElementById('filesList'),
            myFilesList: document.getElementById('myFilesList'),
            activityLog: document.getElementById('activityLog'),
            statusDot: document.getElementById('statusDot'),
            statusText: document.getElementById('statusText')
        };
    }
    
    initEventListeners() {
        this.elements.loginBtn.addEventListener('click', () => this.login());
        this.elements.registerBtn.addEventListener('click', () => this.register());
        this.elements.logoutBtn.addEventListener('click', () => this.logout());

        // Connection
        this.elements.connectBtn.addEventListener('click', () => this.connect());
        this.elements.disconnectBtn.addEventListener('click', () => this.disconnect());
        
        // File upload
        this.elements.fileInput.addEventListener('change', (e) => this.handleFileSelect(e));
        this.elements.uploadBtn.addEventListener('click', () => this.uploadFiles());
        this.elements.refreshBtn.addEventListener('click', () => this.refreshFileList());
        
        // Drag and drop
        this.elements.uploadArea.addEventListener('dragover', (e) => {
            e.preventDefault();
            this.elements.uploadArea.classList.add('dragover');
        });
        
        this.elements.uploadArea.addEventListener('dragleave', () => {
            this.elements.uploadArea.classList.remove('dragover');
        });
        
        this.elements.uploadArea.addEventListener('drop', (e) => {
            e.preventDefault();
            this.elements.uploadArea.classList.remove('dragover');
            this.elements.fileInput.files = e.dataTransfer.files;
            this.handleFileSelect({ target: this.elements.fileInput });
        });
    }
    
    loadPeerId() {
        const saved = localStorage.getItem('p2p_peer_id');
        if (saved) {
            this.elements.peerId.value = saved;
        } else {
            this.elements.peerId.value = 'peer_' + Math.random().toString(36).substr(2, 9);
        }
    }

    async request(path, options = {}) {
        const response = await fetch(`${this.serverUrl}${path}`, {
            credentials: 'same-origin',
            ...options
        });
        const data = await response.json();
        if (!response.ok) {
            throw new Error(data.message || 'Request failed');
        }
        return data;
    }

    async restoreSession() {
        try {
            const data = await this.request('/api/me');
            this.setAuthenticatedUser(data.username);
            this.log(`Signed in as ${data.username}`, 'success');
        } catch (error) {
            this.setAuthenticatedUser('');
        }
    }

    setAuthenticatedUser(username) {
        this.username = username;
        this.authenticated = Boolean(username);
        this.elements.authStatus.textContent = this.authenticated
            ? `Signed in as ${username}`
            : 'Not authenticated';
        this.elements.loginBtn.disabled = this.authenticated;
        this.elements.registerBtn.disabled = this.authenticated;
        this.elements.logoutBtn.disabled = !this.authenticated;
        this.elements.peerId.disabled = !this.authenticated || this.connected;
        this.elements.connectBtn.disabled = !this.authenticated || this.connected;
        this.elements.uploadBtn.disabled = !this.authenticated || !this.connected;
        this.elements.refreshBtn.disabled = !this.authenticated || !this.connected;
        this.setLockedTabs(!this.authenticated);
    }

    setLockedTabs(locked) {
        document.querySelectorAll('.tab-btn[data-tab="upload"], .tab-btn[data-tab="files"]').forEach(btn => {
            btn.disabled = locked;
            btn.title = locked ? 'Sign in first' : '';
        });
        if (locked) {
            document.querySelector('.tab-btn[data-tab="setup"]').click();
        }
    }

    async login() {
        try {
            const data = await this.request('/api/login', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    username: this.elements.username.value,
                    password: this.elements.password.value
                })
            });
            this.setAuthenticatedUser(data.username);
            this.elements.password.value = '';
            this.log(`Signed in as ${data.username}`, 'success');
        } catch (error) {
            this.elements.authStatus.textContent = `Login failed: ${error.message}`;
            this.log(`Login failed: ${error.message}`, 'error');
        }
    }

    async register() {
        try {
            await this.request('/api/register', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    username: this.elements.username.value,
                    password: this.elements.password.value
                })
            });
            this.elements.authStatus.textContent = 'Registration complete. You can now log in.';
            this.log('Registration complete. You can now log in.', 'success');
        } catch (error) {
            this.elements.authStatus.textContent = `Registration failed: ${error.message}`;
            this.log(`Registration failed: ${error.message}`, 'error');
        }
    }

    async logout() {
        if (this.connected) {
            await this.disconnect();
        }

        try {
            await this.request('/api/logout', { method: 'POST' });
        } catch (error) {
            this.log(`Logout failed: ${error.message}`, 'error');
            return;
        }

        this.setAuthenticatedUser('');
        this.log('Signed out', 'info');
    }
    
    async connect() {
        if (!this.authenticated) {
            this.log('Sign in before connecting a peer', 'warning');
            return;
        }

        const peerId = this.elements.peerId.value.trim();
        const serverAddr = this.elements.serverAddress.value.trim();
        
        if (!peerId) {
            this.log('Please enter a peer ID', 'error');
            return;
        }
        
        this.peerId = peerId;
        this.serverUrl = serverAddr.startsWith('http') ? serverAddr : `http://${serverAddr}`;
        
        try {
            const response = await fetch(`${this.serverUrl}/api/connect`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                credentials: 'same-origin',
                body: JSON.stringify({ peer_id: peerId })
            });
            
            const data = await response.json();
            
            if (data.status === 'success') {
                this.connected = true;
                localStorage.setItem('p2p_peer_id', peerId);
                this.updateConnectionUI();
                this.log(`Connected as ${peerId}`, 'success');
                this.refreshFileList();
            } else {
                throw new Error(data.message || 'Connection failed');
            }
        } catch (error) {
            this.log(`Connection failed: ${error.message}`, 'error');
        }
    }
    
    async disconnect() {
        try {
            await fetch(`${this.serverUrl}/api/disconnect`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                credentials: 'same-origin',
                body: JSON.stringify({ peer_id: this.peerId })
            });
        } catch (error) {
            console.error('Disconnect error:', error);
        }
        
        this.connected = false;
        this.updateConnectionUI();
        this.log('Disconnected from server', 'info');
    }
    
    updateConnectionUI() {
        if (this.connected) {
            this.elements.statusDot.classList.add('connected');
            this.elements.statusText.textContent = `Connected as ${this.peerId}`;
            this.elements.connectBtn.disabled = true;
            this.elements.disconnectBtn.disabled = false;
            this.elements.uploadBtn.disabled = false;
            this.elements.refreshBtn.disabled = false;
            this.elements.peerId.disabled = true;
            this.elements.serverAddress.disabled = true;
        } else {
            this.elements.statusDot.classList.remove('connected');
            this.elements.statusText.textContent = 'Disconnected';
            this.elements.connectBtn.disabled = !this.authenticated;
            this.elements.disconnectBtn.disabled = true;
            this.elements.uploadBtn.disabled = !this.authenticated;
            this.elements.refreshBtn.disabled = !this.authenticated;
            this.elements.peerId.disabled = !this.authenticated;
            this.elements.serverAddress.disabled = false;
        }
    }
    
    handleFileSelect(event) {
        const files = event.target.files;
        if (files.length > 0) {
            const fileNames = Array.from(files).map(f => f.name).join(', ');
            this.log(`Selected: ${fileNames}`, 'info');
            this.elements.uploadBtn.disabled = !this.connected;
        }
    }
    
    async uploadFiles() {
        const files = this.elements.fileInput.files;
        if (files.length === 0) {
            this.log('No files selected', 'warning');
            return;
        }
        
        this.elements.uploadProgress.style.display = 'block';
        this.elements.uploadBtn.disabled = true;
        
        let uploadedCount = 0;
        
        for (let i = 0; i < files.length; i++) {
            const file = files[i];
            const formData = new FormData();
            formData.append('file', file);
            formData.append('peer_id', this.peerId);
            
            try {
                const xhr = new XMLHttpRequest();
                
                xhr.upload.addEventListener('progress', (e) => {
                    if (e.lengthComputable) {
                        const percent = Math.round((e.loaded / e.total) * 100);
                        this.updateProgress(percent);
                    }
                });
                
                const uploadPromise = new Promise((resolve, reject) => {
                    xhr.addEventListener('load', () => {
                        if (xhr.status === 200) {
                            resolve(JSON.parse(xhr.responseText));
                        } else {
                            reject(new Error('Upload failed'));
                        }
                    });
                    xhr.addEventListener('error', () => reject(new Error('Upload error')));
                    xhr.open('POST', `${this.serverUrl}/api/upload`);
                    xhr.withCredentials = true;
                    xhr.send(formData);
                });
                
                const result = await uploadPromise;
                
                if (result.status === 'success') {
                    uploadedCount++;
                    this.myFiles.push({ name: file.name, size: file.size });
                    this.log(`✓ Uploaded: ${file.name}`, 'success');
                } else {
                    this.log(`✗ Failed: ${file.name}`, 'error');
                }
            } catch (error) {
                this.log(`✗ Error uploading ${file.name}: ${error.message}`, 'error');
            }
        }
        
        this.log(`Upload complete: ${uploadedCount}/${files.length} files`, 'success');
        this.elements.uploadProgress.style.display = 'none';
        this.elements.uploadBtn.disabled = false;
        this.elements.fileInput.value = '';
        this.updateMyFilesList();
        this.refreshFileList();
    }
    
    updateProgress(percent) {
        this.elements.progressBar.style.width = percent + '%';
        this.elements.progressText.textContent = percent + '%';
    }
    
    async refreshFileList() {
        if (!this.connected) return;
        
        try {
            const response = await fetch(`${this.serverUrl}/api/list`, {
                credentials: 'same-origin'
            });
            const data = await response.json();
            
            if (data.status === 'success') {
                this.displayFilesList(data.files);
                this.log('File list refreshed', 'info');
            }
        } catch (error) {
            this.log(`Failed to refresh file list: ${error.message}`, 'error');
        }
    }
    
    displayFilesList(files) {
        if (files.length === 0) {
            this.elements.filesList.innerHTML = '<p class="empty-state">No files available</p>';
            return;
        }
        
        this.elements.filesList.innerHTML = files.map(file => `
            <div class="file-item">
                <div class="file-info">
                    <div class="file-name">📄 ${file.filename}</div>
                    <div class="file-meta">
                        Owner: ${file.owner} | Size: ${this.formatSize(file.size)}
                    </div>
                </div>
                <button class="btn btn-download" onclick="client.downloadFile('${file.filename}', '${file.owner}')">
                    ⬇️ Download
                </button>
            </div>
        `).join('');
    }
    
    updateMyFilesList() {
        if (this.myFiles.length === 0) {
            this.elements.myFilesList.innerHTML = '<p class="empty-state">No files shared yet</p>';
            return;
        }
        
        this.elements.myFilesList.innerHTML = this.myFiles.map(file => `
            <div class="file-item">
                <div class="file-info">
                    <div class="file-name">📄 ${file.name}</div>
                    <div class="file-meta">Size: ${this.formatSize(file.size)}</div>
                </div>
            </div>
        `).join('');
    }
    
    async downloadFile(filename, owner) {
        try {
            this.log(`Downloading ${filename} from ${owner}...`, 'info');
            
            const url = `${this.serverUrl}/api/download?filename=${encodeURIComponent(filename)}&owner=${encodeURIComponent(owner)}`;
            const a = document.createElement('a');
            a.href = url;
            a.download = filename;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            
            this.log(`✓ Downloaded: ${filename}`, 'success');
        } catch (error) {
            this.log(`✗ Download failed: ${error.message}`, 'error');
        }
    }
    
    formatSize(bytes) {
        if (bytes < 1024) return bytes + ' B';
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(2) + ' KB';
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(2) + ' MB';
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
    }
    
    log(message, type = 'info') {
        console.log(`[${type}] ${message}`);
        if (!this.elements.activityLog) return;

        const timestamp = new Date().toLocaleTimeString();
        const logEntry = document.createElement('div');
        logEntry.className = `log-entry ${type}`;
        logEntry.textContent = `[${timestamp}] ${message}`;
        
        this.elements.activityLog.insertBefore(logEntry, this.elements.activityLog.firstChild);
        
        // Keep only last 50 entries
        while (this.elements.activityLog.children.length > 50) {
            this.elements.activityLog.removeChild(this.elements.activityLog.lastChild);
        }
    }
}

// Initialize the client
const client = new P2PClient();

// Simple tab switching
document.querySelectorAll('.tab-btn').forEach(btn => {
    btn.addEventListener('click', () => {
        if (btn.disabled) return;
        document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
        document.querySelectorAll('.tab-panel').forEach(p => p.classList.remove('active'));
        btn.classList.add('active');
        document.getElementById(`tab-${btn.dataset.tab}`).classList.add('active');
    });
});
client.setLockedTabs(!client.authenticated);
