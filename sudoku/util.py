"""A junkyard of sorts"""

from time import time
from pprint import pprint
import itertools
import gmpy2
from copy import deepcopy
import collections.abc
from random import randint


## Get every name used in the package
from .data import *;     from . import data
from .solver import *;   from . import solver
from .create import *;   from . import create
from .moves import *;    from . import moves
from .concrete import *; from . import concrete
from .config import *;   from . import config
from .errors import *;   from . import errors
import sudoku

# TODO: Clean this up and write docstrings and make it an actual module.
#       Move experimentation type stuff into a file called junk.py and keep
#       useful functions here.

def solve(puzzle):
    s = MS(string_to_grid(puzzle))
    s.solve()
    print_grid(s.state.grid)

def keygen(size):
    for n in range(size):
        for m in range(size):
            yield n,m

def print_grid(grid, size=9):
    sqrt = int(gmpy2.sqrt(size))
    bb = lambda: print(' '*(5*sqrt) + ('   |    ' + (' '*(5*(sqrt-1))))*(sqrt-1))
    bb()
    for n,k in enumerate(keygen(size)):
        if n != 0 and n % size == 0:
            print()
            if n % (size*sqrt) == 0:
                bb()
                print(' ' + ('-'*17 + '\u04dc') * 2 + '-'*17)
                bb()
            else:
                bb()
        if n % size != 0 and n % sqrt == 0:
            try:
                print('   |{:4}'.format(grid[k]+1), end='')
            except KeyError:
                print('   |   .', end='')
        else:
            try:
                print(format(grid[k]+1,'5'), end='')
            except KeyError:
                print('    .', end='')
    print()
    bb()

def set_repr(s):
    base = ['.' if n not in s else str(n+1) for n in range(9)]
    return '< {}   {}   {}   {}   {}   {}   {}   {}   {} >'.format(
        base[0], base[1], base[2], base[3],
        base[4], base[5], base[6], base[7], base[8]
    )

def newpuke(solver):
    count = 0
    while not solver.state.done:
        count += 1
        print(count)
        print()
        for i in range(9):
            for j in range(9):
                key = i,j
                if key not in self.state.solved_keys:
                    cands = solver.state.candidates[key]
                    if isinstance(cands, set):
                        r = set_repr(cands)
                    else:
                        r = repr(cands)
                    print(key, '        ', r)
        print()
        print_grid(solver.state.grid)
        print()
        move = solver.findnextmove()
        print(move)
        print()
        solver.apply(move)
    print_grid(solver.state.grid)

