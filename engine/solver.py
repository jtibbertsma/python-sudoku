"""
This module defines the framework for building sudoku solver classes.
The base classes in this module are BasicSolver and Algorithm.

Concrete solver classes are created by defining a class using multiple
inheritance; the first base is BasicSolver (or a direct subclass of BasicSolver,
such as Solver), followed by Algorithm subclasses in the order that they should
be tried.

To define an Algorithm class, create a class that derives from Algorithm
and defines the function nextmove, which looks at the State object and
returns an object that inherits from Move (see moves.py). The idea is
that algorithms inspect the state, and moves mutate the state.
"""

from abc import ABCMeta, abstractmethod
from random import randint
from itertools import combinations, chain

from .errors import ContradictionError, NoNextMoveError
from .moves import (EliminationMove, Backtrack, Guess, SimpleGuess,
                    RandomGuess, HiddenSingleMove, LockedCandidateMove,
                    NakedPairMove, NakedTripleMove, NakedQuadMove,
                    HiddenPairMove, HiddenTripleMove, HiddenQuadMove,
                    UniqueRectangleMove, XWingMove, FinnedXWingMove,
                    SashimiXWingMove, BUGMove)
from .data import State, CandidateSet

##
## Base Classes
##

class SolverMeta(ABCMeta):
    """Metaclass for solvers. Enforces a few rules about subclasses of BasicSolver:

    1) Only subclasses of BasicSolver may use SolverMeta.
    2) BasicSolver must come before any subclass of Algorithm in the mro of a
        BasicSolver subclass.
    3) A BasicSolver subclass can only instatiate objects if it has Algorithm in
        its mro. Similarly, subclasses of Algorithm that don't have BasicSolver
        in their mro can't instatiate objects. This rule allows subclasses of
        Algorithm to assume that they have certain attributes (i.e. state) without
        worrying about attribute errors.

    You can define solver classes on the fly with SolverMeta(name, bases, {}).
    """
    initialized = False

    def __new__(mcls, name, bases, ns):
        # If we're building BasicSolver, don't do anything.
        if not mcls.initialized:
            mcls.initialized = True
            return super().__new__(mcls, name, bases, ns)

        seen_sol = False
        seen_alg = False
        bad_mro = False
        for base in bases:
            if hasattr(base, '__can_inst__'):    # Subclass of a concrete solver
                assert issubclass(base, BasicSolver) and issubclass(base, Algorithm)
                return super().__new__(mcls, name, bases, ns)
            if issubclass(base, Algorithm):
                if not seen_sol:    # violates rule 2
                    bad_mro = True
                else:
                    seen_alg = True
                    break
            if issubclass(base, BasicSolver):
                seen_sol = True

        if not seen_sol:    # violates rule 1
            raise TypeError(
                "SolverMeta can only be the metaclass for subclasses of BasicSolver"
            )
        if bad_mro:
            raise TypeError(
                "Bad MRO for BasicSolver subclass; "
                "found an Algorithm before BasicSolver"
            )
        cls = super().__new__(mcls, name, bases, ns)
        if seen_alg:  # class can instatiate objects
            cls.__can_inst__ = True
        return cls

class Algorithm(metaclass=ABCMeta):
    """Base class for an algorithm. Algorithms are mixin classes that
    define the method 'nextmove', which builds a move object. Each nextmove
    method must call super().nextmove so that algorithms can be tried in
    succession.

    Algorithms can also define a method 'apply' which takes a move object as
    an argument and is called when a move is about to mutate the state. This
    is to allow algorithms to keep track of any information they might need.
    """
    def __new__(cls, **kwargs):
        if not hasattr(cls, '__can_inst__'):    # See SolverMeta
            raise TypeError('Algorithms can\'t instatiate objects on their own')
        return super().__new__(cls)

    @abstractmethod
    def nextmove(self):
        raise NoNextMoveError

    def apply(self, move):
        """Prevents this error during application of move in a solver class: 
        "AttributeError: 'super' object has no attribute 'apply'". This is
        necessary since we require that any 'apply' method must at some point
        call 'super().apply'.
        """

