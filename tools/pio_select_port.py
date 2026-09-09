# Runs inside PlatformIO's own Python (penv), so pyserial is available.
import json, os, re, subprocess, sys, time
from serial.tools import list_ports
from SCons.Script import COMMAND_LINE_TARGETS

Import("env")

ESPRESSIF_VID = 0x303A          # native USB Serial/JTAG on ESP32-C3
CACHE = os.path.join(env.subst("$PROJECT_DIR"), ".pio", "port_cache.json")
NEEDS_PORT = {"upload", "uploadfs", "monitor"}

def log(msg):
    print(f"[select_port] {msg}")

def fail(msg):
    sys.stderr.write(f"\n[select_port] ERROR: {msg}\n\n")
    env.Exit(1)

def norm(mac):
    return mac.strip().upper().replace("-", ":")

def candidate_ports():
    return sorted(
        p.device for p in list_ports.comports()
        if p.vid == ESPRESSIF_VID and p.device.startswith("/dev/cu.")
    )

def wait_for_port(port, timeout=5.0):
    # After esptool's hard_reset the C3 re-enumerates over USB; the
    # /dev/cu.* node disappears for ~1 s. Wait for it to come back.
    t0 = time.time()
    while time.time() - t0 < timeout:
        if os.path.exists(port):
            return True
        time.sleep(0.2)
    return False

def read_mac(port):
    esptool_dir = env.PioPlatform().get_package_dir("tool-esptoolpy")
    cmd = [env.subst("$PYTHONEXE"), os.path.join(esptool_dir, "esptool.py"),
           "--chip", "esp32c3", "--port", port, "--no-stub",
           "--before", "default_reset", "--after", "hard_reset", "read_mac"]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=20).stdout
    except subprocess.TimeoutExpired:
        return None
    m = re.search(r"^MAC:\s*([0-9A-Fa-f:]{17})", out, re.M)
    mac = norm(m.group(1)) if m else None
    wait_for_port(port)
    return mac

def load_cache():
    try:
        with open(CACHE) as f:
            return json.load(f)
    except Exception:
        return {}

def save_cache(c):
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    with open(CACHE, "w") as f:
        json.dump(c, f, indent=2)

def find_port(expected):
    ports = candidate_ports()
    if not ports:
        fail("No Espressif USB devices connected.")
    cache = load_cache()

    # 1. Try the last known port first (avoids rebooting the other board).
    cached = cache.get(expected)
    if cached in ports:
        log(f"checking cached port {cached} ...")
        if read_mac(cached) == expected:
            return cached

    # 2. Full scan, stop at first match.
    for port in ports:
        if port == cached:
            continue
        log(f"probing {port} ...")
        if read_mac(port) == expected:
            cache[expected] = port
            save_cache(cache)
            return port

    fail(f"No connected board has MAC {expected} "
         f"(env '{env['PIOENV']}'). Ports seen: {', '.join(ports)}")

# ---- main -------------------------------------------------------------
targets = set(COMMAND_LINE_TARGETS)
if targets & NEEDS_PORT:
    if env.GetProjectOption("upload_port", None) or env.GetProjectOption("monitor_port", None):
        fail("Remove upload_port/monitor_port from platformio.ini; ports are resolved by MAC.")
    expected = norm(env.GetProjectOption("custom_device_mac", ""))
    if not expected:
        fail(f"custom_device_mac missing for env '{env['PIOENV']}'")
    port = find_port(expected)
    log(f"env '{env['PIOENV']}' ({expected}) -> {port}")
    env.Replace(UPLOAD_PORT=port, MONITOR_PORT=port)

# Extra Project Task: "Monitor (auto port)" under each env.
def monitor_task(*_, **__):
    expected = norm(env.GetProjectOption("custom_device_mac", ""))
    port = find_port(expected)
    return env.Execute(
        f'"{env.subst("$PYTHONEXE")}" -m platformio device monitor '
        f'-p {port} -b {env.GetProjectOption("monitor_speed", "115200")}')

env.AddCustomTarget(
    "monitor_auto", None, monitor_task,
    title="Monitor (auto port)", description="Find the board by MAC, then open a monitor")