index_to_key = lambda index: (index // 9, index % 9)
key_to_index = lambda key: (key[0] * 9 + key[1])

def puke_candidates(state):
    for key in state.candidates:
        print(key, state.candidates[key])

def ts(solver):
    a = time()
    solver.solve()
    b = time()
    return format(b - a, '.3f')

fart = lambda: print_grid(create_terminal_pattern())

veryhard = '8..........36......7..9.2...5...7.......457.....1...3...1....68..85...1..9....4..'

h = lambda: string_to_grid(veryhard)

def string_to_grid(string):
    grid = {}
    for n, c in enumerate(string):
        if c not in '123456789':
            continue
        grid[index_to_key(n)] = int(c) - 1
    return grid

class list_of_immutable_grids(collections.abc.Sequence):
    def __init__(self, filename):
        with open(filename) as file:
            self.grid_list = list(map(string_to_grid, file))
    def __getitem__(self, item):
        return self.grid_list[item].copy()
    def __len__(self):
        return len(self.grid_list)

gl = list_of_immutable_grids('/Users/J/Source/sudoku/puzzles.txt')
tg = list_of_immutable_grids('/Users/J/Source/sudoku/tough.txt')
vr = list_of_immutable_grids('/Users/J/Source/sudoku/varying.txt')
ah = list_of_immutable_grids('/Users/J/Source/sudoku/andhow.txt')
mp = list_of_immutable_grids('/Users/J/Source/sudoku/morepuzzles.txt')

class Watcher(Solver):
    def __init__(self, state, **kwargs):
        self.watch_type = kwargs.pop('watch', None)
        if self.watch_type is not None:
            if not issubclass(self.watch_type, Move):
                raise TypeError(
                    'Expected a Move subclass, got {}'.format(
                        self.watch_type)
                )
        super().__init__(state, **kwargs)

    def solve(self):
        """Break out once we see a particular type of move."""
        while not self.state.done:
            move = self.findnextmove()
            if self.watch_type:
                if isinstance(move, self.watch_type):
                    return move
            self.apply(move)

    def print_cands(self):
        for key in self.order_simple():
            print(key, self.state.candidates[key])

MS = ProfileSolver
FS = FastSolver

class AS(Watcher, Elimination, NakedPairs, HiddenSingles, LockedCandidates, 
         HiddenPairs, SimpleXWings, UniqueRectangles, HiddenTriples, HiddenQuads,
         NakedTriples, NakedQuads, BUGPlusOne, Sledgehammer):
    pass

def look_for_move(move_type):
    assert issubclass(move_type, Move)
    for string, grid_list in (('vr',vr),('tg',tg),('gl',gl),('ah',ah),('mp',mp)):
        for n, grid in enumerate(grid_list):
            s = AS(grid, watch=move_type)
            q = s.solve()
            if q is not None:
                print('Found one: list = {}, puzzle number={}'.format(string, n))

def list_runtimes(grid_list, key=lambda x: x[1], solver=MS):
    times = [(n,ts(solver(grid))) for n,grid in enumerate(grid_list)]
    yield from sorted(times, key=key)

def print_runtimes(grid_list, key=lambda x: x[1], solver=MS):
    for t in list_runtimes(grid_list, key=key, solver=solver):
        print('{:4}: {}'.format(*t))

def average_runtime(grid_list, solver=MS):
    sum = 0.0
    for grid in grid_list:
        s = solver(grid)
        a = time()
        s.solve()
        b = time()
        sum += b - a
    return format(sum / len(grid_list), '.3f')

def test_solver(Solver, list):
    for n,grid in enumerate(list):
        s = Solver(grid)
        try:
            string = ts(s)
        except NoNextMoveError:
            print('{:4}: Failed'.format(n))
        else:
            print('{:4}: Succeeded: {}'.format(n, string))

def dump_cands(state):
    for offset in (0,9,18):
        for n in range(offset,offset+9):
            string = 'Group' if offset == 0 else 'Column' if offset == 9 else 'Row'
            print('{} {}: {}'.format(string, n-offset+1, state.candidates_from_house(n)))
            for key in state.houses[n]:
                if key not in state.solved_keys:
                    print(key, state.candidates[key])

class CheckHouses(Solver):
    def from_base(self):
        return [self.state.candidates_from_house(house) for house in range(27)]

    def from_candidates(self):
        res = []
        for keyset in self.state.houses:
            sets = [
                self.state.candidates[key] for key in keyset
                    if key not in self.state.solved_keys
            ]
            house = [0 for n in range(9)]
            for candidate in itertools.chain(*sets):
                house[candidate] += 1
            res.append(tuple(house))
        return res

    def print_house(self, house):
        house_keys = self.state.houses[house]
        print('Correct: {}'.format(self.from_candidates()[house]))
        print('Faulty:  {}'.format(self.from_base()[house]))
        for key in house_keys:
            if key not in self.state.solved_keys:
                print(key, self.state.candidates[key])

    @property
    def bad_houses(self):
        bh = []
        a = self.from_base()
        b = self.from_candidates()
        for n in range(27):
            if a[n] != b[n]:
                bh.append(n)
        return bh

    def solve(self):
        while not self.state.done:
            if self.from_base() != self.from_candidates():
                raise SudokuError
            move = self.findnextmove()
            self.apply(move)

class CS(CheckHouses, MS):
    pass

class Puker(FastSolver):
    def solve(self):
        while not self.state.done:
            print('###')
            move = self.findnextmove()
            print(move)
            self.apply(move)

r = lambda list=gl: State(list[randint(0,len(list)-1)])

def fart_size():
    a = time()
    create_terminal_pattern()
    b = time()
    return format(b - a, '.3f')

