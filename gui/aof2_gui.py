"""Backward-compatible public imports and launcher for the AoF workbench."""

import range_grid
import strategy_data
import workbench


def __getattr__(name):
    for module in (workbench, strategy_data, range_grid):
        if hasattr(module, name):
            return getattr(module, name)
    raise AttributeError(name)


def main():
    return workbench.main()


if __name__ == "__main__":
    raise SystemExit(main())
