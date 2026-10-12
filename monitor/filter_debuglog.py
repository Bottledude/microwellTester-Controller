# Captures STM32 Serial Monitor RX/TX into the Cursor debug NDJSON log.
from platformio.public import DeviceMonitorFilterBase

import json
import os
import time

LOG_PATH = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    ".cursor",
    "debug-6e46c5.log",
)


class Debuglog(DeviceMonitorFilterBase):
    NAME = "debuglog"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._rx = ""
        self._tx = ""

    def _emit(self, hypothesis_id, location, message, data):
        rec = {
            "sessionId": "6e46c5",
            "runId": "run1",
            "hypothesisId": hypothesis_id,
            "location": location,
            "message": message,
            "data": data,
            "timestamp": int(time.time() * 1000),
        }
        try:
            os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
            with open(LOG_PATH, "a", encoding="utf-8") as f:
                f.write(json.dumps(rec) + "\n")
        except OSError:
            pass

    def rx(self, text):
        if not text:
            return text
        self._rx += text
        while "\n" in self._rx:
            line, self._rx = self._rx.split("\n", 1)
            line = line.strip("\r")
            if line.startswith("DBG "):
                self._emit("H1H2H3H4H5", "uart:DBG", "stm32 dbg", {"line": line})
            elif line.startswith("{") and "\"ok\"" in line:
                self._emit("H1H2H3", "uart:json", "json line", {"line": line[:900]})
            elif line.startswith("mode=") or line.startswith("M1 ") or line.startswith("M2 ") \
                    or line.startswith("M3 ") or line.startswith("M4 "):
                self._emit("H1H2", "uart:status", "status line", {"line": line})
            elif line.startswith("rotate ") or line.startswith("amp=") or line == "PONG":
                self._emit("H1H4", "uart:cli", "cli reply", {"line": line})
        return text

    def tx(self, text):
        if not text:
            return text
        self._tx += text
        if "\r" in text or "\n" in text:
            self._emit("H4H5", "uart:tx", "line sent", {"text": repr(self._tx)})
            self._tx = ""
        return text