class BasicSolver(metaclass=SolverMeta):
    """Base class for a solver. To create a solver class, derive from this
    class first, followed by algorithm classes in the order that you want
    them to be tried. ex:

    >>> class MySolver(BasicSolver, Elimination, Sledgehammer):
    ...     pass
    ...
    >>> solver = MySolver(state)
    >>> solver.solve()

    Now the puzzle in state is solved, and the solver kept track of the
    moves in order used to solve the puzzle. The solver would first try
    the elimination algorithm, followed by the guess and backtrack method.
    """
    def __new__(cls, state, **kwargs):
        if not hasattr(cls, '__can_inst__'):
            raise TypeError('BasicSolver can\'t instatiate objects with no algorithms')
        return super().__new__(cls, **kwargs)

    def __init__(self, state, **kwargs):
        if isinstance(state, dict):
            state = State(state)
        self.state = state
        super().__init__(**kwargs)

    def findnextmove(self):
        """Let algorithm classes search for moves. This function is not to be
        used on a solved puzzle. We're allowed to assume that a puzzle is unsolved
        in any nextmove methods.
        """
        # Check to see if a next move is forced
        hook = self.state.movehook
        if hook is not None:
            return hook
        try:
            return self.nextmove()
        except ContradictionError as ce:
            # Need to backtrack
            if hasattr(self, 'backtrack') and self.btstack:
                return self.backtrack(ce.args[0])
            raise ce

    def apply(self, move):
        """Let a move mutate the state."""
        super().apply(move)
        try:
            move.do()
        except ContradictionError as ce:
            # Need to backtrack
            if hasattr(self, 'backtrack') and self.btstack:
                self.state.movehook = self.backtrack(ce.args[0])
            else:
                raise ce

    def solve(self):
        """Do moves until the puzzle is complete. If a puzzle has no solutions, or can
        not be completed by the algorithms given, then a NoNextMoveError is raised. If
        the puzzle has multiple solutions, then this will find one of the possible
        solutions.
        """
        while not self.state.done:
            move = self.findnextmove()
            self.apply(move)

class Solver(BasicSolver):
    """Keep track of the moves that are applied to the state."""
    def __init__(self, state, **kwargs):
        self.moves = []
        super().__init__(state, **kwargs)

    def apply(self, move):
        """Apply the move, and add it to the moves list, not necessarily in that
        order.
        """
        self.moves.append(move)
        super().apply(move)

    @property
    def move_tree(self):
        """Each move that is found and applied is stored in a list, including
        bad guesses and their search branches. This property matches each bad
        guess with a backtrack and puts the branch between them into a list.
        This property returns a list containing good moves and lists containing
        branches of bad moves.
        """
        moves = self.moves
        guess_stack = []
        pos = 0
        while pos < len(moves):
            if isinstance(moves[pos], Guess):
                guess_stack.append(pos)
            elif isinstance(moves[pos], Backtrack):
                g = guess_stack.pop()
                branch = moves[g:pos+1]
                moves = moves[:g] + [branch] + moves[pos+1:]
                pos -= len(branch)
            pos += 1
        return moves

##
## Algorithms
##

class CachingAlgorithm(Algorithm):
    """This is base class to be used by any algorithm that uses a cache to
    keep from redoing unnecassary calculations when finding moves. Such
    caches need to be cleared when backtracking. Each cache is a set of
    items.

    Some items should only be added to the cache when a move is applied; otherwise
    nextmove might return different results if called twice in a row without
    applying a move. (The desired behavior is for self.nextmove to give the same
    move again and again if apply isn't called.) To solve this problem, we use
    the cache_on_apply* attributes.
    """
    def __init__(self, **kwargs):
        self.cache_list = []
        self.cache_on_apply = None
        self.cache_on_apply_value = None
        super().__init__(**kwargs)

    def clear_caches(self):
        """Clear all caches in the cache list."""
        for cache in self.cache_list:
            cache.clear()

    def apply(self, move):
        if self.cache_on_apply:
            self.cache_on_apply.add(self.cache_on_apply_value)
            self.cache_on_apply = None
            self.cache_on_apply_value = None
        super().apply(move)

