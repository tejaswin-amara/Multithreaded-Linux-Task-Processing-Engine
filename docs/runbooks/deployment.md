# Deployment Runbook

## 1. Environment Requirements
- **OS**: Ubuntu 22.04 LTS or 24.04 LTS
- **Dependencies**: `libc6`, `libpthread`

## 2. systemd Service Configuration

A production `systemd` service template (`task_engine.service`) is provided for reliable execution. It includes security sandboxing and non-root execution.

```ini
[Unit]
Description=Multithreaded Linux Task Processing Engine
After=network.target

[Service]
Type=simple
User=taskengine
Group=taskengine
ExecStart=/usr/local/bin/task_engine --port 8080 --threads 4 --queue-size 1024
WorkingDirectory=/opt/task_engine
KillSignal=SIGTERM

# Sandboxing
ProtectSystem=strict
ProtectHome=yes
NoNewPrivileges=true

# Resource Limits
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
```

## 3. Deployment Steps
1. Create a `taskengine` user:
   `sudo useradd -r -s /bin/false taskengine`
2. Install the binary to `/usr/local/bin/task_engine`.
3. Copy static assets (e.g., `web/`) to `/opt/task_engine/`.
4. Install the service file:
   `sudo cp task_engine.service /etc/systemd/system/`
5. Reload `systemd` and start the service:
   `sudo systemctl daemon-reload`
   `sudo systemctl enable --now task_engine.service`
