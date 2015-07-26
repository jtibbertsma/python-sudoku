"""This is where exceptions defined for the sudoku package are located."""

class SudokuError(Exception):
    """Base exception for any errors defined in this package."""

class ContradictionError(SudokuError):
    """Raised when a contradiction is found in the sudoku grid, such as
    an empty candidate set or a failed call to integrity_check.
    """

class NoNextMoveError(SudokuError):
    """Raised when a solver fails to find another move. This could happen
    when the algorithms that a solver uses are not adequate to solve the
    puzzle, or if the puzzle has no possible solution.
    """

class Catastrophic(SudokuError):
    """Raised when creating a random terminal pattern if we've hit a
    catastrophic case. Normally, creating a random terminal pattern takes
    0.005 seconds. The majority of cases take this long; a few cases take
    a little longer. However, 1 in every 200 times or so, we will generate
    a catastrophic case where the initial pattern we generate take many
    seconds or minutes. Worse yet, we can generate a case that has no
    solutions and will run for hours as the solver does a depth first search
    over the space of all possible grid configurations.

    To prevent these catastrophic cases, we raise this exception after
    we've gone through the main solver loop 200 times. (Normally it would
    take around 50.) See FinishCreating in concrete.py.
    """

class MoveArgError(SudokuError, TypeError):
    """Move class initializers rely heavily on required keyword arguments.
    This error is raised for missing arguments in move constructors.
    """
