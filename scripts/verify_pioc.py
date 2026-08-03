# Fail the build when a generated PIOC blob differs from its ASM source.

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
