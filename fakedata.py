"""Backup implementation of datamodule.c"""

from . import config
from .errors import ContradictionError

def CandidateSet(*args):
    return set(args)

class BaseState:
    """Python fallback version of data.BaseState."""
    ispython = ...
    def __init__(self, grid, grconfig=None):
        self.size = 9   # We used to allow variable size puzzle
        self._grid = grid

        # If no config is given, build one.
        if not grconfig:
            grconfig = config.build_config()
        self.grconfig = grconfig

        # calculate peers
        self._calc_peers()

        # Calculate subgroups
        self._calc_subgroups()

        # allow moves such as trackback to force a next move
        self._movehook = None

        # calculate initial candidates
        self.terminals = {n for n in range(self.size)}
        self._candidates = {}
        for i in range(self.size):
            for j in range(self.size):
                key = i,j
                if key not in self._grid:
                    self._candidates[key] = self._getcandidates(key)

        self.dead_candidates = {}
        self.solved_keys = self.grid

    def _calc_peers(self):
        """Calculate and set the peers attribute on the State."""
        self.peers = config.calculate_peers(self.grconfig)

    def _calc_subgroups(self):
        """Calculate and set the row_subgroups and col_subgroups atributes."""
        rows, cols = config.calculate_subgroups(self.peers)

        self.row_subgroups = rows
        self.col_subgroups = cols

    def get_candidates(self, key):
        """Get a set from the candidates grid. A key error gets raised if
        a candidate set is requested from a solved cell.
        """
        return self._candidates[key]

    def set_candidates(self, key, cands):
        """Set a candidate set to be the candidates for a given position in
        the grid.
        """
        self._candidates[key] = cands

    def add_candidates(self, key, cands):
        """Add candidates to the candidate set for a particular key."""
        self._candidates[key] |= cands

    def remove_candidates(self, key, cands):
        """Remove candidates from the candidate set for a particular key.
        If the candidate set is left empty after removing the candidates,
        raise a ContradictionError.
        """
        self._candidates[key] -= cands
        if not self._candidates[key]:
            raise ContradictionError

    def key_solved(self, key):
        """Return True if this key has been solved."""
        return key in self._grid

    def key_value(self, key):
        """If the cell that the key represents is solved, return the value of
        that cell. If the cell isn't solved, raise a KeyError.
        """
        return self._grid[key]

    def delete_key(self, key):
        """Eliminate a key from the grid and set the candidates."""
        del self._grid[key]
        if key in self.dead_candidates:
            self._candidates[key] = self.dead_candidates[key]
            del self.dead_candidates[key]
        else:
            self._candidates[key] = self._getcandidates(key)

    def solve_key(self, key, digit):
        """Solve a position in the grid by adding the digit to the grid dict,
        and delete that key from the candidates dict.
        """
        self._grid[key] = digit
        self.dead_candidates[key] = self._candidates[key]
        del self._candidates[key]

    @property
    def movehook(self):
        """Used to force a solver to choose a move without deferring to any
        algorithms.
        """
        h = self._movehook
        self._movehook = None
        return h

    @movehook.setter
    def movehook(self, h):
        self._movehook = h

    @property
    def done(self):
        """True if the puzzle is complete."""
        return not self.candidates

    def _getcandidates(self, key):
        """For a given key, calculate the set of possible answers based on
        the contents of the current row, column, and group.
        """
        keyset = self.oneset(key)
        return self.terminals - {self.grid[j] for j in keyset if j in self.grid}

    def candidate_in_keyset(self, cand, keyset):
        """Search for a candidate in a set of keys. If found, return a list of keys
        from the keyset where the candidate was found. If not found, return None.
        """
        found = [
            key for key in keyset
                if key in self._candidates and cand in self._candidates[key]
        ]
        return found if found else None

    def candidates_from_keyset(self, keyset):
        """Given an iterable of keys, return a set of all candidates that are
        in at least one of the keys.
        """
        cands = set()
        for key in keyset:
            if key in self._candidates:
                cands |= self._candidates[key]
        return cands

class KeyChecker:
    """Base class for any algorithm that checks the keys of the grid in some
    order. This class defines several generator methods that yield keys from
    the grid in some order. This class serves to group these methods in
    the same place. The naming convention is that any method from this class
    starts with 'order_'.
    """
    def order_simple(self):
        """Just iterate through the candidates normally. This seems to be the
        best choice for the HiddenSingles algorithm.
        """
        yield from self.state._candidates

    def order_by_num_candidates(self):
        """Check keys based on the number of candidates for that key. Keys that
        have fewer candidates are checked first.
        """
        yield from sorted(
            self.state._candidates,
            key=lambda k: len(self.state._candidates[k])
        )

    def order_by_num_candidates_reversed(self):
        """Check keys with more candidates first."""
        yield from sorted(
            self.state._candidates,
            key=lambda k: len(self.state._candidates[k]),
            reverse=True
        )

    def order_random(self):
        """Check unsolved cells in random order. This is 'naive' because there
        must be a more efficient way to do this, but I don't want to figure it
        out right now.
        """
        s = self.state.size - 1
        while True:
            key = randint(0,s), randint(0,s)
            if key in self.state._candidates:
                yield key

    def order_exactly_one(self):
        """Find cells with one candidate"""
        for key in self.state._candidates:
            if len(self.state._candidates[key]) == 1:
                yield key

    def order_exactly_two(self):
        """Used by naked pairs algorithm to find any possible pairs."""
        for key in self.state._candidates:
            if len(self.state._candidates[key]) == 2:
                yield key
