#! /usr/bin/env python3

"""Basic profiling for sudoku"""

import profile

from sudoku.util import gl
from sudoku.create import create_terminal_pattern
from sudoku.concrete import ProfileSolver, FastSolver

def grids():
    for n in range(267, 2000, 31):
        yield gl[n]

def main():
    for solver in map(FastSolver, grids()):
        solver.solve()

# def main():
#     for solver in map(ProfileSolver, grids()):
#         solver.solve()

if __name__ == '__main__':
    profile.run(main.__code__, sort='tottime')
