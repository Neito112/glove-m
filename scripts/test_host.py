"""Build and run the zclaw host tests on Windows.

The project normally uses scripts/test.sh, which needs a bash shell and a
native gcc. This script does the same with the clang that comes with the
zig toolchain, so the tests also run on a plain Windows machine.
"""

import os
import subprocess
import sys
import sysconfig

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST_DIR = os.path.join(PROJECT_DIR, "test", "host")
IDF_DIR = os.environ.get("IDF_PATH", r"D:\esp-device\esp-idf")
CJSON_DIR = os.path.join(IDF_DIR, "components", "json", "cJSON")
BUILD_DIR = os.path.join(HOST_DIR, "build")


def zig_cc():
    """The path of the clang that ships with the zig toolchain."""
    return [sys.executable, "-m", "ziglang", "cc"]


def compile_and_run():
    os.makedirs(BUILD_DIR, exist_ok=True)
    runner = os.path.join(BUILD_DIR, "test_runner.exe")

    sources = [
        "test_json.c",
        "test_tools_parse.c",
        "test_json_util_integration.c",
        "test_runtime_utils.c",
        "test_memory_keys.c",
        "test_telegram_update.c",
        "test_telegram_token.c",
        "test_telegram_chat_ids.c",
        "test_telegram_poll_policy.c",
        "test_telegram_http_diag.c",
        "test_agent.c",
        "test_tools_gpio_policy.c",
        "test_tools_i2c_policy.c",
        "test_tools_dht.c",
        "test_builtin_tools_registry.c",
        "test_tools_system_diag.c",
        "test_llm_auth.c",
        "test_wifi_credentials.c",
        "test_peripheral.c",
        "test_tools_peripheral.c",
        "test_runner.c",
        "mock_esp.c",
        "mock_memory.c",
        "mock_llm.c",
        "mock_user_tools.c",
        "mock_freertos.c",
        "mock_tools.c",
        "mock_system_diag_deps.c",
        "mock_ratelimit.c",
        "mock_i2c.c",
        "mock_espnow.c",
        "mock_peripheral.c",
        os.path.join("..", "..", "main", "json_util.c"),
        os.path.join("..", "..", "main", "json_builder.c"),
        os.path.join("..", "..", "main", "cron_utils.c"),
        os.path.join("..", "..", "main", "security.c"),
        os.path.join("..", "..", "main", "text_buffer.c"),
        os.path.join("..", "..", "main", "boot_guard.c"),
        os.path.join("..", "..", "main", "memory_keys.c"),
        os.path.join("..", "..", "main", "llm_auth.c"),
        os.path.join("..", "..", "main", "wifi_credentials.c"),
        os.path.join("..", "..", "main", "telegram_update.c"),
        os.path.join("..", "..", "main", "telegram_token.c"),
        os.path.join("..", "..", "main", "telegram_chat_ids.c"),
        os.path.join("..", "..", "main", "telegram_poll_policy.c"),
        os.path.join("..", "..", "main", "telegram_http_diag.c"),
        os.path.join("..", "..", "main", "agent_commands.c"),
        os.path.join("..", "..", "main", "agent_prompt.c"),
        os.path.join("..", "..", "main", "agent.c"),
        os.path.join("..", "..", "main", "local_admin.c"),
        os.path.join("..", "..", "main", "gpio_policy.c"),
        os.path.join("..", "..", "main", "tools_common.c"),
        os.path.join("..", "..", "main", "tools_gpio.c"),
        os.path.join("..", "..", "main", "tools_i2c.c"),
        os.path.join("..", "..", "main", "tools_dht.c"),
        os.path.join("..", "..", "main", "tools_system.c"),
        os.path.join("..", "..", "main", "tools_peripheral.c"),
        os.path.join("..", "..", "main", "peripheral_font.c"),
        os.path.join("..", "..", "main", "espnow.c"),
        os.path.join(CJSON_DIR, "cJSON.c"),
    ]

    command = zig_cc() + [
        "-o",
        runner,
        "-std=c99",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wshadow",
        "-Wformat=2",
        "-Wno-format-nonliteral",
        # The host tests include cJSON as <cjson/cJSON.h> while the directory
        # on disk is spelled cJSON. The path is fine on a case sensitive
        # file system, so the warning is not useful here.
        "-Wno-nonportable-include-path",
        "-I" + os.path.join("..", "..", "main"),
        "-I.",
        "-I" + CJSON_DIR,
        "-I" + os.path.join(IDF_DIR, "components", "json"),
        "-DTEST_BUILD",
    ] + sources

    print("Compiling the host tests...")
    result = subprocess.run(command, cwd=HOST_DIR)
    if result.returncode != 0:
        return result.returncode

    print("Running the host tests...")
    return subprocess.run([runner], cwd=HOST_DIR).returncode


if __name__ == "__main__":
    sys.exit(compile_and_run())
