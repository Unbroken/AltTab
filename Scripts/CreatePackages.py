import os
import subprocess
import sys
import argparse
import VersionHelper
from pathlib import Path
from py7zr import SevenZipFile
import shutil

SCRIPTS_DIR = Path(__file__).parent.resolve()
ROOT_DIR = SCRIPTS_DIR.parent
SOLUTION_FILE = ROOT_DIR / 'AltTab.sln'

def find_vcvarsall(version=17.7):
    return r'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat'

def removeDuplicates(variable):
    """Remove duplicate values of an environment variable."""
    old_list = variable.split(os.pathsep)
    new_list = []
    for i in old_list:
        if i not in new_list:
            new_list.append(i)
    new_variable = os.pathsep.join(new_list)
    return new_variable

def query_vcvarsall(version, arch="x64"):
    """Launch vcvarsall.bat and read the settings from its environment"""
    vcvarsall = find_vcvarsall()
    interesting = set(("include", "lib", "libpath", "path"))
    result = {}

    if vcvarsall is None:
        raise Exception("Unable to find vcvarsall.bat")
    print("Calling 'vcvarsall.bat %s' (version=%s)" % (arch, version))
    popen = subprocess.Popen('"%s" %s & set' % (vcvarsall, arch),
                             stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)

    stdout, stderr = popen.communicate()
    if popen.wait() != 0:
        # raise PackagingPlatformError(stderr.decode("mbcs"))
        raise Exception(stderr.decode("mbcs"))

    stdout = stdout.decode("mbcs")
    for line in stdout.split("\n"):
        if '=' not in line:
            continue
        line = line.strip()
        key, value = line.split('=', 1)
        key = key.lower()
        if key in interesting:
            if value.endswith(os.pathsep):
                value = value[:-1]
            result[key] = removeDuplicates(value)

    if len(result) != len(interesting):
        raise ValueError(str(list(result)))

    return result


def build_solution(solution_file_path, build_config_platform, build_action):
    if not os.path.exists(solution_file_path):
        print('Solution file not found: %s' % solution_file_path)
        return -1

    build_action = build_action.lower()
    command = 'devenv %s /%s \"%s\"' % (solution_file_path, build_action, build_config_platform)
    print('Running Command : [%s]' % command)
    os.system(command)
    print('\n')


# def read_version_info():
#     version_header = ROOT_DIR / r'source/version.h'
#     major, minor, patch, build = 0, 0, 0, 0  # Default values

#     try:
#         with open(version_header, 'r') as version_file:
#             for line in version_file:
#                 if line.startswith('#define AT_VERSION_MAJOR'):
#                     major = int(line.split()[2])
#                 elif line.startswith('#define AT_VERSION_MINOR'):
#                     minor = int(line.split()[2])
#                 elif line.startswith('#define AT_VERSION_PATCH'):
#                     patch = int(line.split()[2])
#                 elif line.startswith('#define AT_VERSION_BUILD'):
#                     build = int(line.split()[2])
#     except FileNotFoundError:
#         print(f"Warning: Version header file '{version_header}' not found. Using default version 'unknown'.")

#     return f"{major}.{minor}.{patch}.{build}"


def create_7z_archive(output_filename, files_to_pack):
    with SevenZipFile(output_filename, 'w') as archive:
        for file_path in files_to_pack:
            if os.path.exists(file_path):
                archive.write(file_path, os.path.basename(file_path))
            else:
                print(f"Warning: File '{file_path}' not found. Skipping.")


def create_package(config_name):
    # Change the file name based on the configuration
    config_name_new = 'ReleaseLogger' if config_name == 'Release' else config_name
    output_filename = r'{}\Releases\AltTab_{}_{}_x64.7z'.format(str(ROOT_DIR), VersionHelper.get_current_version(), config_name_new)
    files_to_pack = [
        r'{}\x64\{}\AltTab.exe'.format(ROOT_DIR, config_name),
        r'{}\AltTab.chm'.format(ROOT_DIR),
        r'{}\ReadMe.txt'.format(ROOT_DIR),
        r'{}\ReleaseNotes.txt'.format(ROOT_DIR)
    ]

    create_7z_archive(output_filename, files_to_pack)
    print(f"Package '{output_filename}' created successfully.")
    if config_name == 'ReleaseNoLogger':
        # Copy to other file name without configuration suffix
        copy_to = r'{}\Releases\AltTab_{}_x64.7z'.format(str(ROOT_DIR), VersionHelper.get_current_version())
        shutil.copy2(output_filename, copy_to)
        print(f"Copied '{output_filename}' to '{copy_to}'.")

def parse_arguments():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description='Build and create packages for AltTab project.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  # Build and package all configurations
  python CreatePackages.py
  
  # Skip building, only create packages
  python CreatePackages.py --skip-build
  
  # Build and package only Release configuration
  python CreatePackages.py --configs Release
  
  # Clean and rebuild specific configurations
  python CreatePackages.py --configs Release ReleaseNoLogger --build-action rebuild
  
  # Use different Visual Studio version
  python CreatePackages.py --vs-version 16.0
        ''')
    
    # Update version information
    parser.add_argument('--update-version',
                        action='store_true',
                        help='Update version information before building')

    parser.add_argument('--generate-version-header',
                        action='store_true',
                        help='Generate version header before building')

    # Clean build artifacts
    parser.add_argument('--clean-build',
                        action='store_true',
                        help='Clean build artifacts before building')

    parser.add_argument('--configs', 
                        nargs='+',
                        default=['Release', 'ReleaseNoLogger'],
                        choices=['Release', 'ReleaseNoLogger'],
                        help='Configuration(s) to build and package (default: Release ReleaseNoLogger)')
    
    parser.add_argument('--build-action',
                        choices=['clean', 'build', 'rebuild'],
                        default='build',
                        help='Build action to perform (default: build)')
            
    return parser.parse_args()


# Main execution
if __name__ == "__main__":
    args = parse_arguments()

    # Display the version information
    print(f"Current Version: {VersionHelper.get_current_version()}\n")
    
    # Setup Visual Studio environment
    vc_env = query_vcvarsall('17.7', 'x64')
    os.environ["Path"] = vc_env["path"]

    # Update version information
    if args.update_version:
        VersionHelper.update_version_info()
    elif args.generate_version_header:
        VersionHelper.create_version_header()

    sln_path = str(SOLUTION_FILE)
    # Clean build artifacts
    if args.clean_build or args.build_action == 'rebuild':
        for config in args.configs:
            build_solution(sln_path, f'{config}|x64', 'clean')
    
    # Perform the build action
    build_action = 'build' if args.build_action == 'rebuild' else args.build_action
    for config in args.configs:
        build_solution(sln_path, f'{config}|x64', build_action)

    # Create Releases directory if it doesn't exist
    releases_dir = ROOT_DIR / 'Releases'
    releases_dir.mkdir(exist_ok=True)

    # Create packages for specified configurations
    for config in args.configs:
        create_package(config)