class Elimination(Algorithm):
    """Look for cells with only one candidate. Also known as 'naked singles'.
    This algorithm will succeed more often than any other.
    """
    def nextmove(self):
        for key in self.state.order_exactly_n(1):
            cands = self.state.candidates[key]
            return EliminationMove(
                self.state, key=key, digit=next(cands)
            )
        return super().nextmove()

class HiddenSingles(Algorithm):
    """Search the grid for hidden singles. A hidden single is the only appearence
    of a candidate in a row, column, or group.
    """
    def nextmove(self):
        for house, house_keys in enumerate(self.state.houses):
            for digit, count in enumerate(self.state.candidates_from_house(house)):
                if count == 1:
                    mark = house // 9
                    key = next(iter(self.state.candidate_in_keyset(digit, house_keys)))
                    return HiddenSingleMove(
                        self.state, mark=mark, key=key, digit=digit
                    )
        return super().nextmove()

class LockedCandidates(Algorithm):
    """In a sudoku grid, a subgroup is the intersection between a row or column
    and a group. If there is a candidate in a group that's only in one subgroup,
    that candidate can be eliminated from the row or column outside of that
    group. Likewise, if a candidate is in only one subgroup of a row or column,
    it can be eliminated from the group outside the subgroup.
    """
    def nextmove(self):
        for subgroup in chain(set(self.state.row_subgroups.values()),
                              set(self.state.col_subgroups.values())):
            for digit in self.state.candidates_from_keyset(subgroup[0]):
                in_row_or_col = self.state.candidate_in_keyset(digit, subgroup[1])
                in_group = self.state.candidate_in_keyset(digit, subgroup[2])
                if in_row_or_col:
                    if not in_group:
                        return LockedCandidateMove(
                            self.state, mark=0, digit=digit, subgroup=subgroup[0],
                            change={key: CandidateSet(digit) for key in in_row_or_col}
                        )
                    continue
                else:
                    if in_group:
                        return LockedCandidateMove(
                            self.state, mark=1, digit=digit, subgroup=subgroup[0],
                            change={key: CandidateSet(digit) for key in in_group}
                        )
        return super().nextmove()

class NakedSets(CachingAlgorithm):
    """Base algorithm for NakedPairs, NakedTriples, and NakedQuads. Suppose that
    we have a grid where keys (1,3) and (1,8) have candidates {2,7}. Then since
    one of those keys has to be 2 and the other has to be 7, we can eliminate 2
    and 7 from every other position in that row. In this case, (1,3) and (1,8)
    are naked pairs in row 1. The same principle applies for NakedTriples and
    NakedQuads.
    """
    def __init__(self, **kwargs):
        self.naked_keyset_cache = set()
        super().__init__(**kwargs)
        self.cache_list.append(self.naked_keyset_cache)

    def xxx_naked_find(self, count):
        """Do the actual work for the algorithm. If count is 2, search for naked
        pairs, if 3, search for naked triples, etc.
        """
        assert count > 1

        # Create a dictionary with CandidateSets as keys. The values are sets
        # of keys with that CandidateSet as their candidates.
        equalsets = {}
        for key in self.state.order_exactly_n(count):
            cands = self.state.candidates[key]
            if cands not in equalsets:
                equalsets[cands] = set()
            equalsets[cands].add(key)

        # Check every combination of keys with the same candidates
        for digits, equal_cands in equalsets.items():
            for keyset in combinations(equal_cands, count):
                if keyset in self.naked_keyset_cache:
                    continue
                # First, figure out if the keys share a house, and if they do,
                # what house. If they are in the same house, variable 'mark'
                # is set as follows:
                #     0: row
                #     1: column
                #     2: group
                #     3: row, same group
                #     4: column, same group
                a_key = keyset[0]
                same_group = True
                the_group = self.state.grconfig[a_key]
                for key in keyset[1:]:
                    if self.state.grconfig[key] is not the_group:
                        same_group = False
                        break
                for offset in range(2):
                    same_row_or_col = True
                    for key in keyset[1:]:
                        if key[offset] != a_key[offset]:
                            same_row_or_col = False
                            break
                    if same_row_or_col:
                        break
                if same_group:
                    mark = 2
                    if same_row_or_col:
                        mark += offset + 1
                elif same_row_or_col:
                    mark = offset
                # If neither same_group nor same_row_or_col, the keys don't share
                # a house.
                else:
                    self.naked_keyset_cache.add(keyset)
                    continue

                # Figure out a set of keys that candidates can be eliminated from
                if mark == 0:
                    search_set = set(self.state.rows[a_key[0]])
                elif mark == 1:
                    search_set = set(self.state.cols[a_key[1]])
                elif mark == 2:
                    search_set = set(the_group)
                else:
                    if mark == 3:
                        subgro = self.state.row_subgroups[a_key]
                    elif mark == 4:
                        subgro = self.state.col_subgroups[a_key]
                    search_set = subgro[0]|subgro[1]|subgro[2]
                search_set -= set(keyset)

                # Calculate change dictionary to pass into move constructor
                change = {}
                for key in search_set:
                    if key not in self.state.solved_keys:
                        elim = digits & self.state.candidates[key]
                        if elim:
                            change[key] = elim
                if change:
                    self.cache_on_apply = self.naked_keyset_cache
                    self.cache_on_apply_value = keyset
                    return mark, keyset, digits, change
                self.naked_keyset_cache.add(keyset)
                del keyset

