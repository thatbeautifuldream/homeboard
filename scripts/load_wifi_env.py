Import("env")

import os
from pathlib import Path


required_variables = ("WIFI_SSID", "WIFI_PASSWORD", "API_TOKEN")


def load_dotenv(path):
    values = {}
    if not path.is_file():
        return values

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        name, value = line.split("=", 1)
        name = name.strip()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "'\"":
            value = value[1:-1]
        values[name] = value
    return values


project_dir = Path(env.subst("$PROJECT_DIR"))
dotenv_values = load_dotenv(project_dir / ".env")
wifi_values = {
    name: os.environ.get(name) or dotenv_values.get(name)
    for name in required_variables
}
missing_variables = [name for name, value in wifi_values.items() if not value]

if missing_variables:
    missing = ", ".join(missing_variables)
    raise RuntimeError(
        f"Missing required environment variable(s): {missing}. "
        "Set them in the environment or in the Git-ignored project .env file."
    )

env.Append(
    CPPDEFINES=[
        ("WIFI_SSID", env.StringifyMacro(wifi_values["WIFI_SSID"])),
        ("WIFI_PASSWORD", env.StringifyMacro(wifi_values["WIFI_PASSWORD"])),
        ("API_TOKEN", env.StringifyMacro(wifi_values["API_TOKEN"])),
    ]
)
