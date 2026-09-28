"""
P2P File Transfer Web Server
A Flask-based web interface for the P2P file transfer system
"""

from flask import Flask, request, jsonify, send_file, send_from_directory, session
from flask_cors import CORS
from flasgger import Swagger
import socket
import os
import json
import re
import secrets
import sqlite3
import threading
from functools import wraps
from pathlib import Path
from werkzeug.utils import secure_filename
from werkzeug.security import check_password_hash, generate_password_hash

app = Flask(__name__)
CORS(app)
app.secret_key = os.environ.get('FLASK_SECRET_KEY', secrets.token_hex(32))

app.config['SWAGGER'] = {
    'title': 'P2P File Transfer API',
    'uiversion': 3,
    'specs_route': '/apidocs/'
}
swagger = Swagger(app)

# ... [Keep your database initialization, configuration, and helpers exactly the same] ...


# Configuration
TCP_SERVER_HOST = 'localhost'
TCP_SERVER_PORT = 8080
UPLOAD_FOLDER = 'uploads'
WEB_FOLDER = 'web'
MAX_FILE_SIZE = 100 * 1024 * 1024  # 100 MB
AUTH_DB = os.environ.get('P2P_AUTH_DB', 'p2p_auth.db')
app.config['MAX_CONTENT_LENGTH'] = MAX_FILE_SIZE


def initialize_auth_db():
    """Create the local authentication database when the web server starts."""
    with sqlite3.connect(AUTH_DB) as database:
        database.execute('''
            CREATE TABLE IF NOT EXISTS users (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                username TEXT NOT NULL UNIQUE,
                password_hash TEXT NOT NULL,
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            )
        ''')


initialize_auth_db()

# Ensure upload directory exists
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

# File registry (peer_id -> [files])
# peer_id -> authenticated user_id
peer_connections = {}
file_registry = {}


def require_authentication(function):
    """Require a signed web session for protected API routes."""
    @wraps(function)
    def wrapped(*args, **kwargs):
        if 'user_id' not in session:
            return jsonify({'status': 'error', 'message': 'Not authenticated'}), 401
        return function(*args, **kwargs)

    return wrapped


def safe_peer_path(peer_id, filename=None):
    """Return a path confined to the configured upload directory."""
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', peer_id):
        raise ValueError('Invalid peer ID')

    upload_root = os.path.realpath(UPLOAD_FOLDER)
    peer_root = os.path.realpath(os.path.join(upload_root, peer_id))
    if not (peer_root == upload_root or peer_root.startswith(upload_root + os.sep)):
        raise ValueError('Invalid peer path')

    if filename is None:
        return peer_root

    candidate = os.path.realpath(os.path.join(peer_root, filename))
    if candidate != peer_root and not candidate.startswith(peer_root + os.sep):
        raise ValueError('Invalid file path')
    return candidate


class TCPClient:
    """Simple TCP client to communicate with the P2P server"""
    
    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.connections = {}
        self.lock = threading.Lock()

    def _receive_response(self, connection):
        """Read one newline-delimited response from the TCP server."""
        response = bytearray()
        while len(response) <= 65536:
            chunk = connection.recv(4096)
            if not chunk:
                break
            response.extend(chunk)
            if b'\n' in chunk:
                line, _, _ = response.partition(b'\n')
                return line.decode('utf-8').strip()

        raise ConnectionError('Invalid or oversized TCP response')

    def connect_peer(self, peer_id):
        """Open and register a persistent TCP connection for a peer."""
        with self.lock:
            if peer_id in self.connections:
                return 'OK Connected'

            connection = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            connection.settimeout(10)
            try:
                connection.connect((self.host, self.port))
                connection.sendall(f'CONNECT {peer_id}\n'.encode())
                response = self._receive_response(connection)
                if response.startswith('OK'):
                    self.connections[peer_id] = connection
                    return response
            except Exception as error:
                response = f'ERROR {error}'

            connection.close()
            return response

    def disconnect_peer(self, peer_id):
        """Close a peer's persistent TCP session."""
        with self.lock:
            connection = self.connections.pop(peer_id, None)
            if connection is None:
                return 'OK Disconnected'

            try:
                connection.sendall(b'DISCONNECT\n')
                response = self._receive_response(connection)
            except Exception as error:
                response = f'ERROR {error}'
            finally:
                connection.close()

            return response
    
    def send_command(self, command, peer_id=None):
        """Send a command through a registered persistent peer session."""
        with self.lock:
            connection = self.connections.get(peer_id) if peer_id else None
            if connection is None and peer_id is None and self.connections:
                connection = next(iter(self.connections.values()))
            if connection is None:
                return 'ERROR Not connected'

            try:
                connection.sendall((command + '\n').encode())
                return self._receive_response(connection)
            except Exception as error:
                if peer_id:
                    self.connections.pop(peer_id, None)
                return f'ERROR {error}'


