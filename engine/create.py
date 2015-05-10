"""
This module defines whatever for generating new sudoku puzzles
of varying difficulty.
"""

from .concrete import UniqueProver, StartCreating, FinishCreating
from .data import State
from .errors import NoNextMoveError, Catastrophic

def check_unique(grid, grconfig=None):
    """Shortcut to call UniqueProver.check on a grid."""
    gc = grid.copy()
    state = State(gc, grconfig=grconfig)
    up = UniqueProver(state)
    return up.check()

def create_terminal_pattern(*, grconfig=None):
    """Make a randomized solved grid."""
    state = State({}, grconfig)
    sc = StartCreating(state)
    sc.solve()
    fin = FinishCreating(state)
    try:
        fin.solve()
    except NoNextMoveError:
        # Oops, sc created a puzzle with no solutions; start over
        return create_terminal_pattern(grconfig=grconfig)
    except Catastrophic:
        # See docstring for errors.Catastrophic
        return create_terminal_pattern(grconfig=grconfig)
    return state.clues
