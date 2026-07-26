# PlatformIO pre-build hook: assert the committed PIOC blobs match their source.
#
# Runs assemble.py (without --write) on each blob before every build, so an 
# .ASM edited without regenerating its _inc.h (or a hand-edited _inc.h) fails 
# the build instead of silently shipping a stale blob.
# assemble.py exits non-zero on mismatch.

Import("env")

from pathlib import Path
import subprocess
import sys


project_dir = Path(env.subst("$PROJECT_DIR"))
for blob in ("tapioca_swd.ASM", "tapioca_rvswio.ASM", "tapioca_rvswd.ASM"):
    subprocess.check_call(
        [sys.executable, str(project_dir / "pioc" / "assemble.py"),
         str(project_dir / "pioc" / blob)],
        cwd=project_dir,
    )
