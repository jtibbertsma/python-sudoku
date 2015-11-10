# Sudoku Solver in Python

This is a sudoku solver that I wrote in Python last year. This is basically
just a project that I wrote to learn all about the workings of the Python
interpreter. It includes a python module written in python C api. The main
object, the sudoku state object that represents the current state of a
sudoku puzzle, does some cool stuff like override attribute access at
a low level.

## Usage

Concrete solver classes are created by defining a class using multiple
inheritance; the first base is BasicSolver (or a direct subclass of BasicSolver,
such as Solver), followed by Algorithm subclasses in the order that they should
be tried. See solver.py for the definitions of BasicSolver, Solver, and
the Algorithm subclasses. See concrete.py for examples of concrete solver
classes.

When a solver solves a puzzle, it keeps track of the moves that it used
to solve the puzzle by storing a list of move object. See moves.py for
definitions of move objects.