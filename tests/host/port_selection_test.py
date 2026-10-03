"""Exercise the real extra_script with no USB access or project-file writes."""
import os
import io
from contextlib import redirect_stderr
from pathlib import Path
import runpy
import sys
import tempfile
import types
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[2] / "tools" / "pio_select_port.py"


class BuildEnvironment:
    def __init__(self, project):
        self.project = str(project)
        self.targets = []

    def subst(self, expression):
        assert expression == "$PROJECT_DIR"
        return self.project

    def Exit(self, code):
        raise SystemExit(code)

    def __getitem__(self, name):
        assert name == "PIOENV"
        return "bubu"

    def AddCustomTarget(self, *args, **kwargs):
        self.targets.append(args[0])


def forbid_usb():
    raise AssertionError("build-only invocation attempted USB discovery")


with tempfile.TemporaryDirectory(prefix="bubududu-port-guard-") as directory:
    for targets in ([], ["buildprog"], ["upload"], ["uploadfs"], ["uploadfsota"], ["monitor_auto"], ["monitor"]):
        env = BuildEnvironment(directory)
        serial = types.ModuleType("serial")
        serial_tools = types.ModuleType("serial.tools")
        serial_tools.list_ports = types.SimpleNamespace(comports=forbid_usb)
        serial.tools = serial_tools
        scons = types.ModuleType("SCons")
        script = types.ModuleType("SCons.Script")
        script.COMMAND_LINE_TARGETS = targets
        scons.Script = script
        with patch.dict(os.environ, {"BUBUDUDU_BUILD_ONLY": "1"}), patch.dict(sys.modules, {
            "serial": serial, "serial.tools": serial_tools, "SCons": scons, "SCons.Script": script,
        }):
            blocked = bool(targets and targets[0] != "buildprog")
            errors = io.StringIO()
            try:
                with redirect_stderr(errors):
                    runpy.run_path(str(SCRIPT), init_globals={"env": env, "Import": lambda _: None})
            except SystemExit as error:
                assert blocked and error.code == 1
                assert "forbids upload and monitor targets" in errors.getvalue()
            else:
                assert not blocked and env.targets == ["monitor_auto"]
                assert not errors.getvalue()
        assert not list(Path(directory).iterdir()), "build-only invocation wrote project files"
print("PASS: real port-selection script builds without USB or ports.ini and rejects upload/monitor targets")
