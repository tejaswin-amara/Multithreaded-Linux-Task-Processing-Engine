# Incident Response Runbook

## 1. High Queue Saturation & Thread Exhaustion
**Symptoms**: Dashboard shows queue at or near capacity consistently. HTTP requests taking long or timing out. `avg_latency_ms` increasing.
**Triage**:
- Check system CPU load (`top` or `htop`). If CPU is low, tasks are likely I/O bound.
- Check current worker count via the dashboard or `GET /api/stats`.
- Dynamically increase worker pool size via CLI: `workers <new_size>`.
- If CPU is saturated, consider scaling horizontally or investigating task efficiency.

## 2. Socket Descriptor Depletion (`EMFILE`)
**Symptoms**: HTTP requests fail immediately with connection errors. Engine logs show "Too many open files".
**Triage**:
- Verify current limits for the `taskengine` user: `su - taskengine -s /bin/bash -c "ulimit -n"`.
- Ensure `LimitNOFILE=65536` is correctly applied in the `systemd` service file.
- Restart the service: `sudo systemctl restart task_engine.service`.

## 3. Deadlock Detection & Tracing
**Symptoms**: Engine stops processing tasks. Dashboard stops updating or becomes unresponsive. CPU usage drops to 0.
**Triage**:
- Attach `gdb` to the running process: `sudo gdb -p $(pidof task_engine)`.
- Extract thread information:
  ```gdb
  (gdb) info threads
  (gdb) thread apply all bt
  ```
- Look for multiple threads waiting on `pthread_mutex_lock` or `pthread_cond_wait`.
- Review the backtraces to identify the locking order or condition causing the deadlock.
- After tracing, restart the service to restore functionality.
