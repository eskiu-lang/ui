"""Where the Eskiu compiler is: ESKIUC / ESKIUC_SELF, else an eskiu checkout next to this
repository (or ESKIU_ROOT), else the PATH."""
import os
from pathlib import Path
import shutil

HERE = Path(__file__).resolve().parents[1]
ROOT = Path(os.environ.get("ESKIU_ROOT", HERE.parent / "eskiu")).resolve()


def compiler(variable="ESKIUC", name="eskiuc", required=True):
    if os.environ.get(variable):
        return str(Path(os.environ[variable]).resolve())
    built = ROOT / "build" / name
    if built.is_file():
        return str(built)
    found = shutil.which(name)
    if found or not required:
        return found
    raise SystemExit(f"{name} not found: set {variable}, ESKIU_ROOT or add it to PATH")


def compiler_env():
    """The environment for the compiler: ESKIU_ROOT only when a checkout is there."""
    env = dict(os.environ)
    if (ROOT / "stdlib").is_dir():
        env["ESKIU_ROOT"] = str(ROOT)
    return env
