"""
Concrete sudoku solver classes which use various algorithms to solve sudoku
puzzles.
"""

from .errors import Catastrophic, NoNextMoveError
from .solver import (BasicSolver, Solver, Elimination, HiddenSingles,
                     NakedPairs, NakedTriples, NakedQuads,
                     HiddenPairs, HiddenTriples, HiddenQuads,
                     SimpleXWings, UniqueRectangles,
                     LockedCandidates, BUGPlusOne, Sledgehammer, Random)

class ProfileSolver(
    Solver,
    Elimination,
    HiddenSingles,
    NakedPairs,
    HiddenPairs,
    LockedCandidates,
    UniqueRectangles,
    SimpleXWings,
    HiddenTriples,
    NakedTriples,
    HiddenQuads,
    NakedQuads,
    BUGPlusOne,
    Sledgehammer
):
    """This is the solver used by the profiler script prof.py. Adjust this
    to test the relative speeds of Algorithm combinations.
    """

class UniqueProver(BasicSolver, Elimination, HiddenSingles, NakedPairs, Sledgehammer):
    """This is used to prove that a given sudoku grid has a unique
    solution.
    """
    def check(self):
        """Returns True if the puzzle has a unique solution; False
        otherwise.
        """
        self.solve()
        # Force a backtrack to the previous guess
        try:
            move = self.backtrack("Forced")
        except NoNextMoveError:
            # No guesses were made while solving the puzzle
            return True
        self.apply(move)
        try:
            self.solve()
        except NoNextMoveError:
            # Couldn't find another solution
            return True
        else:
            return False

class StartCreating(BasicSolver, Random):
    """This is a weird solver whose solve method just does eleven moves,
    which will all be RandomGuesses. This is used to generate random terminal
    patterns as the first step of creating a new puzzle.
    """
    def solve(self):
        #for n in range(self.howmanymoves()):
        for n in range(11):
            move = self.findnextmove()
            self.apply(move)

   # def howmanymoves(self):
   #     """Calculate the number of cells to fill into the new terminal pattern.
   #     This is set up to give 11 in the normal case of a sudoku grid with 9
   #     rows. However, 11 doesn't work well for other grid sizes.
   #     """
   #     size = self.state.size
   #     square = size * size
   #     return square // 10 + 3

class FinishCreating(BasicSolver, Elimination, Sledgehammer):
    """This solver is used to finish filling the grid which was started by
    StartCreating. Since grids at this point may have multiple solutions,
    it's much faster if we don't use any complicated algorithms. However,
    it's possible that StartCreating will generate a catastrophic case
    that takes forever to solve, so we raise an error if we start taking
    too long.
    """
    def solve(self):
        count = 0
        while not self.state.done:
            if count == 200:
                raise Catastrophic
            move = self.findnextmove()
            self.apply(move)
            count += 1

class FastSolver(Solver, Elimination, HiddenSingles, NakedPairs, Sledgehammer):
    """Designed to solve the widest variety of puzzles the fastest."""

class Slowpoke(Solver, Elimination, Random):
    """Designed to be super slow and take up hella memory."""
