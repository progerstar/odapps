"""PyInstaller entry point: the package uses relative imports, so it cannot be the entry script itself."""
import sys

from ndef_writer.cli import main

if __name__ == "__main__":
    sys.exit(main())
