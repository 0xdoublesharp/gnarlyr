#!/usr/bin/env python3
"""MCP server for the Doublesharp Echo debug loop.

JSON-RPC 2.0 over stdio, one message per line, standard library only. It offers the
operations of echo_debug.py as tools and returns the same JSON objects, as text and as
structured content; a failed operation is a tool result with isError set and the same
{"ok": false, "error": {...}} object. The safety rules are echo_debug's own.
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import echo_debug

PROTOCOL_VERSION = "2024-11-05"
SERVER = {"name": "echo-debug", "version": echo_debug.IDENTITY["version"]}
PARSE_ERROR = -32700
INVALID_REQUEST = -32600
METHOD_NOT_FOUND = -32601
INVALID_PARAMS = -32602
TIMEOUT = {"type": "number", "description": "seconds to wait for the player to come back"}


def schema(properties=None, required=()):
    return {"type": "object", "properties": properties or {}, "required": list(required),
            "additionalProperties": False}


TOOLS = {
    "echo_status": {
        "description": "Is the player on USB as a storage device, and which volume is its internal drive.",
        "inputSchema": schema(),
        "call": lambda tool, arguments: tool.status(),
    },
    "echo_report": {
        "description": "Read and parse the debug report the player wrote when this USB session started.",
        "inputSchema": schema(),
        "call": lambda tool, arguments: tool.report(),
    },
    "echo_profile": {
        "description": "Time per function and per source file from the report's profile histogram.",
        "inputSchema": schema({"elf": {"type": "string", "description": "echo_device.elf of the build that ran"},
                               "nm": {"type": "string"}, "top": {"type": "integer", "minimum": 1}},
                              ["elf"]),
        "call": lambda tool, arguments: tool.profile(arguments["elf"], arguments.get("nm"),
                                                     arguments.get("top", echo_debug.DEFAULT_TOP)),
    },
    "echo_install": {
        "description": "Copy a firmware image as the update file, verify the copy and eject. With restart, "
                       "ask a developer build to restart by itself after the install and wait for it.",
        "inputSchema": schema({"image": {"type": "string"}, "restart": {"type": "boolean"},
                               "timeout": TIMEOUT}, ["image"]),
        "call": lambda tool, arguments: tool.install(arguments["image"], arguments.get("restart", False),
                                                     arguments.get("timeout", echo_debug.DEFAULT_TIMEOUT)),
    },
    "echo_run": {
        "description": "Send a debug script (a file, or its text), eject, wait for the player to come back "
                       "and return the script's results. Without a timeout the wait is the default plus "
                       "the minutes of the script's soak lines.",
        "inputSchema": schema({"script": {"type": "string", "description": "path of a script file"},
                               "text": {"type": "string", "description": "the script itself"},
                               "timeout": TIMEOUT}),
        "call": lambda tool, arguments: run(tool, arguments),
    },
    "echo_wait": {
        "description": "Wait until the player is on USB with its internal drive mounted, then return its status.",
        "inputSchema": schema({"timeout": TIMEOUT}),
        "call": lambda tool, arguments: tool.wait(arguments.get("timeout", echo_debug.DEFAULT_TIMEOUT)),
    },
    "echo_screenshots": {
        "description": "Convert the last script run's screenshots to PNG files in a folder off the player.",
        "inputSchema": schema({"out": {"type": "string"}}, ["out"]),
        "call": lambda tool, arguments: tool.screenshots(arguments["out"]),
    },
}


def run(tool, arguments):
    timeout = arguments.get("timeout")
    if ("script" in arguments) == ("text" in arguments):
        raise echo_debug.DebugError("usage", "give either script (a path) or text (the script itself)")
    if "text" in arguments:
        return tool.run_text(arguments["text"], timeout)
    return tool.run(arguments["script"], timeout)


def check_arguments(name, arguments):
    properties = TOOLS[name]["inputSchema"]
    unknown = sorted(set(arguments) - set(properties["properties"]))
    missing = sorted(set(properties["required"]) - set(arguments))
    if unknown or missing:
        raise echo_debug.DebugError("usage", f"{name}: unknown {unknown}, missing {missing}")


def call_tool(tool, name, arguments):
    try:
        check_arguments(name, arguments)
        result = TOOLS[name]["call"](tool, arguments)
    except echo_debug.DebugError as error:
        result = error.as_json()
    except OSError as error:
        result = echo_debug.DebugError("io_error", str(error)).as_json()
    return {"content": [{"type": "text", "text": json.dumps(result)}], "structuredContent": result,
            "isError": not result["ok"]}


def tool_list():
    return [{"name": name, "description": entry["description"], "inputSchema": entry["inputSchema"]}
            for name, entry in TOOLS.items()]


class RpcError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code
        self.message = message


def dispatch(tool, method, params):
    if method == "initialize":
        return {"protocolVersion": PROTOCOL_VERSION, "capabilities": {"tools": {}}, "serverInfo": SERVER}
    if method == "ping":
        return {}
    if method == "tools/list":
        return {"tools": tool_list()}
    if method == "tools/call":
        name, arguments = params.get("name"), params.get("arguments") or {}
        if name not in TOOLS or not isinstance(arguments, dict):
            raise RpcError(INVALID_PARAMS, f"unknown tool or arguments: {name!r}")
        return call_tool(tool, name, arguments)
    raise RpcError(METHOD_NOT_FOUND, f"unknown method {method!r}")


def answer(tool, line):
    """The response to one request line, or None for a notification."""
    try:
        message = json.loads(line)
    except ValueError:
        return {"jsonrpc": "2.0", "id": None, "error": {"code": PARSE_ERROR, "message": "not JSON"}}
    if not isinstance(message, dict) or not isinstance(message.get("method"), str):
        return {"jsonrpc": "2.0", "id": None,
                "error": {"code": INVALID_REQUEST, "message": "not a request"}}
    if "id" not in message:
        return None
    try:
        result = dispatch(tool, message["method"], message.get("params") or {})
    except RpcError as error:
        return {"jsonrpc": "2.0", "id": message["id"],
                "error": {"code": error.code, "message": error.message}}
    return {"jsonrpc": "2.0", "id": message["id"], "result": result}


def serve(tool, lines, output):
    for line in lines:
        if not line.strip():
            continue
        response = answer(tool, line)
        if response is not None:
            output.write(json.dumps(response) + "\n")
            output.flush()


def main(argv=None, host=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--volume", type=Path,
                        help="the internal drive's folder, instead of finding the player on USB")
    parser.add_argument("--volume-name", default=echo_debug.DEFAULT_VOLUME_NAME)
    arguments = parser.parse_args(argv)
    serve(echo_debug.Tool(host, arguments.volume, arguments.volume_name), sys.stdin, sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