tcp_client = TCPClient(TCP_SERVER_HOST, TCP_SERVER_PORT)


@app.route('/api/register', methods=['POST'])
def register():
    """Register a new user account.
    ---
    tags:
      - Auth
    parameters:
      - in: body
        name: body
        schema:
          type: object
          properties:
            username:
              type: string
            password:
              type: string
    responses:
      201:
        description: Account created
      400:
        description: Invalid username or password
      409:
        description: Username already taken
    """
    data = request.get_json(silent=True) or {}
    username = str(data.get('username', '')).strip().lower()
    password = data.get('password', '')

    if not re.fullmatch(r'[a-z0-9_-]{3,64}', username):
        return jsonify({'status': 'error', 'message': 'Invalid username'}), 400
    if not isinstance(password, str) or len(password) < 8:
        return jsonify({'status': 'error', 'message': 'Password must be at least 8 characters'}), 400

    try:
        with sqlite3.connect(AUTH_DB) as database:
            database.execute(
                'INSERT INTO users (username, password_hash) VALUES (?, ?)',
                (username, generate_password_hash(password))
            )
    except sqlite3.IntegrityError:
        return jsonify({'status': 'error', 'message': 'Registration failed'}), 409

    return jsonify({'status': 'success', 'username': username}), 201


@app.route('/api/login', methods=['POST'])
def login():
    """Log in and start a session.
    ---
    tags:
      - Auth
    parameters:
      - in: body
        name: body
        schema:
          type: object
          properties:
            username:
              type: string
            password:
              type: string
    responses:
      200:
        description: Login successful
      401:
        description: Invalid credentials
    """
    data = request.get_json(silent=True) or {}
    username = str(data.get('username', '')).strip().lower()
    password = data.get('password', '')

    with sqlite3.connect(AUTH_DB) as database:
        user = database.execute(
            'SELECT id, username, password_hash FROM users WHERE username = ?',
            (username,)
        ).fetchone()

    if user is None or not isinstance(password, str) or not check_password_hash(user[2], password):
        return jsonify({'status': 'error', 'message': 'Invalid username or password'}), 401

    session.clear()
    session['user_id'] = user[0]
    session['username'] = user[1]
    return jsonify({'status': 'success', 'username': user[1]})


@app.route('/api/logout', methods=['POST'])
def logout():
    """Log out and end the current session.
    ---
    tags:
      - Auth
    responses:
      200:
        description: Logged out
    """
    session.clear()
    return jsonify({'status': 'success'})


@app.route('/api/me', methods=['GET'])
def current_session():
    """Return the authenticated user, if any."""
    if 'user_id' not in session:
        return jsonify({'status': 'error', 'message': 'Not authenticated'}), 401
    return jsonify({
        'status': 'success',
        'user_id': session['user_id'],
        'username': session['username']
    })


@app.route('/')
def index():
    """Serve the main web interface"""
    return send_from_directory(WEB_FOLDER, 'index.html')


@app.route('/<path:path>')
def serve_static(path):
    """Serve static files (CSS, JS)"""
    return send_from_directory(WEB_FOLDER, path)


@app.route('/api/connect', methods=['POST'])
@require_authentication
def connect():
    """Connect a peer to the TCP server.
    ---
    tags:
      - Peer
    parameters:
      - in: body
        name: body
        schema:
          type: object
          properties:
            peer_id:
              type: string
    responses:
      200:
        description: Connected
      409:
        description: Peer ID already in use
    """
    data = request.get_json(silent=True) or {}
    peer_id = str(data.get('peer_id', '')).strip()
    
    if not peer_id:
        return jsonify({'status': 'error', 'message': 'Peer ID required'})
    
    existing_owner = peer_connections.get(peer_id)
    if existing_owner is not None and existing_owner != session['user_id']:
        return jsonify({'status': 'error', 'message': 'Peer ID is already in use'}), 409

    response = tcp_client.connect_peer(peer_id)
    
    if response.startswith('OK'):
        peer_connections[peer_id] = session['user_id']
        if peer_id not in file_registry:
            file_registry[peer_id] = []
        
        return jsonify({
            'status': 'success',
            'message': 'Connected successfully',
            'peer_id': peer_id
        })
    else:
        return jsonify({
            'status': 'error',
            'message': response
        })