class NakedSetsNextAttempt(CachingAlgorithm):
    """The original NakedSets implementation is shit and we all know it. Let's
    scrap it and write a new one.
    """
    def __init__(self, **kwargs):
        self.naked_keyset_cache = set()
        super().__init__(**kwargs)
        self.cache_list.append(self.naked_keyset_cache)

    def naked_find(self, count):
        """Do the actual work for the algorithm. If count is 2, search for naked
        pairs, if 3, search for naked triples, etc.
        """
        assert count > 1
        houses = [
            {key for key in house if key not in self.state.solved_keys}
                for house in self.state.houses
        ]
        for house, house_keys in enumerate(houses):
            for keyset in combinations(house_keys, count):
                if keyset in self.naked_keyset_cache:
                    continue

                digits = CandidateSet()
                for key in keyset:
                    digits |= self.state.candidates[key]
                if len(digits) == count:
                    # We've found a naked set, so now we need to figure out whether
                    # it's in a row or col or group. This information is stored in the
                    # variable 'mark' as follows:
                    #     0: row
                    #     1: column
                    #     2: group
                    #     3: row, same group
                    #     4: column, same group
                    mark = house  // 9
                    a_key = keyset[0]
                    if mark == 0 and count < 4:   # Maybe share a subgroup
                        for i in range(2):
                            same_row_or_col = True
                            for key in keyset[1:]:
                                if key[i] != a_key[i]:
                                    same_row_or_col = False
                                    break
                            if same_row_or_col:
                                break
                        if same_row_or_col:
                            mark += i + 3
                        else:
                            mark = 2
                    elif mark == 2:
                        mark = 0

                    # Figure out a set of keys that candidates can be eliminated from
                    if mark < 3:
                        search_set = house_keys
                    else:
                        if mark == 3:
                            subgro = self.state.row_subgroups[a_key]
                        elif mark == 4:
                            subgro = self.state.col_subgroups[a_key]
                        search_set = subgro[0]|subgro[1]|subgro[2]
                    search_set -= set(keyset)

                    # XXX This code is hella WET
                    # Calculate change dictionary to pass into move constructor
                    change = {}
                    for key in search_set:
                        if key not in self.state.solved_keys:
                            elim = digits & self.state.candidates[key]
                            if elim:
                                change[key] = elim
                    if change:
                        self.cache_on_apply = self.naked_keyset_cache
                        self.cache_on_apply_value = keyset
                        return mark, keyset, digits, change
                    self.naked_keyset_cache.add(keyset)
                del keyset

