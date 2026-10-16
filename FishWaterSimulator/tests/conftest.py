import sys
from pathlib import Path


SIMULATOR_DIR = Path(__file__).resolve().parents[1]
if str(SIMULATOR_DIR) not in sys.path:
    sys.path.insert(0, str(SIMULATOR_DIR))