@app.route('/api/disconnect', methods=['POST'])
@require_authentication
def disconnect():
    """Disconnect a peer from the TCP server.
    ---
    tags:
      - Peer
    parameters:
      - in: body
        name: body
        schema:
          type: object
          properties:
            peer_id:
              type: string
    responses:
      200:
        description: Disconnected
      404:
        description: Peer session not found
    """
    data = request.get_json(silent=True) or {}
    peer_id = str(data.get('peer_id', '')).strip()

    if peer_connections.get(peer_id) != session['user_id']:
        return jsonify({'status': 'error', 'message': 'Peer session not found'}), 404
    
    if peer_id in peer_connections:
        del peer_connections[peer_id]
    
    tcp_client.disconnect_peer(peer_id)
    
    return jsonify({'status': 'success'})


@app.route('/api/upload', methods=['POST'])
@require_authentication
def upload_file():
    """Upload a file for a connected peer.
    ---
    tags:
      - Files
    consumes:
      - multipart/form-data
    parameters:
      - in: formData
        name: file
        type: file
        required: true
      - in: formData
        name: peer_id
        type: string
        required: true
    responses:
      200:
        description: File uploaded
      403:
        description: Peer session not found
      413:
        description: File too large
    """
    if 'file' not in request.files:
        return jsonify({'status': 'error', 'message': 'No file provided'})
    
    file = request.files['file']
    peer_id = request.form.get('peer_id')
    
    if not peer_id:
        return jsonify({'status': 'error', 'message': 'Peer ID required'})

    if peer_connections.get(peer_id) != session['user_id']:
        return jsonify({'status': 'error', 'message': 'Peer session not found'}), 403
    
    if file.filename == '':
        return jsonify({'status': 'error', 'message': 'No file selected'})
    
    try:
        # Secure the filename
        filename = secure_filename(file.filename)
        if not filename or not re.fullmatch(r'[A-Za-z0-9_.-]{1,255}', filename):
            return jsonify({'status': 'error', 'message': 'Invalid filename'}), 400

        if request.content_length and request.content_length > MAX_FILE_SIZE:
            return jsonify({'status': 'error', 'message': 'File is too large'}), 413
        
        peer_dir = safe_peer_path(peer_id)
        os.makedirs(peer_dir, exist_ok=True)
        
        # Save file
        filepath = safe_peer_path(peer_id, filename)
        file.save(filepath)
        
        # Get file size
        filesize = os.path.getsize(filepath)
        if filesize > MAX_FILE_SIZE:
            os.remove(filepath)
            return jsonify({'status': 'error', 'message': 'File is too large'}), 413
        
        # Register with TCP server
        response = tcp_client.send_command(f"UPLOAD {filename} {filesize}", peer_id)
        if not response.startswith('OK'):
            os.remove(filepath)
            return jsonify({
                'status': 'error',
                'message': f'Backend registration failed: {response}'
            }), 502
        
        # Update local registry
        if peer_id not in file_registry:
            file_registry[peer_id] = []
        
        file_registry[peer_id].append({
            'filename': filename,
            'size': filesize,
            'path': filepath,
            'user_id': session['user_id'],
            'visibility': 'public'
        })
        
        return jsonify({
            'status': 'success',
            'message': 'File uploaded successfully',
            'filename': filename,
            'size': filesize
        })
    
    except Exception as e:
        return jsonify({
            'status': 'error',
            'message': f'Upload failed: {str(e)}'
        })


@app.route('/api/list', methods=['GET'])
@require_authentication
def list_files():
    """List all files visible to the current user.
    ---
    tags:
      - Files
    responses:
      200:
        description: List of files
    """
    files = []
    
    for peer_id, peer_files in file_registry.items():
        for file_info in peer_files:
            if (file_info['user_id'] != session['user_id'] and
                    file_info.get('visibility', 'public') != 'public'):
                continue
            files.append({
                'filename': file_info['filename'],
                'size': file_info['size'],
                'owner': peer_id,
                'visibility': file_info.get('visibility', 'public')
            })
    
    return jsonify({
        'status': 'success',
        'files': files
    })


