import argparse
import subprocess
from datetime import datetime
from pathlib import Path
import CreateVersionH

VERSION_INFO_TXT = Path(__file__).parent.resolve() / "VersionInfo.txt"

def read_version_info():
    with open(VERSION_INFO_TXT, "r") as f:
        version = f.read().strip()

    version_parts = version.split('.')
    if len(version_parts) != 4:
        # If version format is incorrect, reset to default
        version_parts = ["0", "0", "0", "0"]

    # Update major version with year if needed
    current_year = str(datetime.now().year)

    if version_parts[0] != current_year:
        version_parts[0] = current_year
        version_parts[1] = "1"  # Reset minor version if major version changes
        version_parts[2] = "0"  # Reset patch version if major version changes
        version_parts[3] = "0"  # Reset build number if major version changes

    return version_parts


def get_current_version():
    version_parts = read_version_info()
    return f"{version_parts[0]}.{version_parts[1]}.{version_parts[2]}.{version_parts[3]}"


def update_version_info():
    version_parts = read_version_info()
    current_version = get_current_version()
    print(f"Current version: {current_version}")
    version_parts[3] = str(int(version_parts[3]) + 1)  # Increment build number
    new_version = f"{version_parts[0]}.{version_parts[1]}.{version_parts[2]}.{version_parts[3]}"
    print(f"New version: {new_version}")

    with open(VERSION_INFO_TXT, "w") as f:
        f.write(new_version)

    # Here we need to regenerate version.h file, just invoke CreateVersionH.py
    CreateVersionH.create_version_header()


if __name__ == "__main__":
    update_version_info()