class NakedPairs(NakedSets):
    """Search for naked pairs. We use the broken version of NakedSets because the
    working one is SLOW. It seems to work fine for pairs.
    """
    def nextmove(self):
        move = self.xxx_naked_find(2)
        if move is not None:
            mark, keyset, digits, change = move
            return NakedPairMove(
                self.state, mark=mark, keyset=keyset,
                digits=digits, change=change
            )
        return super().nextmove()

class NakedTriples(NakedSetsNextAttempt):
    """Search for naked triples."""
    def nextmove(self):
        move = self.naked_find(3)
        if move is not None:
            mark, keyset, digits, change = move
            return NakedTripleMove(
                self.state, mark=mark, keyset=keyset,
                digits=digits, change=change
            )
        return super().nextmove()

class NakedQuads(NakedSetsNextAttempt):
    """Search for naked quads."""
    def nextmove(self):
        move = self.naked_find(4)
        if move is not None:
            mark, keyset, digits, change = move
            return NakedQuadMove(
                self.state, mark=mark, keyset=keyset,
                digits=digits, change=change
            )
        return super().nextmove()

class HiddenSets(Algorithm):
    """Base algorithm for algorithms that search for hidden sets. For example,
    if there are a pair of candidates that appear in only two cells in a row,
    we can eliminate other candidates from those two cells. Then these cells
    are called hidden pairs.
    """
    def hidden_find(self, count):
        """Search for hidden sets of size count. If count is 2, search for
        hidden pairs, etc.
        """
        assert count > 1

        # search for candidates that appear a certain amount of times in a house
        for house, house_keys in enumerate(self.state.houses):
            cand_counts = self.state.candidates_from_house(house)
            possibles = tuple(
                n for n,k in enumerate(cand_counts)
                    if k <= count and k > 0
            )
            for poset in combinations(possibles, count):
                keyset = set(self.state.candidate_in_keyset(poset[0], house_keys))
                for cand in poset[1:]:
                    keyset |= set(self.state.candidate_in_keyset(cand, house_keys))
                if len(keyset) != count:
                    continue

                # We've found a set; make sure it isn't naked
                digits = CandidateSet(*poset)
                change = {}
                for key in keyset:
                    cands = self.state.candidates[key]
                    elim = cands - digits
                    if elim:
                        change[key] = elim
                if change:
                    return house // 9, keyset, digits, change
                del poset

class HiddenPairs(HiddenSets):
    """Search for hidden pairs."""
    def nextmove(self):
        move = self.hidden_find(2)
        if move is not None:
            mark, keyset, digits, change = move
            return HiddenPairMove(
                self.state, mark=mark, keyset=keyset,
                digits=digits, change=change
            )
        return super().nextmove()

class HiddenTriples(HiddenSets):
    """Search for hidden triples."""
    def nextmove(self):
        move = self.hidden_find(3)
        if move is not None:
            mark, keyset, digits, change = move
            return HiddenTripleMove(
                self.state, mark=mark, keyset=keyset,
                digits=digits, change=change
            )
        return super().nextmove()

class HiddenQuads(HiddenSets):
    """Search for hidden quads."""
    def nextmove(self):
        move = self.hidden_find(4)
        if move is not None:
            mark, keyset, digits, change = move
            return HiddenQuadMove(
                self.state, mark=mark, keyset=keyset,
                digits=digits, change=change
            )
        return super().nextmove()

class cache_for_sf(list):
    """Private list subtype for SetFinder; implements clear so that it can be
    used by the CachingAlgorithm protocol.
    """
    def __new__(cls, *args, **kwargs):
        return [set() for n in range(27)]
    def clear(self):
        for s in self:
            s.clear()