@app.route('/api/download', methods=['GET'])
@require_authentication
def download_file():
    """Download an owned file.
    ---
    tags:
      - Files
    parameters:
      - in: query
        name: filename
        type: string
        required: true
      - in: query
        name: owner
        type: string
        required: true
    responses:
      200:
        description: File contents
      404:
        description: File not found
    """
    filename = request.args.get('filename')
    owner = request.args.get('owner')
    
    if not filename or not owner:
        return jsonify({'status': 'error', 'message': 'Filename and owner required'}), 400
    
    # Find the file
    if owner in file_registry:
        for file_info in file_registry[owner]:
            if (file_info['filename'] == filename and
                    file_info['user_id'] == session['user_id']):
                filepath = file_info['path']
                if os.path.realpath(filepath) == safe_peer_path(owner, filename) and os.path.exists(filepath):
                    return send_file(filepath, as_attachment=True, download_name=filename)
    
    return jsonify({'status': 'error', 'message': 'File not found'}), 404


@app.route('/api/file/visibility', methods=['POST'])
@require_authentication
def update_file_visibility():
    """Make an owned file public or private."""
    data = request.get_json(silent=True) or {}
    peer_id = str(data.get('peer_id', '')).strip()
    filename = str(data.get('filename', '')).strip()
    visibility = data.get('visibility')

    if peer_connections.get(peer_id) != session['user_id']:
        return jsonify({'status': 'error', 'message': 'Peer session not found'}), 403
    if visibility not in ('public', 'private'):
        return jsonify({'status': 'error', 'message': 'Invalid visibility'}), 400

    file_info = next(
        (item for item in file_registry.get(peer_id, [])
         if item['filename'] == filename and item['user_id'] == session['user_id']),
        None
    )
    if file_info is None:
        return jsonify({'status': 'error', 'message': 'File not found'}), 404

    command = 'PUBLIC' if visibility == 'public' else 'PRIVATE'
    response = tcp_client.send_command(f'{command} {filename}', peer_id)
    if not response.startswith('OK'):
        return jsonify({'status': 'error', 'message': response}), 502

    file_info['visibility'] = visibility
    return jsonify({'status': 'success', 'filename': filename, 'visibility': visibility})


@app.route('/api/file/delete', methods=['POST'])
@require_authentication
def delete_file():
    """Delete an owned file from the peer registry and local storage."""
    data = request.get_json(silent=True) or {}
    peer_id = str(data.get('peer_id', '')).strip()
    filename = str(data.get('filename', '')).strip()

    if peer_connections.get(peer_id) != session['user_id']:
        return jsonify({'status': 'error', 'message': 'Peer session not found'}), 403

    files = file_registry.get(peer_id, [])
    file_info = next(
        (item for item in files
         if item['filename'] == filename and item['user_id'] == session['user_id']),
        None
    )
    if file_info is None:
        return jsonify({'status': 'error', 'message': 'File not found'}), 404

    response = tcp_client.send_command(f'DELETE {filename}', peer_id)
    if not response.startswith('OK'):
        return jsonify({'status': 'error', 'message': response}), 502

    try:
        os.remove(file_info['path'])
    except FileNotFoundError:
        pass
    files.remove(file_info)
    return jsonify({'status': 'success', 'filename': filename})


@app.route('/api/status', methods=['GET'])
def status():
    """Get server and session status.
    ---
    tags:
      - Status
    responses:
      200:
        description: Status summary
    """
    user_id = session.get('user_id')
    total_files = sum(
        sum(file_info['user_id'] == user_id for file_info in files)
        for files in file_registry.values()
    ) if user_id is not None else 0
    total_peers = sum(
        owner == user_id for owner in peer_connections.values()
    ) if user_id is not None else 0
    
    return jsonify({
        'status': 'success',
        'total_files': total_files,
        'total_peers': total_peers,
        'connected_peers': total_peers
    })


if __name__ == '__main__':
    web_host = os.environ.get('P2P_WEB_HOST', '0.0.0.0')
    web_port = int(os.environ.get('P2P_WEB_PORT', 5000))

    print("=" * 50)
    print("P2P File Transfer Web Server")
    print("=" * 50)
    print(f"Web Interface: http://localhost:{web_port}")
    print(f"TCP Server: {TCP_SERVER_HOST}:{TCP_SERVER_PORT}")
    print(f"Upload Folder: {os.path.abspath(UPLOAD_FOLDER)}")
    print("=" * 50)
    print("\nMake sure the TCP server is running on port 8080!")
    print("Starting web server...\n")

    # Debug mode enables the Werkzeug debugger (arbitrary code execution) - never enable in production.
    web_debug = os.environ.get('P2P_WEB_DEBUG', 'false').lower() == 'true'
    app.run(host=web_host, port=web_port, debug=web_debug)
