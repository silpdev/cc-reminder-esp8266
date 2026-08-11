#!/usr/bin/env python3
"""
cc-reminder host script - tu tim thiet bi o bat ky mang nao.

Dung:
    cc_reminder_http.py IDLE|WORKING|INTERACT
    cc_reminder_http.py STATUS
    cc_reminder_http.py discover      # tim lai, in ra IP
    cc_reminder_http.py forget        # xoa cache

Chi dung thu vien chuan, khong can pip install gi.

CHUOI DO IP (dung o buoc nao thi lay luon, roi cache lai):
    1. Bien moi truong CC_REMINDER_HOST  - neu ban muon ep cung
    2. Cache tu lan truoc                - ~99% truong hop, ~30ms
    3. mDNS  <host>.local                - chay tot tren macOS/Linux
    4. UDP broadcast "CCR?" port 45678   - khi mDNS chet
    5. Quet subnet /24                   - khi ca broadcast cung khong qua duoc

Buoc 5 ton tai vi WSL2. Mang WSL2 la mang NAT rieng nen goi broadcast va
mDNS KHONG ra duoc LAN that, nhung ket noi unicast toi IP LAN thi duoc.
Script se lay subnet LAN tu ipconfig.exe roi quet.

  Muon bo han buoc 5: dung WSL tren Windows 11 va bat mirrored networking,
  them vao C:\\Users\\<ban>\\.wslconfig:
      [wsl2]
      networkingMode=mirrored
  Luc do broadcast va mDNS chay binh thuong.

LUON tra ve exit code 0 - den offline khong duoc lam Claude Code fail.
"""

from __future__ import annotations

import concurrent.futures
import ipaddress
import json
import os
import re
import socket
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

DISCO_PORT = 45678
DISCO_PROBE = b"CCR?"
DEF_HOST = "cc-reminder"
VALID = ("IDLE", "WORKING", "INTERACT")

# Timeout ngan - hook khong duoc treo
T_VERIFY = 0.6
T_MDNS = 1.0
T_UDP = 1.2
T_SCAN = 0.35

CACHE = Path(
    os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")
) / "cc-reminder" / "host"


def log(msg: str) -> None:
    if os.environ.get("CC_REMINDER_DEBUG"):
        print(f"[cc-reminder] {msg}", file=sys.stderr)


# ----------------------------------------------------------------- HTTP
def http_get(ep: str, path: str, timeout: float) -> str | None:
    """ep la "ip" hoac "ip:port"."""
    try:
        req = urllib.request.Request(f"http://{ep}{path}")
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.read().decode("utf-8", "replace").strip()
    except (urllib.error.URLError, OSError, ValueError):
        return None


def verify(ep: str) -> bool:
    """Dung thiet bi that chu khong phai may nao khac cung dia chi do."""
    body = http_get(ep, "/status", T_VERIFY)
    return body is not None and body.strip() in VALID


# ----------------------------------------------------------------- cache
def cache_read() -> str | None:
    try:
        return CACHE.read_text(encoding="utf-8").strip() or None
    except OSError:
        return None


def cache_write(ip: str) -> None:
    try:
        CACHE.parent.mkdir(parents=True, exist_ok=True)
        CACHE.write_text(ip, encoding="utf-8")
    except OSError:
        pass


def cache_clear() -> None:
    try:
        CACHE.unlink()
    except OSError:
        pass


# ----------------------------------------------------------------- mDNS
def try_mdns(name: str) -> str | None:
    fqdn = name if name.endswith(".local") else f"{name}.local"
    old = socket.getdefaulttimeout()
    socket.setdefaulttimeout(T_MDNS)
    try:
        infos = socket.getaddrinfo(fqdn, 80, socket.AF_INET, socket.SOCK_STREAM)
        for info in infos:
            ip = info[4][0]
            if verify(ip):
                return ip
    except (socket.gaierror, OSError):
        pass
    finally:
        socket.setdefaulttimeout(old)
    return None


# ----------------------------------------------------------------- UDP
def broadcast_addrs() -> list[str]:
    addrs = ["255.255.255.255"]
    for net in local_subnets():
        addrs.append(str(net.broadcast_address))
    # bo trung, giu thu tu
    return list(dict.fromkeys(addrs))


def try_udp() -> str | None:
    for dest in broadcast_addrs():
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            s.settimeout(T_UDP)
            s.sendto(DISCO_PROBE, (dest, DISCO_PORT))
            while True:
                data, addr = s.recvfrom(512)
                try:
                    obj = json.loads(data.decode("utf-8", "replace"))
                except ValueError:
                    continue
                if obj.get("cc-reminder"):
                    ip = obj.get("ip") or addr[0]
                    port = int(obj.get("port") or 80)
                    ep = ip if port == 80 else f"{ip}:{port}"
                    log(f"udp thay {ep} qua {dest}")
                    if verify(ep):
                        return ep
        except (socket.timeout, OSError):
            continue
        finally:
            s.close()
    return None