class SetFinder(CachingAlgorithm):
    """Analyze each house looking for naked pairs, naked triples etc. Finding
    a naked set also implies finding a hidden set. For example, if we find
    a naked pair in a row with five unsolved cells, the three remaining cells
    comprise a hidden triple.

    This algorithm assumes that any possible hidden or naked singles have been
    found already.
    """
    def __init__(self, **kwargs):
        self.sf_fin_houses = set()
        self.sf_elim_keys = cache_for_sf()
        super().__init__(**kwargs)
        self.cache_list.append(self.sf_fin_houses)
        self.cache_list.append(self.sf_elim_keys)

    def do_setfind(self):
        """Do a search for sets up to whatever is in our MRO. So if only
        NakedPairs are in the MRO, we only search for two item naked sets,
        but if NakedQuads are in the MRO, search for naked pairs, naked triples,
        and naked quads (Even if NakedPairs isn't explicitly in the MRO.)
        """
        # Get a list of houses containing unsolved keys
        houses = [
            {key for key in house if key not in self.state.solved_keys}
                for house in self.state.houses
        ]
        for house, house_keys in enumerate(houses):
            # check caches
            if house in self.sf_fin_houses:
                continue
            if self.sf_elim_keys[house]:
                house_keys -= self.sf_elim_keys[house]

            

class SimpleXWings(CachingAlgorithm):
    """Search for simple x-wing patterns. Not finned, sashimi, or mutant; just
    regular old x-wings.
    """
    def __init__(self, **kwargs):
        self.simple_xwings_cache = set()
        super().__init__(**kwargs)
        self.cache_list.append(self.simple_xwings_cache)

    def nextmove(self):
        # TODO: clean this method up; i can barely follow this and i just wrote it
        for rectangle in self.state.find_rectangles():
            if rectangle in self.simple_xwings_cache:
                continue
            cands, rect_keys = rectangle

            # k1: upper left
            # k2: upper right
            # k3: lower right
            # k4: lower left
            k1, k2, k3, k4 = rect_keys
            col1_cands = self.state.candidates_from_house(k1[1] + 9)
            col2_cands = self.state.candidates_from_house(k3[1] + 9)
            row1_cands = self.state.candidates_from_house(k1[0] + 18)
            row2_cands = self.state.candidates_from_house(k3[0] + 18)

            for cand in cands:
                cs = CandidateSet(cand)
                if (cs, rect_keys) in self.simple_xwings_cache:
                    continue
                elim = None
                if col1_cands[cand] == 2 and col2_cands[cand] == 2:
                    if row1_cands[cand] > 2 or row2_cands[cand] > 2:
                        elim = (set(
                            self.state.candidate_in_keyset(cand, self.state.houses[k1[0] + 18])) |
                                set(
                            self.state.candidate_in_keyset(cand, self.state.houses[k3[0] + 18]))
                               ) - set(rect_keys)
                if row1_cands[cand] == 2 and row2_cands[cand] == 2:
                    if col1_cands[cand] > 2 and col2_cands[cand] > 2:
                        elim = (set(
                            self.state.candidate_in_keyset(cand, self.state.houses[k1[1] + 9])) |
                                set(
                            self.state.candidate_in_keyset(cand, self.state.houses[k3[1] + 9]))
                               ) - set(rect_keys)
                if elim:
                    self.cache_on_apply = self.simple_xwings_cache
                    self.cache_on_apply_value = (cs, rect_keys)
                    return XWingMove(
                        self.state, digit=cand, fish=rect_keys,
                        change={key: cs for key in elim}
                    )
                self.simple_xwings_cache.add((cs, rect_keys))
        return super().nextmove()

class BUGPlusOne(Algorithm):
    """BUG stands for Binary Universal Grave (sometimes *Bivalue* Universal Grave).
    This is the name of a pattern that occurs when every remaining cell has
    two possible candidates. If this pattern occurs, then the puzzle is
    ambiguous. This algorithm looks for a situation where all cells are bivalue
    except for one. Then we can make conclusions about that last cell.

    This algorithm is not logically valid if there are hidden singles that can be
    found. As such, it should always come after HiddenSingles in a solver mro.
    """
    def nextmove(self):
        keys_with_2_candidates = set(self.state.order_exactly_n(2))
        difference = self.state.num_remaining - len(keys_with_2_candidates)
        if difference == 1:
            key = next(iter(set(self.state.order_simple()) - keys_with_2_candidates))
            noelim = CandidateSet()
            cands = self.state.candidates[key]
            for cand in cands:
                if any(n > 2 for n in self.state.candidate_in_houses(key, cand)):
                    noelim |= CandidateSet(cand)
            return BUGMove(self.state, change={key: cands - noelim})
        if difference == 0:
            raise ContradictionError('Binary universal grave')
        return super().nextmove()

