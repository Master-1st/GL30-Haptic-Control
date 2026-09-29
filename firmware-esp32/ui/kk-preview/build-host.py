"""Build and test the shared C preview with the caller's CMake toolchain."""
import argparse
from pathlib import Path
import subprocess


def main():
    source = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=source / "build")
    parser.add_argument("--build-type", choices=("Debug", "Release", "RelWithDebInfo"),
                        default="Release")
    parser.add_argument("--generator", help="Optional CMake generator, for example Ninja")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    configure = ["cmake", "-S", str(source), "-B", str(build),
                 f"-DCMAKE_BUILD_TYPE={args.build_type}"]
    if args.generator:
        configure.extend(["-G", args.generator])
    subprocess.run(configure, check=True)
    subprocess.run(["cmake", "--build", str(build), "--config", args.build_type,
                    "--parallel", "2"], check=True)
    subprocess.run(["ctest", "--test-dir", str(build), "-C", args.build_type,
                    "--output-on-failure"], check=True)


if __name__ == "__main__":
    main()