# ----------------------------------------------------------------- subnet
def is_wsl() -> bool:
    try:
        return "microsoft" in Path("/proc/version").read_text().lower()
    except OSError:
        return False


def windows_subnets() -> list[ipaddress.IPv4Network]:
    """Lay subnet LAN that tu ipconfig.exe (chay tu trong WSL duoc)."""
    out = ""
    for exe in ("/mnt/c/Windows/System32/ipconfig.exe", "ipconfig.exe"):
        try:
            out = subprocess.run(
                [exe], capture_output=True, text=True, timeout=6,
                errors="replace",
            ).stdout
            if out:
                break
        except (OSError, subprocess.SubprocessError):
            continue

    nets: list[ipaddress.IPv4Network] = []
    ip = None
    for line in out.splitlines():
        m = re.search(r"IPv4[^:]*:\s*([0-9.]+)", line)
        if m:
            ip = m.group(1)
            continue
        m = re.search(r"(?:Subnet Mask|Mask)[^:]*:\s*([0-9.]+)", line)
        if m and ip:
            try:
                nets.append(ipaddress.IPv4Network(f"{ip}/{m.group(1)}", strict=False))
            except ValueError:
                pass
            ip = None
    return nets


def local_subnets() -> list[ipaddress.IPv4Network]:
    nets: list[ipaddress.IPv4Network] = []
    if is_wsl():
        nets.extend(windows_subnets())

    # IP cua chinh may nay (khong thuc su gui goi nao)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 53))
        own = s.getsockname()[0]
        nets.append(ipaddress.IPv4Network(f"{own}/24", strict=False))
    except OSError:
        pass
    finally:
        s.close()

    out = []
    for n in nets:
        if n.is_loopback or n.num_addresses > 1024:
            continue          # bo /16 tro len, quet khong xong
        if n not in out:
            out.append(n)
    return out


def try_scan() -> str | None:
    for net in local_subnets():
        hosts = [str(h) for h in net.hosts()]
        log(f"quet {net} ({len(hosts)} dia chi)")
        ex = concurrent.futures.ThreadPoolExecutor(max_workers=64)
        try:
            futs = {ex.submit(scan_one, h): h for h in hosts}
            for fut in concurrent.futures.as_completed(futs):
                try:
                    hit = fut.result()
                except Exception:
                    continue
                if hit:
                    ip = futs[fut]
                    log(f"quet thay {ip}")
                    return ip
        finally:
            # cancel_futures: bo cac task chua chay, khong cho pool drain
            ex.shutdown(wait=False, cancel_futures=True)
    return None


def scan_one(ip: str) -> bool:
    body = http_get(ip, "/status", T_SCAN)
    return body is not None and body.strip() in VALID


# ----------------------------------------------------------------- resolve
def resolve(force: bool = False) -> str | None:
    name = os.environ.get("CC_REMINDER_NAME", DEF_HOST)

    forced = os.environ.get("CC_REMINDER_HOST")
    if forced:
        return forced          # ep cung thi khong verify, khong cache

    if not force:
        ip = cache_read()
        if ip and verify(ip):
            log(f"cache: {ip}")
            return ip

    for step, fn in (("mdns", lambda: try_mdns(name)),
                     ("udp", try_udp),
                     ("scan", try_scan)):
        ip = fn()
        if ip:
            log(f"{step}: {ip}")
            cache_write(ip)
            return ip
    return None


# ----------------------------------------------------------------- main
def main() -> int:
    arg = (sys.argv[1] if len(sys.argv) > 1 else "STATUS").upper()

    if arg == "FORGET":
        cache_clear()
        print("da xoa cache")
        return 0

    if arg == "DISCOVER":
        ip = resolve(force=True)
        if ip:
            print(f"{ip}  ({http_get(ip, '/status', T_VERIFY)})")
        else:
            print("khong tim thay thiet bi", file=sys.stderr)
        return 0

    if arg == "STATUS":
        path = "/status"
    elif arg in VALID:
        path = f"/state?s={arg}"
    else:
        print(f"trang thai khong hop le: {arg}", file=sys.stderr)
        return 0

    ip = resolve()
    if not ip:
        log("khong tim thay thiet bi")
        return 0

    body = http_get(ip, path, T_VERIFY)
    if body is None:
        # co the doi mang tu luc cache -> dò lại 1 lần
        ip = resolve(force=True)
        if ip:
            body = http_get(ip, path, T_VERIFY)
    if body is not None:
        print(body)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as e:              # khong bao gio lam hook fail
        log(f"loi: {e}")
        sys.exit(0)
