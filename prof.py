#! /usr/bin/env python3

"""Basic profiling for sudoku"""

import profile

from engine.util import gl
from engine.create import create_terminal_pattern
from engine.concrete import ProfileSolver, FastSolver

def grids():
    for n in range(267, 2000, 31):
        yield gl[n]

def main():
    for solver in map(ProfileSolver, grids()):
        solver.solve()

if __name__ == '__main__':
    profile.run(main.__code__, sort='tottime')