class UniqueRectangles(Algorithm):
    """It is impossible to have four naked pairs arranged in a rectangle which are
    contained by only two groups. If we assume that the puzzle has a unique solution,
    we may be able to eliminate candidates to avoid this pattern.
    """
    def nextmove(self):
        for cs,rectangle in self.state.find_rectangles():
            # need at least one key with only two candidates.
            if len(cs) != 2:
                continue
            # For unique rectangle techniques to apply, all four keys must be
            # contained in only two groups.
            groups = set()
            for key in rectangle:
                groups.add(id(self.state.grconfig[key]))
            if len(groups) > 2:
                continue

            # Ok, we found a potential rectangle. We need to check for various
            # conditions that will allow us to eliminate candidates.
            #k1, k2, k3, k4 = rectangle

            # Type 1: Only one key with more than two candidates
            count = 0
            for i,key in enumerate(rectangle):
                if len(self.state.candidates[key]) > 2:
                    found = rectangle[i]
                    count += 1
            if count == 0:
                # if we assume that the puzzle has a unique solution, we know
                # we've made a bad guess. So we can bail out of this search branch.
                raise ContradictionError('Ambiguous rectangle')
            if count == 1:   # Type 1 found
                return UniqueRectangleMove(
                    self.state, mark=1, rectangle=rectangle, 
                    pair=tuple(x+1 for x in cs), change={found: cs}
                )

            # Type 2:
            if count == 2:
                pass
        return super().nextmove()

class BasicGuesser(Algorithm):
    """Makes guesses and backtracks if the guess turns out to wrong.
    Keeps a stack of moves that would need to be undone during a backtrack.
    """
    def __init__(self, **kwargs):
        self.btstack = []
        super().__init__(**kwargs)

    def apply(self, move):
        """We need to push the move onto the stack if it's a guess or
        if the stack is non-empty, but not if the move is a backtrack,
        because undoing a backtrack move redoes the moves that were
        backtracked.
        """
        super().apply(move)
        if (self.btstack and not isinstance(move, Backtrack))\
                or isinstance(move, Guess):
            self.btstack.append(move)

    def backtrack(self, why):
        """Create a backtrack move, which backtracks to the previous guess."""
        if hasattr(self, 'cache_list'):
            self.clear_caches()
        bt = Backtrack(self.state, stack=self.btstack, why=why)
        self.btstack = bt.stack
        return bt

    def makeguess(self, key, digit, cands, guess):
        """Make a guess. The key is the location of the cell that we want to guess
        for, the digit is the specific guess, cands is the set of candidates for
        this key, and guess is the guess class to use.
        """
        remaining = len(cands) - 1
        return guess(self.state, key=key, digit=digit, remaining=remaining)

class Sledgehammer(BasicGuesser):
    """This is the naive algorithm for generating guesses. We choose a cell with
    the smalest number of candidates, then we choose the lowest digit in the
    candidate set.
    """
    def nextmove(self):
        key = next(self.state.order_by_num_candidates())
        cands = self.state.candidates[key]
        return self.makeguess(key, next(iter(cands)), cands, SimpleGuess)

class Random(BasicGuesser):
    """Make up guesses randomly. This is used to generate terminal patterns in
    the first step of creating a new puzzle from scratch.
    """
    def nextmove(self):
        key = next(self.state.order_random())
        cands = list(self.state.candidates[key])
        return self.makeguess(key, cands[randint(0,len(cands)-1)], cands, RandomGuess)
