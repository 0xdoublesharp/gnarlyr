import io
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import echo_debug
import echo_debug_mcp
from test_echo_debug import FakeHost, usb_tree
from test_echo_debug_loop import IMAGE_BUILD, LoopCase, PlayerHost, image


class McpServer(LoopCase):
    def exchange(self, *messages, host=None):
        tool = self.tool(host or PlayerHost(self))
        output = io.StringIO()
        echo_debug_mcp.serve(tool, [message if isinstance(message, str) else json.dumps(message)
                                    for message in messages], output)
        return [json.loads(line) for line in output.getvalue().splitlines()]

    def call(self, name, arguments=None, host=None):
        request = {"jsonrpc": "2.0", "id": 7, "method": "tools/call",
                   "params": {"name": name, "arguments": arguments or {}}}
        return self.exchange(request, host=host)[0]["result"]

    def test_initialize_names_the_server_and_its_tool_capability(self):
        reply = self.exchange({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
                              {"jsonrpc": "2.0", "method": "notifications/initialized"},
                              {"jsonrpc": "2.0", "id": 2, "method": "ping"})
        self.assertEqual(len(reply), 2)
        self.assertEqual(reply[0]["id"], 1)
        self.assertEqual(reply[0]["result"]["serverInfo"]["name"], "echo-debug")
        self.assertIn("tools", reply[0]["result"]["capabilities"])
        self.assertEqual(reply[1], {"jsonrpc": "2.0", "id": 2, "result": {}})

    def test_the_tools_are_the_command_lines_operations(self):
        tools = self.exchange({"jsonrpc": "2.0", "id": 1, "method": "tools/list"})[0]["result"]["tools"]
        self.assertEqual([tool["name"] for tool in tools],
                         ["echo_status", "echo_report", "echo_profile", "echo_install", "echo_run",
                          "echo_wait", "echo_screenshots", "echo_language_inspect", "echo_language_list",
                          "echo_language_install", "echo_language_select", "echo_settings_list", "echo_settings_set",
                          "echo_set_time"])
        for tool in tools:
            self.assertEqual(tool["inputSchema"]["type"], "object")
            self.assertTrue(tool["description"])

    def test_set_time_sends_the_computer_time_and_ejects(self):
        host = PlayerHost(self)
        tool = echo_debug.Tool(host, sleep=lambda seconds: None, wall=lambda: (1794482460, 60))
        result = echo_debug_mcp.call_tool(tool, "echo_set_time", {})["structuredContent"]
        self.assertEqual((result["utc"], result["offset_minutes"]), (1794482460, 60))
        self.assertEqual(result["ejected"], host.ejected)
        self.assertTrue(host.ejected)

    def test_a_tool_returns_what_the_command_line_returns(self):
        result = self.call("echo_status")
        expected = self.tool(PlayerHost(self)).status()
        self.assertEqual(result["structuredContent"], expected)
        self.assertEqual(json.loads(result["content"][0]["text"]), expected)
        self.assertFalse(result["isError"])
        self.assertEqual(self.call("echo_report")["structuredContent"],
                         self.tool(PlayerHost(self)).report())

    def test_a_failed_operation_is_a_tool_error_with_the_same_json(self):
        result = self.call("echo_report", host=FakeHost(usb_tree()))
        self.assertTrue(result["isError"])
        self.assertEqual(result["structuredContent"]["error"]["code"], "player_not_connected")
        self.assertFalse(result["structuredContent"]["ok"])

    def test_install_and_run_work_through_the_server_with_the_same_safety(self):
        result = self.call("echo_install", {"image": str(self.image)})
        self.assertEqual(result["structuredContent"]["image_build"], IMAGE_BUILD)
        self.image.write_bytes(image(brand=b"Rockchip"))
        self.assertEqual(self.call("echo_install", {"image": str(self.image)})["structuredContent"]["error"]["code"],
                         "not_an_image")
        sent = self.call("echo_run", {"text": "build any\nplay\n"})
        self.assertEqual(sent["structuredContent"]["error"]["code"], "stale_report")
        self.assertEqual((self.debug / "run.txt").read_text(), "build any\nplay\n")

    def test_run_takes_a_file_or_its_text_but_not_both(self):
        for arguments in ({}, {"script": str(self.script), "text": "play"}):
            self.assertEqual(self.call("echo_run", arguments)["structuredContent"]["error"]["code"], "usage")
        self.assertEqual(self.call("echo_profile", {})["structuredContent"]["error"]["code"], "usage")
        self.assertEqual(self.call("echo_status", {"force": True})["structuredContent"]["error"]["code"], "usage")

    def test_protocol_mistakes_get_json_rpc_errors(self):
        reply = self.exchange("not json", {"jsonrpc": "2.0", "id": 3, "method": "resources/list"},
                              {"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "format"}},
                              [1, 2], "")
        self.assertEqual([item["error"]["code"] for item in reply], [-32700, -32601, -32602, -32600])
        self.assertEqual([item["id"] for item in reply], [None, 3, 4, None])



if __name__ == "__main__":
    unittest.main()
