#!/usr/bin/env python3
"""XT-SECSGEM-1 (FR-TOOL-2): interoperability with an independent SECS/GEM host.

Runs the machine (`equipment_cli serve`) and connects to it as a host using the
open-source `secsgem` Python library, which shares no code with this project.
It performs S1F13, S1F3 and S2F41 and receives S6F11 and S5F1, the sequence in
PRD 10.2.

    pip install secsgem==0.3.0
    python3 scripts/interop_secsgem.py --cli build/src/app_cli/equipment_cli

Or against a machine that is already running:  --port 5000

Exit codes: 0 every check passed, 1 a check failed, 3 blocked (secsgem could
not be imported). A blocked run says so; it is never reported as a pass.

One thing is set up on the library's side, not the machine's: secsgem's host
looks up every incoming report id in `report_subscriptions`, expecting the host
to have defined the reports with S2F33/S2F35 (FR-GEM-8, dynamic reports, which
this machine does not implement). The machine sends fixed reports 3001-3003
(PRD 8.6.5), so their layouts are declared to the library up front.
"""

import argparse
import logging
import os
import re
import signal
import subprocess
import sys
import tempfile
import threading
import time

try:
    import secsgem.common
    import secsgem.gem
    import secsgem.hsms
except ImportError as exc:  # pragma: no cover - reported, not hidden
    print(f"BLOCKED: cannot import secsgem ({exc}). Install it with: pip install secsgem==0.3.0")
    sys.exit(3)

# Report layouts, PRD 8.6.5 (field names, in order).
REPORTS = {
    3001: ["WAFER_ID", "SLOT", "STRESS_MPA", "STRESS_UNC_MPA", "CURVATURE", "FIT_RMS_UM", "OUT_OF_SPEC"],
    3002: ["CONTROL_STATE", "PROCESS_STATE"],
    3003: ["WAFER_ID", "SLOT", "NUM_LINES"],
}


class Host(secsgem.gem.GemHostHandler):
    def __init__(self, settings):
        super().__init__(settings)
        self.MDLN = "interop-host"
        self.SOFTREV = "1.0"
        self.report_subscriptions = {rptid: list(fields) for rptid, fields in REPORTS.items()}


class Checks:
    def __init__(self):
        self.failures = 0
        self.total = 0

    def check(self, name, ok, detail=""):
        self.total += 1
        if ok:
            print(f"  ok    {name}")
        else:
            self.failures += 1
            print(f"  FAIL  {name}  {detail}")
        return ok


def start_machine(cli, config, out_dir):
    cmd = [cli, "serve", "--port", "0", "--control", "remote", "--rtf", "0", "--out", out_dir]
    if config:
        cmd += ["--config", config]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    deadline = time.time() + 30
    while time.time() < deadline:
        line = proc.stdout.readline()
        if not line:
            break
        match = re.search(r"listening on [\d.]+:(\d+)", line)
        if match:
            return proc, int(match.group(1))
    proc.kill()
    raise RuntimeError("the machine did not report a listening port")


