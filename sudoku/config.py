"""
Utility functions used for initialization of the State object.
"""

import gmpy2
from itertools import chain

def default_keylists(*args, **kwargs):
    """Default arg for build_config"""
    size = kwargs.pop('size', None)
    if size is None:
        size = 9
    assert gmpy2.is_square(size), "size must be a square number"
    groupwidth = int(gmpy2.sqrt(size))
    k1 = lambda i, j: i // groupwidth + (j // groupwidth) * groupwidth
    k2 = lambda i, j: i %  groupwidth + (j %  groupwidth) * groupwidth
    return [
        [(k1(i,j),k2(i,j)) for i in range(size)]
            for j in range(size)
    ]

def build_config(*args, keylistfunc=default_keylists, **kwargs):
    """Create a group configuration for a square sudoku puzzle.
    A config is a dict where each key maps to a list of keys-- the ones that
    belong to the same group. Each key maps to a list that contains itself.

    The second argument is a function that creates a list of lists,
    each list containing size keys.
    """
    grid = {}
    keylists = keylistfunc(*args, **kwargs)
    for keylist in keylists:
        for key in keylist:
            grid[key] = keylist
    return grid

def calculate_peers(grconfig):
    """Calculate the peers of each cell based on a grconfig. A peer of a cell
    is a cell in the same row, column, or group. The peers attribute is a
    3-tuple of sets containing the peers in the row, the peers in the column,
    and the peers in the group in that order.
    """
    peers = {}
    for key in grconfig:
        keysets = ({n for n in grconfig[key]},
                   {(n,key[1]) for n in range(9)},
                   {(key[0],n) for n in range(9)})
        for keyset in keysets:
            keyset.remove(key)
        peers[key] = (frozenset(keysets[0]),
                      frozenset(keysets[1]),
                      frozenset(keysets[2]))
    return peers

def calculate_subgroups(peers):
    """Calculate the subgroups based on the peers.
    A row subgroup is the intersection of a row with a group. Ditto columns.
    In each grid, each key maps to a 3-tuple of sets; the subgroup keys,
    the peers from the row(column) not in the subgroup, and the group peers
    not in the subgroup.
    """
    rows, cols = {}, {}
    for key in peers:
        group,col,row = peers[key]

        # Calculate row subgroups
        if key not in rows:
            subgroup = frozenset(k for k in group if k in row) | {key}
            subgroup_tuple = subgroup, row - subgroup, group - subgroup
            for _key in subgroup:
                rows[_key] = subgroup_tuple

        # The column subgroup calculation works the same
        if key not in cols:
            subgroup = frozenset(k for k in group if k in col) | {key}
            subgroup_tuple = subgroup, col - subgroup, group - subgroup
            for _key in subgroup:
                cols[_key] = subgroup_tuple

    return rows, cols

def calculate_housekeys(grconfig):
    """Calculate the rows, cols, and houses attributes."""
    rows = tuple(tuple((i,j) for j in range(9)) for i in range(9))
    cols = tuple(tuple((i,j) for i in range(9)) for j in range(9))
    group_list = []
    for i in range(9):
        for j in range(9):
            key = i,j
            keys = grconfig[key]
            if keys not in group_list:
                group_list.append(keys)
    houses = tuple(chain((n for n in group_list), cols, rows))
    return rows, cols, houses

def calculate_oneset(peers):
    """Items from the peers dict are tuples containing three sets. This dictionary
    is a shortcut to get a peers item as a single set.
    """
    return {k: v[0]|v[1]|v[2] for k,v in peers.items()}