def value(item):
    return item.get() if hasattr(item, "get") else item


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cli", help="path to equipment_cli; the script starts and stops the machine")
    ap.add_argument("--config", default=None, help="machine config (for --cli); needs a fault on wafer W002 for the alarm check")
    ap.add_argument("--port", type=int, help="port of a machine that is already running")
    ap.add_argument("--timeout", type=float, default=90.0, help="seconds to wait for each event")
    ap.add_argument("-v", "--verbose", action="store_true", help="show secsgem's own log")
    args = ap.parse_args()
    if not args.cli and not args.port:
        ap.error("give --cli PATH (start a machine) or --port N (use a running one)")

    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.CRITICAL)

    checks = Checks()
    machine = None
    host = None
    out_dir = tempfile.mkdtemp(prefix="ssim_interop_")
    try:
        port = args.port
        if args.cli:
            machine, port = start_machine(args.cli, args.config, out_dir)
        print(f"secsgem host -> machine on 127.0.0.1:{port}")

        settings = secsgem.hsms.HsmsSettings(
            address="127.0.0.1",
            port=port,
            connect_mode=secsgem.hsms.HsmsConnectMode.ACTIVE,
            device_type=secsgem.common.DeviceType.HOST,
        )
        host = Host(settings)

        events, alarms = [], []
        got_event, got_alarm = threading.Condition(), threading.Condition()

        def on_event(data):
            with got_event:
                events.append({"ceid": value(data["ceid"]), "rptid": value(data["rptid"]),
                               "values": {v["dvid"]: value(v["value"]) for v in data["values"]}})
                got_event.notify_all()

        def on_alarm(data):
            with got_alarm:
                alarms.append({"alid": value(data["alid"]), "code": value(data["code"]), "text": value(data["text"])})
                got_alarm.notify_all()

        host.events.collection_event_received += on_event
        host.events.alarm_received += on_alarm

        def wait_for(cond, items, pred, what):
            deadline = time.time() + args.timeout
            with cond:
                while True:
                    found = [x for x in items if pred(x)]
                    if found:
                        checks.check(what, True)
                        return found[0]
                    left = deadline - time.time()
                    if left <= 0:
                        checks.check(what, False, "(timed out)")
                        return None
                    cond.wait(left)

        host.enable()

        # S1F13 / S1F14: the library sends S1F13 itself once connected and selected.
        checks.check("S1F13/S1F14: communication established",
                     host.waitfor_communicating(timeout=15))

        # S1F3 / S1F4: status variables 4001 ControlState, 4002 ProcessState,
        # 4004 CurrentSlot, 4010 SoftwareRevision, 4012 WafersProcessed.
        reply = host.settings.streams_functions.decode(
            host.send_and_waitfor_response(host.stream_function(1, 3)([4001, 4002, 4004, 4010, 4012, 9999])))
        sv = [value(x) for x in reply.get()]
        checks.check("S1F3/S1F4: control=2 (Remote), process=0 (Idle), slot=1, revision, 0 wafers",
                     sv[:5] == [2, 0, 1, sv[3], 0] and isinstance(sv[3], str) and sv[3] != "",
                     f"got {sv}")
        checks.check("S1F3/S1F4: an unknown SVID gives an empty item", sv[5] in ([], None, ""), f"got {sv[5]!r}")

        # S2F41 / S2F42: START a wafer. HCACK 4 = accepted, completion by event.
        ack = host.send_remote_command("START", [("WAFER_ID", "W001")])
        checks.check("S2F41/S2F42: START accepted with HCACK 4", value(ack.HCACK) == 4, f"HCACK={value(ack.HCACK)}")

        # S6F11: WaferScanStarted (2004) and WaferScanComplete (2005) with the result.
        started = wait_for(got_event, events, lambda e: e["ceid"] == 2004, "S6F11: event 2004 received")
        if started:
            checks.check("S6F11: event 2004 carries WAFER_ID W001 and 6 lines",
                         started["values"].get("WAFER_ID") == "W001" and started["values"].get("NUM_LINES") == 6,
                         f"got {started['values']}")
        done = wait_for(got_event, events, lambda e: e["ceid"] == 2005, "S6F11: event 2005 received")
        if done:
            stress = done["values"].get("STRESS_MPA")
            checks.check("S6F11: event 2005 stress within 2 % of the hidden truth (-180 MPa)",
                         stress is not None and abs(stress - (-180.0)) <= 3.6, f"STRESS_MPA={stress}")
            checks.check("S6F11: event 2005 is in spec", done["values"].get("OUT_OF_SPEC") is False,
                         f"got {done['values'].get('OUT_OF_SPEC')}")

        # S5F1: wafer W002 has an injected sensor fault, which raises alarm 1001.
        ack = host.send_remote_command("START", [("WAFER_ID", "W002")])
        checks.check("S2F41/S2F42: second START accepted", value(ack.HCACK) == 4, f"HCACK={value(ack.HCACK)}")
        alarm = wait_for(got_alarm, alarms, lambda a: a["alid"] == 1001 and (a["code"] & 0x80),
                         "S5F1: alarm 1001 (set) received")
        if alarm:
            checks.check("S5F1: alarm text is present", bool(alarm["text"]), f"got {alarm}")

        # The host acknowledged every S6F11 and S5F1 automatically; a clean
        # session also means the machine saw those acknowledgements.
        ack = host.send_remote_command("CLEAR_ALARM", [])
        checks.check("S2F41/S2F42: CLEAR_ALARM done (HCACK 0)", value(ack.HCACK) == 0, f"HCACK={value(ack.HCACK)}")
        cleared = wait_for(got_alarm, alarms, lambda a: a["alid"] == 1001 and not (a["code"] & 0x80),
                           "S5F1: alarm 1001 (cleared) received")
        if cleared:
            checks.check("S5F1: alarm-cleared report follows", True)
    except Exception as exc:  # a crash is a failed run, with the reason shown
        checks.check("interop run completed without an exception", False, repr(exc))
    finally:
        if host is not None:
            try:
                host.disable()
            except Exception:
                pass
        if machine is not None:
            machine.send_signal(signal.SIGINT)
            try:
                machine.wait(timeout=10)
            except subprocess.TimeoutExpired:
                machine.kill()

    verdict = "PASS" if checks.failures == 0 else "FAIL"
    print(f"XT-SECSGEM-1: {verdict} ({checks.total - checks.failures}/{checks.total} checks)")
    return 0 if checks.failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
