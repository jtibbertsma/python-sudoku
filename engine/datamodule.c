#include "Python.h"
#include "structmember.h"

PyDoc_STRVAR(module_doc,
"This module contains the implementation of the sudoku state object,\n\
which is responsible for keeping track of the puzzle state information\n\
such as solved keys and candidates for unsolved keys.\n\
\n\
This module also defines several helper types. Of these, only the\n\
CandidateSet type can be imported. The other three helper types\n\
cannot be imported but are returned by methods or attributes\n\
of the State object. One is an iterator type which can iterate\n\
through the state's keys in various orders, and there are two\n\
mappings, candidates and clues, which are used to access information\n\
from the State object.");


/*[clinic input]
module data
class data.State "SudokuStateObject *" "&SudokuState_Type"
class data.CandidateSet "CandidateSetObject *" "&CandidateSet_Type"
class data.state_candidates "SudokuMapObject *" "&state_candidates_Type"
class data.state_clues "SudokuMapObject *" "&state_clues_Type"
class data.state_iterator "KeyIterObject *" "&state_iterator_Type"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=8c27dca51b54bba5]*/

/*static int num_allocs;
static int num_deallocs;*/

/* global variables and macro definitions */

/* config module; contains python functions used in the initialization function */
static PyObject *config_module;

/* Our custom exception indicating contradiction */
static PyObject *ContradictionError;

/* Default state attributes */
static PyObject *default_grconfig;
static PyObject *default_peers;
static PyObject *default_subgroups;
static PyObject *default_housekeys;
static PyObject *default_oneset;

/* Variable size puzzles are currently unsupported. */
#define NUMROWS  9
#define GRIDSIZE 81

/* Mask for candidate set */
#define TERMS    0x01FF

#define ERRORBIT 0x8000

/* Maximum number of rectangles in the grid; depends on NUMROWS */
#define MAXRECT  1296

/* Macros to switch between 1 and 2 dimensional array coordinates */
#define INDEX(x, y) \
    (((x) * NUMROWS) + (y))
#define ROW(i) \
    ((i) / NUMROWS)
#define COL(i) \
    ((i) % NUMROWS)

/* Macros for accessing cell_info struct. */

/* Set a bit in a number. (set is usually a uint16_t). */
#define SET_BIT(set, bit) \
    (set |= (1 << (bit)))

/* Value aka clue for a cell. */
#define CELL_VALUE(grid, x, y) \
    ((grid)[INDEX((x),(y))].ci_value)

/* Candidate set for a cell. */
#define CELL_CANDS(grid, x, y) \
    ((grid)[INDEX((x),(y))].ci_candidates)

/* Test if a cell is solved. */
#define CELL_FILLED(grid, x, y) \
    ((!(CELL_VALUE((grid),(x),(y)) & ERRORBIT)))

/* Get group house_info struct from a cell. */
#define CELL_GROUP(state, x, y) \
    ((state)->ss_houses[(state)->ss_grid[INDEX((x),(y))].ci_group])

/* Using the macros below to parse key tuples instead of PyArg_ParseTuple
 * is significantly faster. Using this is also good for catching errors
 * involving bad keys.
 *
 * key is supposed to be a tuple with 2 ints. error_stmt is a statement to
 * be performed on error, such as 'return NULL' or 'goto error'. Coordinates
 * are stored in variables x and y, which are declared automatically.
 */
#define UNPACK_KEY_NO_DECL(key, error_stmt, name)                       \
    do {                                                                \
        if (!PyTuple_Check(key) ||                                      \
            PyTuple_GET_SIZE(key) != 2) {                               \
            PyErr_Format(PyExc_TypeError,                               \
                "%s: Invalid key, expected tuple, not '%.100s",         \
                name, Py_TYPE(key)->tp_name);                           \
            error_stmt;                                                 \
        }                                                               \
        _x = PyTuple_GET_ITEM(key, 0);                                  \
        _y = PyTuple_GET_ITEM(key, 1);                                  \
        x = PyLong_AsSsize_t(_x);                                       \
        y = PyLong_AsSsize_t(_y);                                       \
        if (PyErr_Occurred()) {                                         \
            error_stmt;                                                 \
        }                                                               \
        if (x >= NUMROWS || y >= NUMROWS || x < 0 || y < 0) {           \
            PyErr_Format(PyExc_TypeError,                               \
                "%s: Invalid key: (%ld, %ld)", name, x, y);             \
            error_stmt;                                                 \
        }                                                               \
    } while(0)

#define UNPACK_KEY(key, error_stmt, name)                               \
    Py_ssize_t x, y;                                                    \
    PyObject *_x, *_y;                                                  \
    UNPACK_KEY_NO_DECL(key, error_stmt, name)


/* CandidateSet implementation. This object is immutable. Since a candidate
 * set can contain only 9 binary values, there are 512 possibilities.
 */

static PyTypeObject CandidateSet_Type;

#define CandidateSet_Check(set) \
    (Py_TYPE(set) == &CandidateSet_Type)

/* Count the number of ones in the binary representation of an int. */
static int
count_ones(int u)
{
   /* This implementation comes from stack overflow;
    * I don't claim to be clever enough to come up with this.
    * Supposedly it comes from MIT graduate students or something.
    */
   uint32_t uCount = u
                       - ((u >> 1)  & 033333333333)
                       - ((u >> 2)  & 011111111111);
   return ((uCount + (uCount >> 3)) & 030707070707) % 63;
}

typedef struct {
    PyObject_HEAD
    Py_ssize_t cs_iterpos;  /* iteration placeholder */
    uint16_t cs_set;        /* data */
} CandidateSetObject;

/* Interned sizes for each CandidateSet. */
static Py_ssize_t isizes[512];

static PyObject *
build_set(uint16_t set)
{
    CandidateSetObject *self;

    self = PyObject_New(CandidateSetObject, &CandidateSet_Type);
    if (!self)
        return NULL;
    self->cs_set = set & TERMS;
    self->cs_iterpos = 0;

    return (PyObject *)self;
}

/* Public constructor */
PyDoc_STRVAR(data_CandidateSet___new___doc,
"CandidateSet(*bits, /)\n"
"--\n"
"\n"
"Represents a set of candidates in a cell in a sudoku grid.\n"
"\n"
"Each argument to the constructor is an element in the new set;\n"
"this doesn't accept arbitrary iterators like normal Python sets.");

static PyObject *
data_CandidateSet___new__(PyTypeObject *cls, PyObject *args, PyObject *kwargs)
{
    Py_ssize_t bit, i, num_args;
    uint16_t set = 0;

    if (!_PyArg_NoKeywords("CandidateSet", kwargs))
        return NULL;
    num_args = PyTuple_Size(args);
    if (num_args < 0)
        return NULL;

    for (i = 0; i < num_args; i++) {
        bit = PyLong_AsSsize_t(PyTuple_GET_ITEM(args, i));
        if (bit >= 9 || bit < 0) {
            if (!PyErr_Occurred())
                PyErr_Format(PyExc_TypeError,
                    "CandidateSet constructor expects "
                    "positive ints < 9, not '%d'", bit);
            return NULL;
        }
        SET_BIT(set,bit);
    }

    return build_set(set);
}

static PyObject *
data_CandidateSet___repr__(CandidateSetObject *self)
{
    char *format = "< %c   %c   %c   %c   %c   %c   %c   %c   %c >";
    char data[9];
    Py_ssize_t i;
    uint16_t set = self->cs_set;

    for (i = 0; i < 9; i++) {
        if (set & (1 << i)) {
            data[i] = i + '1';
        } else {
            data[i] = '.';
        }
    }

    return PyUnicode_FromFormat(format,
        data[0], data[1], data[2],
        data[3], data[4], data[5],
        data[6], data[7], data[8]
    );
}

static Py_hash_t
data_CandidateSet___hash__(CandidateSetObject *self)
{
    return (Py_hash_t)self->cs_set;
}

#define CHECK_BINOP(v,w)                                      \
    do {                                                      \
        if (!CandidateSet_Check(v) || !CandidateSet_Check(w)) \
            Py_RETURN_NOTIMPLEMENTED;                         \
    } while (0)

static PyObject *
cs_num_subtract(CandidateSetObject *a, CandidateSetObject *b)
{
    CHECK_BINOP(a,b);
    return build_set(a->cs_set & ~b->cs_set);
}

static int
cs_num_bool(CandidateSetObject *a)
{
    return (int)a->cs_set;
}

static PyObject *
cs_num_invert(CandidateSetObject *a)
{
    return build_set(~a->cs_set);
}

static PyObject *
cs_num_and(CandidateSetObject *a, CandidateSetObject *b)
{
    CHECK_BINOP(a,b);
    return build_set(a->cs_set & b->cs_set);
}

static PyObject *
cs_num_xor(CandidateSetObject *a, CandidateSetObject *b)
{
    CHECK_BINOP(a,b);
    return build_set(a->cs_set ^ b->cs_set);
}

static PyObject *
cs_num_or(CandidateSetObject *a, CandidateSetObject *b)
{
    CHECK_BINOP(a,b);
    return build_set(a->cs_set | b->cs_set);
}

static PyObject *
cs_num_int(CandidateSetObject *a)
{
    return PyLong_FromLong((long)a->cs_set);
}

static PyNumberMethods CandidateSet_as_number = {
    0,                            /* nb_add */
    (binaryfunc)cs_num_subtract,  /* nb_subtract */
    0,                            /* nb_multiply */
    0,                            /* nb_remainder */
    0,                            /* nb_divmod */
    0,                            /* nb_power */
    0,                            /* nb_negative */
    0,                            /* nb_positive */
    0,                            /* nb_absolute */
    (inquiry)cs_num_bool,         /* nb_bool */
    (unaryfunc)cs_num_invert,     /* nb_invert */
    0,                            /* nb_lshift */
    0,                            /* nb_rshift */
    (binaryfunc)cs_num_and,       /* nb_and */
    (binaryfunc)cs_num_xor,       /* nb_xor */
    (binaryfunc)cs_num_or,        /* nb_or */
    (unaryfunc)cs_num_int,        /* nb_int */
};

static Py_ssize_t
cs_seq_length(CandidateSetObject *self)
{
    return isizes[(Py_ssize_t)self->cs_set];
}

static int
cs_seq_contains(CandidateSetObject *self, PyObject *item)
{
    if (!PyLong_Check(item))
        return 0;

    Py_ssize_t bit = PyLong_AsSsize_t(item);
    if (PyErr_Occurred())
        return -1;
    if (bit >= 9)
        return 0;

    return self->cs_set & (1 << bit);
}

static PySequenceMethods CandidateSet_as_sequence = {
    (lenfunc)cs_seq_length,      /* sq_length */
    0,                           /* sq_concat */
    0,                           /* sq_repeat */
    0,                           /* sq_item */
    0,                           /* sq_slice */
    0,                           /* sq_ass_item */
    0,                           /* sq_ass_slice */
    (objobjproc)cs_seq_contains, /* sq_contains */
    0,                           /* sq_inplace_concat */
    0,                           /* sq_inplace_repeat */
};

#define TEST_COND(test) \
    ((test) ? Py_True : Py_False)

/* less than means subset */
static PyObject *
data_CandidateSet_richcompare(CandidateSetObject *a,
                              CandidateSetObject *b, int op)
{
    PyObject *v;
    int i, j;

    CHECK_BINOP(a,b);

    switch (op) {
    case Py_EQ:
        v = TEST_COND(a->cs_set == b->cs_set);
        break;
    case Py_NE:
        v = TEST_COND(a->cs_set != b->cs_set);
        break;
    case Py_LE:
        v = TEST_COND((a->cs_set | b->cs_set) == b->cs_set);
        break;
    case Py_GE:
        v = TEST_COND((a->cs_set | b->cs_set) == a->cs_set);
        break;
    case Py_LT:
        i = isizes[(Py_ssize_t)a->cs_set];
        j = isizes[(Py_ssize_t)b->cs_set];
        v = TEST_COND((a->cs_set | b->cs_set) == b->cs_set && i < j);
        break;
    case Py_GT:
        i = isizes[(Py_ssize_t)a->cs_set];
        j = isizes[(Py_ssize_t)b->cs_set];
        v = TEST_COND((a->cs_set | b->cs_set) == a->cs_set && i > j);
        break;
    default:
        PyErr_BadArgument();
        return NULL;
    }

    Py_INCREF(v);
    return v;
}

static PyObject *
data_CandidateSet_iter(CandidateSetObject *self)
{
    /* CandidateSets are their own iterators, but don't iterate over
     * this one. It wouldn't work if we tried to iterate over the same
     * set in multiple threads. So make a fresh one.
     */
    return build_set(self->cs_set);
}

static PyObject *
data_CandidateSet_iternext(CandidateSetObject *self)
{
    for (; self->cs_iterpos < 9; self->cs_iterpos++) {
        if (self->cs_set & (1 << self->cs_iterpos)) {
            return PyLong_FromSsize_t(self->cs_iterpos++);
        }
    }

    return NULL;
}

/*[clinic input]
data.CandidateSet.__getstate__

Pickle support for CandidateSet.

Returns a tuple containing two ints. The first is converted into
bitwise set information, and the second is used as the iteration
position.
[clinic start generated code]*/

PyDoc_STRVAR(data_CandidateSet___getstate____doc__,
"__getstate__($self, /)\n"
"--\n"
"\n"
"Pickle support for CandidateSet.\n"
"\n"
"Returns a tuple containing two ints. The first is converted into\n"
"bitwise set information, and the second is used as the iteration\n"
"position.");

#define DATA_CANDIDATESET___GETSTATE___METHODDEF    \
    {"__getstate__", (PyCFunction)data_CandidateSet___getstate__, METH_NOARGS, data_CandidateSet___getstate____doc__},

static PyObject *
data_CandidateSet___getstate___impl(CandidateSetObject *self);

static PyObject *
data_CandidateSet___getstate__(CandidateSetObject *self, PyObject *Py_UNUSED(ignored))
{
    return data_CandidateSet___getstate___impl(self);
}

static PyObject *
data_CandidateSet___getstate___impl(CandidateSetObject *self)
/*[clinic end generated code: output=8097811e317fcd17 input=3f9c6c1add00022c]*/
{
    return Py_BuildValue("(nn)", (Py_ssize_t)self->cs_set, self->cs_iterpos);
}

/*[clinic input]
data.CandidateSet.__setstate__

    state: object
        This should be a tuple containing two ints.
    /

Unpickle a CandidateSet.
[clinic start generated code]*/

PyDoc_STRVAR(data_CandidateSet___setstate____doc__,
"__setstate__($self, state, /)\n"
"--\n"
"\n"
"Unpickle a CandidateSet.\n"
"\n"
"  state\n"
"    This should be a tuple containing two ints.");

#define DATA_CANDIDATESET___SETSTATE___METHODDEF    \
    {"__setstate__", (PyCFunction)data_CandidateSet___setstate__, METH_O, data_CandidateSet___setstate____doc__},

static PyObject *
data_CandidateSet___setstate__(CandidateSetObject *self, PyObject *state)
/*[clinic end generated code: output=d43bd51c59ce8736 input=d78edfbbf56a8790]*/
{
    Py_ssize_t x, y;

    if (PyArg_ParseTuple(state, "nn", &x, &y) < 0)
        return NULL;
    if (x >= NUMROWS || x < 0 || y < 0) {
        PyErr_Format(PyExc_ValueError,
            "__setstate__: Bad values (%ld, %ld)", x, y);
        return NULL;
    }
    self->cs_iterpos = y;
    self->cs_set = (uint16_t)x;
    
    Py_RETURN_NONE;
}

static PyMethodDef CandidateSet_methods[] = {
    DATA_CANDIDATESET___GETSTATE___METHODDEF
    DATA_CANDIDATESET___SETSTATE___METHODDEF
    {NULL}  /*sentinel*/
};

static PyTypeObject CandidateSet_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "engine.data.CandidateSet", /*tp_name*/
    sizeof(CandidateSetObject), /*tp_basicsize*/
    0,                          /*tp_itemsize*/
    /* methods */
    0,                          /*tp_dealloc*/
    0,                          /*tp_print*/
    0,                          /*tp_getattr*/
    0,                          /*tp_setattr*/
    0,                          /*tp_reserved*/
    (reprfunc)data_CandidateSet___repr__,/*tp_repr*/
    &CandidateSet_as_number,    /*tp_as_number*/
    &CandidateSet_as_sequence,  /*tp_as_sequence*/
    0,                          /*tp_as_mapping*/
    (hashfunc)data_CandidateSet___hash__,/*tp_hash*/
    0,                          /*tp_call*/
    0,                          /*tp_str*/
    PyObject_GenericGetAttr,    /*tp_getattro*/
    0,                          /*tp_setattro*/
    0,                          /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,         /*tp_flags*/
    data_CandidateSet___new___doc,/*tp_doc*/
    0,                          /*tp_traverse*/
    0,                          /*tp_clear*/
    (richcmpfunc)data_CandidateSet_richcompare,/*tp_richcompare*/
    0,                          /*tp_weaklistoffset*/
    (getiterfunc)data_CandidateSet_iter,/*tp_iter*/
    (iternextfunc)data_CandidateSet_iternext,/*tp_iternext*/
    CandidateSet_methods,       /*tp_methods*/
    0,                          /*tp_members*/
    0,                          /*tp_getset*/
    0,                          /*tp_base*/
    0,                          /*tp_dict*/
    0,                          /*tp_descr_get*/
    0,                          /*tp_descr_set*/
    0,                          /*tp_dictoffset*/
    0,                          /*tp_init*/
    0,                          /*tp_alloc*/
    data_CandidateSet___new__,  /*tp_new*/
    PyObject_Del,               /*tp_free*/
    0,                          /*tp_is_gc*/
};

/* State declaration, initialization and various private functions */

static PyTypeObject SudokuState_Type;

/* struct to store information about each cell */
typedef struct {
   /* Py_ssize_t ci_id;     */     /* index of the struct in the grid array */
    Py_ssize_t ci_group;       /* offset into house_info array for this cell's group */
    uint16_t ci_value;         /* ERRORBIT is set if cell is unsolved. */
    uint16_t ci_candidates;    /* Bits 0-8 are set if that number is a candidate. */
    /*Py_ssize_t not_used_yet[36];*/
} cell_info;

/* offsets into ss_houses for each type of house */
#define ROWOFFSET (NUMROWS * 2)
#define COLOFFSET  NUMROWS
#define GROFFSET   0

/* store information for a house */
typedef struct {
    PyObject *hi_keyset;    /* borrowed reference to keyset from ss_grconfig.
                               Only has meaning for groups, not rows or columns. */
    Py_ssize_t hi_solved;   /* number of solved positions for the house */
    Py_ssize_t hi_cand_count[NUMROWS];  /* number of each candidate remaining */
} house_info;

typedef struct {
    PyObject_HEAD
    Py_ssize_t ss_solved;       /* number of solved positions */
    Py_ssize_t ss_digits[NUMROWS];/* Number of times each digit appears in the grid */
    PyObject *ss_grconfig;      /* dict */
    PyObject *ss_peers;         /* dict */
    PyObject *ss_subgroups;     /* tuple; (dict, dict) */
    PyObject *ss_movehook;      /* Move subclass object (or NULL) */
    PyObject *ss_weakref;       /* allow weak references */
    PyObject *ss_skeys;         /* set of solved keys */
    PyObject *ss_housekeys;     /* Keys in each house */
    PyObject *ss_oneset;        /* unions of peer sets */
    PyObject *ss_dict;          /* Support for dynamic attributes */
    house_info ss_houses[NUMROWS*3];    /* information for each house */
    cell_info ss_grid[GRIDSIZE];/* cell information */
} SudokuStateObject;

static void
data_State_dealloc(SudokuStateObject *self)
{
    PyObject_ClearWeakRefs((PyObject *)self);
    PyObject_GC_UnTrack(self);
    Py_CLEAR(self->ss_dict);
    Py_CLEAR(self->ss_skeys);
    Py_CLEAR(self->ss_grconfig);
    Py_CLEAR(self->ss_peers);
    Py_CLEAR(self->ss_subgroups);
    Py_CLEAR(self->ss_housekeys);
    Py_CLEAR(self->ss_oneset);
    Py_CLEAR(self->ss_movehook);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

#define State_CheckExact(s) \
    (Py_TYPE(s) == &SudokuState_Type)

static uint16_t
find_clues_in_keyset(cell_info *grid, PyObject *keyset)
{
    PyObject *key, *iter;
    uint16_t clues = 0;

    iter = PyObject_GetIter(keyset);
    if (!iter)
        goto done;

    while ((key = PyIter_Next(iter))) {
        UNPACK_KEY(key, goto done, "__init__");

        if (CELL_FILLED(grid, x, y)) {
            SET_BIT(clues, CELL_VALUE(grid, x, y));
        }
    }

done:
    Py_DECREF(iter);
    if (PyErr_Occurred())
        clues |= ERRORBIT;
    return clues;
}

static PyObject *
do_default_build_config(void)
{
    _Py_IDENTIFIER(build_config);
    PyObject *f, *args, *v = NULL;

    args = PyTuple_New(0);
    if (!args)
        return NULL;

    f = _PyDict_GetItemId(config_module, &PyId_build_config);
    if (!f) {
        PyErr_SetString(PyExc_AttributeError, "function not found");
        goto done;
    }
    v = PyObject_Call(f, args, NULL);

done:
    Py_DECREF(args);
    return v;
}

#define CALC_ATTR_FUNC(attr)                                            \
static PyObject *                                                       \
do_calculate_##attr(PyObject *var)                                      \
{                                                                       \
    _Py_IDENTIFIER(calculate_##attr);                                   \
    PyObject *f;                                                        \
                                                                        \
    f = _PyDict_GetItemId(config_module, &PyId_calculate_##attr);       \
    if (!f) {                                                           \
        PyErr_SetString(PyExc_AttributeError, "function not found");    \
        return NULL;                                                    \
    }                                                                   \
    return PyObject_CallFunction(f, "O", var);                          \
}

CALC_ATTR_FUNC(peers)
CALC_ATTR_FUNC(subgroups)
CALC_ATTR_FUNC(housekeys)
CALC_ATTR_FUNC(oneset)

static int
set_python_calculated_attrs(SudokuStateObject *self, PyObject *grconfig)
{
    PyObject *peers = NULL, *subgroups = NULL, *housekeys = NULL, *oneset = NULL;
    int same;

    /* If grconfig is the NULL pointer or is Py_None, use default values.
     * Otherwise, calculate values using python functions from config_module.
     */
    if (!grconfig || grconfig == Py_None)
        goto default_attrs;

    /* If a grconfig was passed in, check if it's equal to default */
    if ((same = PyObject_RichCompareBool(grconfig, default_grconfig, Py_EQ)) < 0)
        return -1;
    if (same)
        goto default_attrs;

    peers = do_calculate_peers(grconfig);
    if (!peers)
        goto fail;

    subgroups = do_calculate_subgroups(peers);
    if (!subgroups)
        goto fail;

    housekeys = do_calculate_housekeys(grconfig);
    if (!housekeys)
        goto fail;

    oneset = do_calculate_oneset(peers);
    if (!oneset)
        goto fail;

    self->ss_grconfig  = grconfig;
    self->ss_peers     = peers;
    self->ss_subgroups = subgroups;
    self->ss_housekeys = housekeys;
    self->ss_oneset    = oneset;

    Py_INCREF(self->ss_grconfig);
    return 0;

default_attrs:
    self->ss_grconfig  = default_grconfig;
    self->ss_peers     = default_peers;
    self->ss_subgroups = default_subgroups;
    self->ss_housekeys = default_housekeys;
    self->ss_oneset    = default_oneset;

    Py_INCREF(self->ss_grconfig);
    Py_INCREF(self->ss_peers);
    Py_INCREF(self->ss_subgroups);
    Py_INCREF(self->ss_housekeys);
    Py_INCREF(self->ss_oneset);

    return 0;

fail:
    Py_XDECREF(peers);
    Py_XDECREF(subgroups);
    Py_XDECREF(housekeys);
    Py_XDECREF(oneset);
    return -1;
}

/* init cell_info structs */
static int
set_defaults(cell_info *grid)
{
    Py_ssize_t i;

    for (i = 0; i < GRIDSIZE; i++) {
       /* grid[i].ci_id = i;*/
        grid[i].ci_value = ERRORBIT;
        grid[i].ci_candidates = 0;
    }

    return 0;
}

/* Set location of the group in the houses array for each cell.
 * Also set the pointer to the keyset in the house_info struct
 * for each group.
 */
static int
set_groups_in_cells(cell_info *grid, house_info *houses, PyObject *grconfig)
{
    Py_ssize_t found = 0, i, j, k;
    PyObject *found_vals[NUMROWS];
    PyObject *key, *value;
    int seen;

    for (i = 0; i < NUMROWS; i++) {
        for (j = 0; j < NUMROWS; j++) {
            key = Py_BuildValue("(nn)", i, j);
            if (!key)
                return -1;
            value = PyDict_GetItem(grconfig, key);
            if (!value) {
                if (!PyErr_Occurred())
                    _PyErr_SetKeyError(key);
                Py_DECREF(key);
                return -1;
            }
            seen = 0;
            for (k = 0; k < found; k++) {
                if (value == found_vals[k]) {
                    seen = 1;
                    break;
                }
            }
            grid[INDEX(i,j)].ci_group = k + GROFFSET;
            if (!seen)
                found_vals[found++] = value;
            Py_DECREF(key);
        }
    }

    for (i = 0; i < NUMROWS; i++) {
        houses[i+GROFFSET].hi_keyset = found_vals[i];
    }

    return 0;
}

/* utility functions to incref or decref hi_solved for a given cell. */
static void
house_adjust_solved_up(SudokuStateObject *self, Py_ssize_t x, Py_ssize_t y)
{
    self->ss_houses[x+ROWOFFSET].hi_solved++;
    self->ss_houses[y+COLOFFSET].hi_solved++;
    CELL_GROUP(self, x, y).hi_solved++;
}

static void
house_adjust_solved_down(SudokuStateObject *self, Py_ssize_t x, Py_ssize_t y)
{
    self->ss_houses[x+ROWOFFSET].hi_solved--;
    self->ss_houses[y+COLOFFSET].hi_solved--;
    CELL_GROUP(self, x, y).hi_solved--;
}

/* similar functions for cand_count, but do adjustment for each item in the set */

static void
house_adjust_cand_count_up(SudokuStateObject *self, Py_ssize_t x, Py_ssize_t y, uint16_t set)
{
    Py_ssize_t i;

    for (i = 0; i < NUMROWS; i++) {
        if (set & (1 << i)) {
            self->ss_houses[x+ROWOFFSET].hi_cand_count[i]++;
            self->ss_houses[y+COLOFFSET].hi_cand_count[i]++;
            CELL_GROUP(self, x, y).hi_cand_count[i]++;
        }
    }
}

static void
house_adjust_cand_count_down(SudokuStateObject *self, Py_ssize_t x, Py_ssize_t y, uint16_t set)
{
    Py_ssize_t i;

    for (i = 0; i < NUMROWS; i++) {
        if (set & (1 << i)) {
            self->ss_houses[x+ROWOFFSET].hi_cand_count[i]--;
            self->ss_houses[y+COLOFFSET].hi_cand_count[i]--;
            CELL_GROUP(self, x, y).hi_cand_count[i]--;
        }
    }
}

/* fill in pencil marks based on the clues in the grid */
static int
fill_in_pencilmarks(SudokuStateObject *self)
{
    Py_ssize_t n, m, z;
    PyObject *key, *value;

    for (n = 0; n < NUMROWS*3; n++)
        memset(self->ss_houses[n].hi_cand_count, 0, sizeof(Py_ssize_t));

    for (n = 0; n < NUMROWS; n++) {
        for (m = 0; m < NUMROWS; m++) {
            uint16_t notcandidates = 0, q;
            PyObject *keyset;

            if (CELL_FILLED(self->ss_grid, n, m))
                continue;

            /* look at peers in row */
            for (z = 0; z < NUMROWS; z++) {
                if (z == m || !CELL_FILLED(self->ss_grid, n, z))
                    continue;
                SET_BIT(notcandidates, CELL_VALUE(self->ss_grid, n, z));
            }

            /* look at peers in column */
            for (z = 0; z < NUMROWS; z++) {
                if (z == n || !CELL_FILLED(self->ss_grid, z, m))
                    continue;
                SET_BIT(notcandidates, CELL_VALUE(self->ss_grid, z, m));
            }

            /* look at peers in the current group */
            key = Py_BuildValue("(nn)", n, m);
            if (!key)
                return -1;
            value = PyDict_GetItem(self->ss_peers, key);
            if (!value) {
                if (!PyErr_Occurred())
                    _PyErr_SetKeyError(key);
                Py_DECREF(key);
                return -1;
            }
            keyset = PyTuple_GetItem(value, 0);
            if (!keyset) {
                Py_DECREF(key);
                return -1;
            }

            q = find_clues_in_keyset(self->ss_grid, keyset);
            if (q & ERRORBIT) {
                Py_DECREF(key);
                return -1;
            }
            notcandidates |= q;

            CELL_CANDS(self->ss_grid, n, m) = (~notcandidates) & TERMS;

            /* adjust houses */
            house_adjust_cand_count_up(self, n, m, CELL_CANDS(self->ss_grid, n, m));
            Py_DECREF(key);
        }
    }

    return 0;
}

/*[clinic input]
data.State.__init__

    clues: object
        A dictionary that maps positions in the sudoku grid to numbers
        in range(9).

    dofill: bool = True
        If true, then fill in pencilmarks. If false, then each candidate set
        will be empty.

    grconfig: object = NULL
        A dictionary that maps each cell to a list of keys. If None, a default
        value is used.

This is the representation of the information in a sudoku puzzle.
[clinic start generated code]*/

PyDoc_STRVAR(data_State___init____doc__,
"State(clues, dofill=True, grconfig=None)\n"
"--\n"
"\n"
"This is the representation of the information in a sudoku puzzle.\n"
"\n"
"  clues\n"
"    A dictionary that maps positions in the sudoku grid to numbers\n"
"    in range(9).\n"
"  dofill\n"
"    If true, then fill in pencilmarks. If false, then each candidate set\n"
"    will be empty.\n"
"  grconfig\n"
"    A dictionary that maps each cell to a list of keys. If None, a default\n"
"    value is used.");

static int
data_State___init___impl(SudokuStateObject *self, PyObject *clues, int dofill, PyObject *grconfig);

static int
data_State___init__(PyObject *self, PyObject *args, PyObject *kwargs)
{
    int return_value = -1;
    static char *_keywords[] = {"clues", "dofill", "grconfig", NULL};
    PyObject *clues;
    int dofill = 1;
    PyObject *grconfig = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs,
        "O|pO:State", _keywords,
        &clues, &dofill, &grconfig))
        goto exit;
    return_value = data_State___init___impl((SudokuStateObject *)self, clues, dofill, grconfig);

exit:
    return return_value;
}

static int
data_State___init___impl(SudokuStateObject *self, PyObject *clues, int dofill, PyObject *grconfig)
/*[clinic end generated code: output=cb6333ae454ac383 input=6f62a2a9cc95d39e]*/
{
    Py_ssize_t i = 0, cl;
    PyObject *key, *value;

    /* Since __init__ can be used to reset an object, we explicitly zero
     * out all fields, except for dynamic attributes and weak references,
     * which are left alone.
     */
    Py_CLEAR(self->ss_grconfig);
    Py_CLEAR(self->ss_peers);
    Py_CLEAR(self->ss_subgroups);
    Py_CLEAR(self->ss_movehook);
    Py_CLEAR(self->ss_skeys);
    self->ss_solved = 0;
    if (set_defaults(self->ss_grid) < 0)
        return -1;
    memset(self->ss_houses, 0, sizeof(house_info)*NUMROWS*3);

    /* Set various attributes */
    if (set_python_calculated_attrs(self, grconfig) < 0)
        return -1;
    if ((self->ss_skeys = PySet_New(NULL)) == NULL)
        return -1;
    if (set_groups_in_cells(self->ss_grid, self->ss_houses, self->ss_grconfig) < 0)
        return -1;

    /* Put givens in the grid */
    while (PyDict_Next(clues, &i, &key, &value)) {
        cl = PyLong_AsSsize_t(value);
        if (cl < 0 || cl > NUMROWS) {
            if (!PyErr_Occurred())
                PyErr_Format(PyExc_ValueError,
                    "__init__: Expected values from 0-%d, saw %d",
                    NUMROWS, cl);
            return -1;
        }

        if (PySet_Add(self->ss_skeys, key) < 0)
            return -1;
        UNPACK_KEY(key, return -1, "__init__");
        CELL_VALUE(self->ss_grid, x, y) = cl;

        /* Adjust houses */
        house_adjust_solved_up(self, x, y);

        /* Record clue number */
        self->ss_digits[cl]++;
    }

    /* Note the number of solved cells */
    if ((self->ss_solved = PyDict_Size(clues)) < 0)
        return -1;

    /* Calculate the candidates for the remaining positions */
    if (dofill && fill_in_pencilmarks(self) < 0)
        return -1;

    return 0;
}

/* State methods */

/*[clinic input]
data.State.candidate_in_keyset

    cand: Py_ssize_t
        Candidate to be searched for.

    keyset: object
        Set of keys to search for candidate in.
    /

Search through a keyset for a particular candidate.

Return a tuple containing each key where the candidate was found.
[clinic start generated code]*/

PyDoc_STRVAR(data_State_candidate_in_keyset__doc__,
"candidate_in_keyset($self, cand, keyset, /)\n"
"--\n"
"\n"
"Search through a keyset for a particular candidate.\n"
"\n"
"  cand\n"
"    Candidate to be searched for.\n"
"  keyset\n"
"    Set of keys to search for candidate in.\n"
"\n"
"Return a tuple containing each key where the candidate was found.");

#define DATA_STATE_CANDIDATE_IN_KEYSET_METHODDEF    \
    {"candidate_in_keyset", (PyCFunction)data_State_candidate_in_keyset, METH_VARARGS, data_State_candidate_in_keyset__doc__},

static PyObject *
data_State_candidate_in_keyset_impl(SudokuStateObject *self, Py_ssize_t cand, PyObject *keyset);

static PyObject *
data_State_candidate_in_keyset(SudokuStateObject *self, PyObject *args)
{
    PyObject *return_value = NULL;
    Py_ssize_t cand;
    PyObject *keyset;

    if (!PyArg_ParseTuple(args,
        "nO:candidate_in_keyset",
        &cand, &keyset))
        goto exit;
    return_value = data_State_candidate_in_keyset_impl(self, cand, keyset);

exit:
    return return_value;
}

static PyObject *
data_State_candidate_in_keyset_impl(SudokuStateObject *self, Py_ssize_t cand, PyObject *keyset)
/*[clinic end generated code: output=50ecdfeee9d24908 input=d9314af58c8b337c]*/
{
    PyObject *temp_storage[GRIDSIZE];
    Py_ssize_t found = 0, i;
    PyObject *key, *found_keys = NULL, *iter;

    iter = PyObject_GetIter(keyset);
    if (!iter)
        return NULL;

    while ((key = PyIter_Next(iter))) {
        UNPACK_KEY(key, goto done, "candidate_in_keyset");

        if (!CELL_FILLED(self->ss_grid, x, y)) {
            if (CELL_CANDS(self->ss_grid, x, y) & (1 << cand)) {
                temp_storage[found++] = key;
            }
        }
    }
    if (PyErr_Occurred())
        goto done;

    found_keys = PyTuple_New(found);
    if (!found_keys)
        goto done;

    for (i = 0; i < found; i++) {
        key = temp_storage[i];
        Py_INCREF(key);
        PyTuple_SET_ITEM(found_keys, i, key);
    }

done:
    Py_DECREF(iter);
    return found_keys;
}

/*[clinic input]
data.State.candidates_from_keyset

    keyset: object
        Set of keys to search for candidates.
    /

Get all candidates from a keyset.

Return the union of each candidate set for each of unsolved key in the
keyset. Ignore solved positions.
[clinic start generated code]*/

PyDoc_STRVAR(data_State_candidates_from_keyset__doc__,
"candidates_from_keyset($self, keyset, /)\n"
"--\n"
"\n"
"Get all candidates from a keyset.\n"
"\n"
"  keyset\n"
"    Set of keys to search for candidates.\n"
"\n"
"Return the union of each candidate set for each of unsolved key in the\n"
"keyset. Ignore solved positions.");

#define DATA_STATE_CANDIDATES_FROM_KEYSET_METHODDEF    \
    {"candidates_from_keyset", (PyCFunction)data_State_candidates_from_keyset, METH_O, data_State_candidates_from_keyset__doc__},

static PyObject *
data_State_candidates_from_keyset(SudokuStateObject *self, PyObject *keyset)
/*[clinic end generated code: output=2717161bd6408bba input=3017d3ede78897eb]*/
{
    uint16_t cands = 0;
    PyObject *key, *iter;

    iter = PyObject_GetIter(keyset);
    if (!iter)
        return NULL;

    while ((key = PyIter_Next(iter))) {
        UNPACK_KEY(key, goto done, "candidates_from_keyset");

        if (!CELL_FILLED(self->ss_grid, x, y)) {
            cands |= CELL_CANDS(self->ss_grid, x, y);
        }
    }

done:
    Py_DECREF(iter);
    if (PyErr_Occurred())
        return NULL;
    return build_set(cands);
}

/*[clinic input]
data.State.add_candidates

    change: object
        Dict that maps keys (tuples) to CandidateSet objects. Each
        CandidateSet represents the candidates to be added to its
        key.
    /

Add candidates to a collection of candidate sets.

Note that if this method ends up raising a KeyError, the grid will be in
disarray, so be careful that only unsolved keys are in the change
dictionary.
[clinic start generated code]*/

PyDoc_STRVAR(data_State_add_candidates__doc__,
"add_candidates($self, change, /)\n"
"--\n"
"\n"
"Add candidates to a collection of candidate sets.\n"
"\n"
"  change\n"
"    Dict that maps keys (tuples) to CandidateSet objects. Each\n"
"    CandidateSet represents the candidates to be added to its\n"
"    key.\n"
"\n"
"Note that if this method ends up raising a KeyError, the grid will be in\n"
"disarray, so be careful that only unsolved keys are in the change\n"
"dictionary.");

#define DATA_STATE_ADD_CANDIDATES_METHODDEF    \
    {"add_candidates", (PyCFunction)data_State_add_candidates, METH_O, data_State_add_candidates__doc__},

static PyObject *
data_State_add_candidates(SudokuStateObject *self, PyObject *change)
/*[clinic end generated code: output=c3da7bc1cbb2c92a input=ee5ed6b11b9848cb]*/
{
    PyObject *key, *value;
    Py_ssize_t i = 0;
    uint16_t add_set, old_set, intersection;

    if (!PyDict_Check(change)) {
        PyErr_Format(PyExc_TypeError,
            "add_candidates: Expected dict, not '%.100s'",
            Py_TYPE(change)->tp_name);
        return NULL;
    }

    while (PyDict_Next(change, &i, &key, &value)) {
        if (!CandidateSet_Check(value)) {
            PyErr_Format(PyExc_TypeError,
                "add_candidates: Expected values to be "
                "CandidateSets, not '%.100s'", Py_TYPE(value)->tp_name);
            return NULL;
        }

        UNPACK_KEY(key, return NULL, "add_candidates");
        if (CELL_FILLED(self->ss_grid, x, y)) {
            _PyErr_SetKeyError(key);
            return NULL;
        }
        add_set = ((CandidateSetObject *)value)->cs_set;
        old_set = CELL_CANDS(self->ss_grid, x, y);
        intersection = add_set & old_set;
        if (intersection) {
            /* Make sure that we don't mess up house cand counts if we're
               trying to add candidates that are already there. */
            house_adjust_cand_count_down(self, x, y, intersection);
        }

        house_adjust_cand_count_up(self, x, y, add_set);
        CELL_CANDS(self->ss_grid, x, y) |= add_set;
    }

    Py_RETURN_NONE;
}

/*[clinic input]
data.State.remove_candidates

    change: object
        Dict that maps keys (tuples) to CandidateSet objects.Each
        CandidateSet represents the candidates to be removed to its
        key.
    /

Remove candidates from a collection of candidate sets.

If a candidate set is left empty after removing the candidates,
this method raises a ContradictionError. If you want to have the empty
candidate set as the candidates for a cell, use set_candidates. Note
that if this raises a ContradictionError, it will have already removed
the candidates from the grid.

Note that if this method ends up raising a KeyError, the grid will be in
disarray, so be careful that only unsolved keys are in the change
dictionary.
[clinic start generated code]*/

PyDoc_STRVAR(data_State_remove_candidates__doc__,
"remove_candidates($self, change, /)\n"
"--\n"
"\n"
"Remove candidates from a collection of candidate sets.\n"
"\n"
"  change\n"
"    Dict that maps keys (tuples) to CandidateSet objects.Each\n"
"    CandidateSet represents the candidates to be removed to its\n"
"    key.\n"
"\n"
"If a candidate set is left empty after removing the candidates,\n"
"this method raises a ContradictionError. If you want to have the empty\n"
"candidate set as the candidates for a cell, use set_candidates. Note\n"
"that if this raises a ContradictionError, it will have already removed\n"
"the candidates from the grid.\n"
"\n"
"Note that if this method ends up raising a KeyError, the grid will be in\n"
"disarray, so be careful that only unsolved keys are in the change\n"
"dictionary.");

#define DATA_STATE_REMOVE_CANDIDATES_METHODDEF    \
    {"remove_candidates", (PyCFunction)data_State_remove_candidates, METH_O, data_State_remove_candidates__doc__},

static PyObject *
data_State_remove_candidates(SudokuStateObject *self, PyObject *change)
/*[clinic end generated code: output=d40a24c6f3a4c045 input=01afff6fcf51cde0]*/
{
    PyObject *key, *value;
    Py_ssize_t i = 0, rx, ry;
    uint16_t remove_set, old_set, not_subset;
    int raise = 0;

    if (!PyDict_Check(change)) {
        PyErr_Format(PyExc_TypeError,
            "remove_candidates: Expected dict, not '%.100s'",
            Py_TYPE(change)->tp_name);
        return NULL;
    }

    while (PyDict_Next(change, &i, &key, &value)) {
        if (!CandidateSet_Check(value)) {
            PyErr_Format(PyExc_TypeError,
                "remove_candidates: Expected values to be "
                "CandidateSets, not '%.100s'", Py_TYPE(value)->tp_name);
            return NULL;
        }

        UNPACK_KEY(key, return NULL, "remove_candidates");
        if (CELL_FILLED(self->ss_grid, x, y)) {
            _PyErr_SetKeyError(key);
            return NULL;
        }
        remove_set = ((CandidateSetObject *)value)->cs_set;
        old_set = CELL_CANDS(self->ss_grid, x, y);
        not_subset = remove_set & ~(remove_set & old_set);
        if (not_subset) {
            /* Make sure that we don't mess up house cand counts if we're
               trying to remove candidates that are aren't there. */
            house_adjust_cand_count_up(self, x, y, not_subset);
        }

        house_adjust_cand_count_down(self, x, y, remove_set);
        CELL_CANDS(self->ss_grid, x, y) &= ~remove_set;
        if (!CELL_CANDS(self->ss_grid, x, y)) {
            raise = 1;
            rx = x, ry = y;
        }
    }

    if (raise) {
        PyErr_Format(ContradictionError,
            "Empty candidate set at (%d, %d)", rx, ry);
        return NULL;
    }

    Py_RETURN_NONE;
}

/*[clinic input]
data.State.candidate_in_houses

    key: object
        This is the key that were checking; that is, we're checking
        the three houses that intersect in this key.

    cand: Py_ssize_t
        The candidate to check in the houses.
    /

Check a key's houses for a particular candidate.

The return value is a 3-tuple of ints telling us the candidate count for
the group, the column, and the row, respectively.
[clinic start generated code]*/

PyDoc_STRVAR(data_State_candidate_in_houses__doc__,
"candidate_in_houses($self, key, cand, /)\n"
"--\n"
"\n"
"Check a key\'s houses for a particular candidate.\n"
"\n"
"  key\n"
"    This is the key that were checking; that is, we\'re checking\n"
"    the three houses that intersect in this key.\n"
"  cand\n"
"    The candidate to check in the houses.\n"
"\n"
"The return value is a 3-tuple of ints telling us the candidate count for\n"
"the group, the column, and the row, respectively.");

#define DATA_STATE_CANDIDATE_IN_HOUSES_METHODDEF    \
    {"candidate_in_houses", (PyCFunction)data_State_candidate_in_houses, METH_VARARGS, data_State_candidate_in_houses__doc__},

static PyObject *
data_State_candidate_in_houses_impl(SudokuStateObject *self, PyObject *key, Py_ssize_t cand);

static PyObject *
data_State_candidate_in_houses(SudokuStateObject *self, PyObject *args)
{
    PyObject *return_value = NULL;
    PyObject *key;
    Py_ssize_t cand;

    if (!PyArg_ParseTuple(args,
        "On:candidate_in_houses",
        &key, &cand))
        goto exit;
    return_value = data_State_candidate_in_houses_impl(self, key, cand);

exit:
    return return_value;
}

static PyObject *
data_State_candidate_in_houses_impl(SudokuStateObject *self, PyObject *key, Py_ssize_t cand)
/*[clinic end generated code: output=94cbf6e76fcc7e95 input=f2631f690a4cc850]*/
{
    Py_ssize_t g, c, r;

    if (cand < 0 || cand >= NUMROWS) {
        PyErr_Format(PyExc_ValueError,
            "candidate_in_houses: Bad candidate '%d'", cand);
        return NULL;
    }

    UNPACK_KEY(key, return NULL, "candidate_in_houses");
    g = CELL_GROUP(self, x, y).hi_cand_count[cand];
    c = self->ss_houses[y+COLOFFSET].hi_cand_count[cand];
    r = self->ss_houses[x+ROWOFFSET].hi_cand_count[cand];

    return Py_BuildValue("(nnn)", g, c, r);
}

/*[clinic input]
data.State.candidates_from_house

    house: Py_ssize_t
        Index of the house to report the candidate counts of. This
        index corresponds to the index of the house in the houses
        attribute defined in state.py; 0-8 are groups, 9-17 are
        columns, and 18-26 are rows.
    /

Get the candidate counts for a house.

The return value is a 9 element tuple of ints. Similarly to the
num_clues attribute, the int at a particular index represents the
candidate count for that house. Example:

>>> state.candidates_from_house(18)[3]

gives the number of cells in the top row that can be 3.
[clinic start generated code]*/

PyDoc_STRVAR(data_State_candidates_from_house__doc__,
"candidates_from_house($self, house, /)\n"
"--\n"
"\n"
"Get the candidate counts for a house.\n"
"\n"
"  house\n"
"    Index of the house to report the candidate counts of. This\n"
"    index corresponds to the index of the house in the houses\n"
"    attribute defined in state.py; 0-8 are groups, 9-17 are\n"
"    columns, and 18-26 are rows.\n"
"\n"
"The return value is a 9 element tuple of ints. Similarly to the\n"
"num_clues attribute, the int at a particular index represents the\n"
"candidate count for that house. Example:\n"
"\n"
">>> state.candidates_from_house(18)[3]\n"
"\n"
"gives the number of cells in the top row that can be 3.");

#define DATA_STATE_CANDIDATES_FROM_HOUSE_METHODDEF    \
    {"candidates_from_house", (PyCFunction)data_State_candidates_from_house, METH_VARARGS, data_State_candidates_from_house__doc__},

static PyObject *
data_State_candidates_from_house_impl(SudokuStateObject *self, Py_ssize_t house);

static PyObject *
data_State_candidates_from_house(SudokuStateObject *self, PyObject *args)
{
    PyObject *return_value = NULL;
    Py_ssize_t house;

    if (!PyArg_ParseTuple(args,
        "n:candidates_from_house",
        &house))
        goto exit;
    return_value = data_State_candidates_from_house_impl(self, house);

exit:
    return return_value;
}

static PyObject *
data_State_candidates_from_house_impl(SudokuStateObject *self, Py_ssize_t house)
/*[clinic end generated code: output=fc684bd7d5663b12 input=e48123f8971ba9b8]*/
{
    PyObject *candidates, *integer;
    Py_ssize_t *cands_count;
    Py_ssize_t i;

    if (house < 0 || house >= 27) {
        PyErr_Format(PyExc_ValueError,
            "Expected a house index in range(0,27), "
            "but got '%d'", house);
        return NULL;
    }

    cands_count = self->ss_houses[house].hi_cand_count;
    candidates = PyTuple_New(NUMROWS);
    if (!candidates)
        return NULL;
    for (i = 0; i < NUMROWS; i++) {
        integer = PyLong_FromSsize_t(cands_count[i]);
        if (!integer) {
            Py_DECREF(candidates);
            return NULL;
        }
        PyTuple_SET_ITEM(candidates, i, integer);
    }

    return candidates;
}

/* writes rectangles found into pos, and returns num found */
static Py_ssize_t
find_rectangles_one_key(SudokuStateObject *self, Py_ssize_t x, Py_ssize_t y,
    PyObject *cands, PyObject **pos);

/*[clinic input]
data.State.find_rectangles

    key: object = NULL
        If not None, then we find all rectangles that have this key as the
        upper left corner. If this is None, then search the whole grid for
        rectangles.

    cands: object = NULL
        If this isn't None, then it should be a CandidateSet. In this case,
        return only rectangles where each key contains all candidates in the
        set. Otherwise, return every rectangle that shares at least one
        candidate.

Find rectangles -- four keys in two rows and two columns that share candidates.

This method returns a tuple of found rectangles. A rectangle is a two member
tuple. The first member of is the set of shared candidates. The second item
is a tuple of four keys; first: upper left, second: upper right, third:
lower right, fourth: lower left.
[clinic start generated code]*/

PyDoc_STRVAR(data_State_find_rectangles__doc__,
"find_rectangles($self, /, key=None, cands=None)\n"
"--\n"
"\n"
"Find rectangles -- four keys in two rows and two columns that share candidates.\n"
"\n"
"  key\n"
"    If not None, then we find all rectangles that have this key as the\n"
"    upper left corner. If this is None, then search the whole grid for\n"
"    rectangles.\n"
"  cands\n"
"    If this isn\'t None, then it should be a CandidateSet. In this case,\n"
"    return only rectangles where each key contains all candidates in the\n"
"    set. Otherwise, return every rectangle that shares at least one\n"
"    candidate.\n"
"\n"
"This method returns a tuple of found rectangles. A rectangle is a two member\n"
"tuple. The first member of is the set of shared candidates. The second item\n"
"is a tuple of four keys; first: upper left, second: upper right, third:\n"
"lower right, fourth: lower left.");

#define DATA_STATE_FIND_RECTANGLES_METHODDEF    \
    {"find_rectangles", (PyCFunction)data_State_find_rectangles, METH_VARARGS|METH_KEYWORDS, data_State_find_rectangles__doc__},

static PyObject *
data_State_find_rectangles_impl(SudokuStateObject *self, PyObject *key, PyObject *cands);

static PyObject *
data_State_find_rectangles(SudokuStateObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *return_value = NULL;
    static char *_keywords[] = {"key", "cands", NULL};
    PyObject *key = NULL;
    PyObject *cands = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs,
        "|OO:find_rectangles", _keywords,
        &key, &cands))
        goto exit;
    return_value = data_State_find_rectangles_impl(self, key, cands);

exit:
    return return_value;
}

static PyObject *
data_State_find_rectangles_impl(SudokuStateObject *self, PyObject *key, PyObject *cands)
/*[clinic end generated code: output=0f2accc852803472 input=32b5333a6b7eb6b3]*/
{
    PyObject *temp_storage[MAXRECT];
    Py_ssize_t found = 0, i, j, res;
    PyObject *tuple;

    if (cands == Py_None)
        cands = NULL;
    if (cands && !CandidateSet_Check(cands)) {
        PyErr_Format(PyExc_TypeError,
            "find_rectangles: expected CandidateSet or None, not '%.100s'",
            Py_TYPE(cands)->tp_name);
        return NULL;
    }

    if (key && key != Py_None) {
        UNPACK_KEY(key, goto error, "find_rectangles");
        found = find_rectangles_one_key(self, x, y, cands, temp_storage);
        if (found < 0)
            return NULL;
    } else {
        for (i = 0; i < NUMROWS-1; i++) {
            for (j = 0; j < NUMROWS-1; j++) {
                if (!CELL_FILLED(self->ss_grid, i, j)) {
                    res = find_rectangles_one_key(self, i, j, cands, temp_storage + found);
                    if (res < 0)
                        goto error;
                    found += res;
                }
            }
        }
    }

    tuple = PyTuple_New(found);
    if (!tuple)
        goto error;

    for (i = 0; i < found; i++) {
        PyTuple_SET_ITEM(tuple, i, temp_storage[i]);
    }
    return tuple;

error:
    for (i = 0; i < found; i++) {
        Py_DECREF(temp_storage[i]);
    }
    return NULL;
}

/* true if x is subset of y; see CandidateSet rich compare */
#define SUBSET(x,y) (((x) | (y)) == (y))

static Py_ssize_t
find_rectangles_one_key(SudokuStateObject *self, Py_ssize_t x, Py_ssize_t y,
    PyObject *cands, PyObject **pos)
{
    Py_ssize_t found = 0, i, j;
    uint16_t ul_set, required, intersection, tmp;
    PyObject *candidate_set, *next_slot;

    if (x == NUMROWS-1 || y == NUMROWS-1) {
        PyErr_Format(PyExc_ValueError,
            "find_rectangles: key (%ld, %ld) cannot be the upper left "
            "corner of a rectangle.", x, y);
        return -1;
    }
    if (CELL_FILLED(self->ss_grid, x, y))
        return 0;

    required = cands ? ((CandidateSetObject *)cands)->cs_set : 0;
    ul_set = CELL_CANDS(self->ss_grid, x, y);
    if (!SUBSET(required, ul_set))
        return 0;

    /* At this point, we've identified a potential upper left corner for
     * candidate rectangles. We proceed by walking to the right from this
     * corner, looking for an upper right corner, and then if we find one,
     * we'll walk down looking for the bottom corners.
     */
    for (j = y+1; j < NUMROWS; j++) {
        intersection = ul_set;
        if (!CELL_FILLED(self->ss_grid, x, j)) {
            intersection &= CELL_CANDS(self->ss_grid, x, j);
            if (intersection && SUBSET(required, intersection)) {
                /* we found an upper right corner */
                tmp = intersection;
                for (i = x+1; i < NUMROWS; i++) {
                    intersection = tmp;
                    if (!CELL_FILLED(self->ss_grid, i, y)) {
                        intersection &= CELL_CANDS(self->ss_grid, i, y);
                        if (intersection && SUBSET(required, intersection)) {
                            /* we found a lower left corner; one more to go */
                            if (!CELL_FILLED(self->ss_grid, i, j)) {
                                intersection &= CELL_CANDS(self->ss_grid, i, j);
                                if (intersection && SUBSET(required, intersection)) {
                                    /* found a complete rectangle */
                                    candidate_set = build_set(intersection);
                                    if (!candidate_set)
                                        goto error;
                                    next_slot = Py_BuildValue("(O((nn)(nn)(nn)(nn)))",
                                        /* keys are arranged in clockwise order */
                                        candidate_set, x,y, x,j, i,j, i,y);
                                    Py_DECREF(candidate_set);
                                    if (!next_slot)
                                        goto error;
                                    pos[found++] = next_slot;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return found;

error:
    for (i = 0; i < found; i++) {
        Py_DECREF(pos[i]);
    }
    return -1;
}

/* state_iterator object */

static PyTypeObject state_iterator_Type;
typedef struct _ki_ob KeyIterObject;

/* keyiterfuncs return -1 on error or completion, 0 on success.
 * On success, sets the last two args to the coordinates of the key
 * to be returned by next.
 */
typedef int (*keyiterfunc)(KeyIterObject *, Py_ssize_t *, Py_ssize_t *);

struct _ki_ob {
    PyObject_VAR_HEAD
    SudokuStateObject *ki_state;
    PyObject *ki_weakref;
    keyiterfunc ki_func;
    Py_ssize_t ki_next_x;
    Py_ssize_t ki_next_y;
    Py_ssize_t ki_pos;
    Py_ssize_t ki_data[1];
};

static KeyIterObject *
new_keyiter_object(Py_ssize_t nitems, SudokuStateObject *state)
{
    KeyIterObject *self;
    self = PyObject_NewVar(KeyIterObject, &state_iterator_Type, nitems);
    if (!self)
        return NULL;
    Py_INCREF(state);
    self->ki_state = state;
    self->ki_weakref = NULL;
    return self;
}

static void
data_KeyIter_dealloc(KeyIterObject *self)
{
    PyObject_ClearWeakRefs((PyObject *)self);
    Py_DECREF(self->ki_state);
    PyObject_Del((PyObject *)self);
}

static PyObject *
data_KeyIter_iternext(KeyIterObject *self)
{
    Py_ssize_t x, y;

    if (!self->ki_func)
        return NULL;
    if (self->ki_func(self, &x, &y) < 0) {
        self->ki_func = NULL;
        return NULL;
    }

    return Py_BuildValue("(nn)", x, y);
}

static PyMemberDef KeyIter_members[] = {
    {"state", T_OBJECT, offsetof(KeyIterObject, ki_state), READONLY},
    {"__weakref__", T_OBJECT, offsetof(KeyIterObject, ki_weakref), READONLY},
    {NULL}  /* sentinel */
};

static PyTypeObject state_iterator_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "engine.data.state_iterator",/*tp_name*/
    sizeof(KeyIterObject) - sizeof(Py_ssize_t),/*tp_basicsize*/
    sizeof(Py_ssize_t),         /*tp_itemsize*/
    /* methods */
    (destructor)data_KeyIter_dealloc,/*tp_dealloc*/
    0,                          /*tp_print*/
    0,                          /*tp_getattr*/
    0,                          /*tp_setattr*/
    0,                          /*tp_reserved*/
    0,                          /*tp_repr*/
    0,                          /*tp_as_number*/
    0,                          /*tp_as_sequence*/
    0,                          /*tp_as_mapping*/
    0,                          /*tp_hash*/
    0,                          /*tp_call*/
    0,                          /*tp_str*/
    0,                          /*tp_getattro*/
    0,                          /*tp_setattro*/
    0,                          /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,         /*tp_flags*/
    0,                          /*tp_doc*/
    0,                          /*tp_traverse*/
    0,                          /*tp_clear*/
    0,                          /*tp_richcompare*/
    offsetof(KeyIterObject, ki_weakref),/*tp_weaklistoffset*/
    PyObject_SelfIter,          /*tp_iter*/
    (iternextfunc)data_KeyIter_iternext,/*tp_iternext*/
    0,                          /*tp_methods*/
    KeyIter_members,            /*tp_members*/
};

/* Iterator methods -- the methods below return a state_iterator */

/* keyiterfunc for ones that calculate their iteration order at
 * construction time.
 */
static int
precalculated_keyiterfunc(KeyIterObject *ki, Py_ssize_t *x, Py_ssize_t *y)
{
    Py_ssize_t i;

    if (ki->ki_pos == Py_SIZE(ki))
        return -1;

    i = ki->ki_data[ki->ki_pos++];
    *x = ROW(i);
    *y = COL(i);
    return 0;
}

/* used to iterate in reverse order through a precalculated keyiter */
static int
reversed_keyiterfunc(KeyIterObject *ki, Py_ssize_t *x, Py_ssize_t *y)
{
    Py_ssize_t i;

    if (--ki->ki_pos == -1)
        return -1;

    i = ki->ki_data[ki->ki_pos];
    *x = ROW(i);
    *y = COL(i);
    return 0;
}

static KeyIterObject *
start_building_precalculated_keyiter(SudokuStateObject *self)
{
    KeyIterObject *keyiter;
    Py_ssize_t size;

    size = GRIDSIZE - self->ss_solved;
    keyiter = new_keyiter_object(size, self);
    if (!keyiter)
        return NULL;

    keyiter->ki_func = precalculated_keyiterfunc;
    keyiter->ki_pos = 0;

    return keyiter;
}

static int
simple_keyiterfunc(KeyIterObject *ki, Py_ssize_t *x, Py_ssize_t *y)
{
    Py_ssize_t i, j;
    cell_info *grid = ki->ki_state->ss_grid;

    for (i = ki->ki_next_x; i < NUMROWS; i++) {
        for (j = ki->ki_next_y; j < NUMROWS; j++) {
            if (!CELL_FILLED(grid, i, j)) {
                *x = ki->ki_next_x = i;
                *y = j;
                ki->ki_next_y = j + 1;
                return 0;
            }
        }
        ki->ki_next_y = 0;
    }

    return -1;
}

/*[clinic input]
data.State.order_simple

Iterates over every unsolved key in the grid in simple order.

In other words, it iterates over the unsolved keys, row by row,
column by column, starting from the upper left corner, down to
the lower right corner.
[clinic start generated code]*/

PyDoc_STRVAR(data_State_order_simple__doc__,
"order_simple($self, /)\n"
"--\n"
"\n"
"Iterates over every unsolved key in the grid in simple order.\n"
"\n"
"In other words, it iterates over the unsolved keys, row by row,\n"
"column by column, starting from the upper left corner, down to\n"
"the lower right corner.");

#define DATA_STATE_ORDER_SIMPLE_METHODDEF    \
    {"order_simple", (PyCFunction)data_State_order_simple, METH_NOARGS, data_State_order_simple__doc__},

static PyObject *
data_State_order_simple_impl(SudokuStateObject *self);

static PyObject *
data_State_order_simple(SudokuStateObject *self, PyObject *Py_UNUSED(ignored))
{
    return data_State_order_simple_impl(self);
}

static PyObject *
data_State_order_simple_impl(SudokuStateObject *self)
/*[clinic end generated code: output=27d373786f84cfdd input=a7c7e0b5ad9b4701]*/
{
    KeyIterObject *keyiter;

    keyiter = new_keyiter_object(0, self);
    if (keyiter) {
        keyiter->ki_func = simple_keyiterfunc;
        keyiter->ki_next_x = 0;
        keyiter->ki_next_y = 0;
    }

    return (PyObject *)keyiter;
}

static int
solved_keyiterfunc(KeyIterObject *ki, Py_ssize_t *x, Py_ssize_t *y)
{
    Py_ssize_t i, j;
    cell_info *grid = ki->ki_state->ss_grid;

    for (i = ki->ki_next_x; i < NUMROWS; i++) {
        for (j = ki->ki_next_y; j < NUMROWS; j++) {
            if (CELL_FILLED(grid, i, j)) {
                *x = ki->ki_next_x = i;
                *y = j;
                ki->ki_next_y = j + 1;
                return 0;
            }
        }
        ki->ki_next_y = 0;
    }

    return -1;
}

/*[clinic input]
data.State.order_solved

Iterate through all solved keys in simple order.
[clinic start generated code]*/

PyDoc_STRVAR(data_State_order_solved__doc__,
"order_solved($self, /)\n"
"--\n"
"\n"
"Iterate through all solved keys in simple order.");

#define DATA_STATE_ORDER_SOLVED_METHODDEF    \
    {"order_solved", (PyCFunction)data_State_order_solved, METH_NOARGS, data_State_order_solved__doc__},

static PyObject *
data_State_order_solved_impl(SudokuStateObject *self);

static PyObject *
data_State_order_solved(SudokuStateObject *self, PyObject *Py_UNUSED(ignored))
{
    return data_State_order_solved_impl(self);
}

static PyObject *
data_State_order_solved_impl(SudokuStateObject *self)
/*[clinic end generated code: output=c31b4dc27ab34e7c input=8d145a6787d1635c]*/
{
    KeyIterObject *keyiter;

    keyiter = new_keyiter_object(0, self);
    if (keyiter) {
        keyiter->ki_func = solved_keyiterfunc;
        keyiter->ki_next_x = 0;
        keyiter->ki_next_y = 0;
    }

    return (PyObject *)keyiter;
}

static int
exactly_n_keyiterfunc(KeyIterObject *ki, Py_ssize_t *x, Py_ssize_t *y)
{
    Py_ssize_t i, j;
    cell_info *grid = ki->ki_state->ss_grid;

    for (i = ki->ki_next_x; i < NUMROWS; i++) {
        for (j = ki->ki_next_y; j < NUMROWS; j++) {
            if (!CELL_FILLED(grid, i, j)) {
                if (isizes[(Py_ssize_t)CELL_CANDS(grid, i, j)] == ki->ki_pos) {
                    *x = ki->ki_next_x = i;
                    *y = j;
                    ki->ki_next_y = j + 1;
                    return 0;
                }
            }
        }
        ki->ki_next_y = 0;
    }

    return -1;
}

/*[clinic input]
data.State.order_exactly_n

    count: Py_ssize_t
        Yield keys with this many candidates.
    /

Yield keys that have a certain number of candidates.

Raises a ValueError if an insane count is given.
[clinic start generated code]*/

PyDoc_STRVAR(data_State_order_exactly_n__doc__,
"order_exactly_n($self, count, /)\n"
"--\n"
"\n"
"Yield keys that have a certain number of candidates.\n"
"\n"
"  count\n"
"    Yield keys with this many candidates.\n"
"\n"
"Raises a ValueError if an insane count is given.");

#define DATA_STATE_ORDER_EXACTLY_N_METHODDEF    \
    {"order_exactly_n", (PyCFunction)data_State_order_exactly_n, METH_VARARGS, data_State_order_exactly_n__doc__},

static PyObject *
data_State_order_exactly_n_impl(SudokuStateObject *self, Py_ssize_t count);

static PyObject *
data_State_order_exactly_n(SudokuStateObject *self, PyObject *args)
{
    PyObject *return_value = NULL;
    Py_ssize_t count;

    if (!PyArg_ParseTuple(args,
        "n:order_exactly_n",
        &count))
        goto exit;
    return_value = data_State_order_exactly_n_impl(self, count);

exit:
    return return_value;
}

static PyObject *
data_State_order_exactly_n_impl(SudokuStateObject *self, Py_ssize_t count)
/*[clinic end generated code: output=6d3ad1cb29c50356 input=f5d4d02978352105]*/
{
    KeyIterObject *keyiter;

    if (count < 0 || count >= NUMROWS) {
        PyErr_Format(PyExc_ValueError,
            "order_exactly_n: Bad candidate count '%d'", count);
        return NULL;
    }

    keyiter = new_keyiter_object(0, self);
    if (keyiter) {
        keyiter->ki_func = exactly_n_keyiterfunc;
        keyiter->ki_next_x = 0;
        keyiter->ki_next_y = 0;
        keyiter->ki_pos = count;    /* use pos to store arg */
    }

    return (PyObject *)keyiter;
}

/*[clinic input]
data.State.order_random

Yields unsolved keys in random order.

Each unsolved key will be given exactly once.
[clinic start generated code]*/

PyDoc_STRVAR(data_State_order_random__doc__,
"order_random($self, /)\n"
"--\n"
"\n"
"Yields unsolved keys in random order.\n"
"\n"
"Each unsolved key will be given exactly once.");

#define DATA_STATE_ORDER_RANDOM_METHODDEF    \
    {"order_random", (PyCFunction)data_State_order_random, METH_NOARGS, data_State_order_random__doc__},

static PyObject *
data_State_order_random_impl(SudokuStateObject *self);

static PyObject *
data_State_order_random(SudokuStateObject *self, PyObject *Py_UNUSED(ignored))
{
    return data_State_order_random_impl(self);
}

static PyObject *
data_State_order_random_impl(SudokuStateObject *self)
/*[clinic end generated code: output=03fbc0486f11a6cf input=d356deba3306ee04]*/
{
    KeyIterObject *ki;
    cell_info *grid;
    Py_ssize_t seen[GRIDSIZE];
    Py_ssize_t numtries, i, found = 0, key = 0;

    ki = start_building_precalculated_keyiter(self);
    if (!ki)
        return NULL;
    if (!Py_SIZE(ki))
        return (PyObject *)ki;
    numtries = Py_SIZE(ki)/6 + 1;

    uint8_t tries[numtries];

    grid = ki->ki_state->ss_grid;
    memset(seen, 0, sizeof(Py_ssize_t) * GRIDSIZE);
    if (_PyOS_URandom((void *)tries, numtries) < 0)
        return NULL;

    /* First generate keys by trying a few keys randomly. Numbers are generated
     * by operating system random, such as /dev/urandom.
     */
    for (i = 0; i < numtries; i++) {
        key = (Py_ssize_t)(tries[i] % GRIDSIZE);

        if (seen[key])
            continue;
        seen[key] = 1;

        if (!(grid[key].ci_value & ERRORBIT))
            continue;
        ki->ki_data[found++] = key;
        if (found == Py_SIZE(ki))
            break;
    }

    /* Then visit the rest of the unsolved keys by walking the grid in a seemingly
     * random order, inspired by the probe order of dicts.
     */
    while (found < Py_SIZE(ki)) {
        key = ((key << 2) + key + 1) & 127;
        if (key >= 81)
            continue;

        if (seen[key])
            continue;
        seen[key] = 1;

        if (!(grid[key].ci_value & ERRORBIT))
            continue;
        ki->ki_data[found++] = key;
    }

    return (PyObject *)ki;
}

/*[clinic input]
data.State.order_by_num_candidates

Yields keys based on the size of their candidate sets.

That is, keys with one candidate will be yielded, followed by keys with
two candidates, and so on. Indices with the same number of candidates
will be ordered the same as order_simple.
[clinic start generated code]*/

PyDoc_STRVAR(data_State_order_by_num_candidates__doc__,
"order_by_num_candidates($self, /)\n"
"--\n"
"\n"
"Yields keys based on the size of their candidate sets.\n"
"\n"
"That is, keys with one candidate will be yielded, followed by keys with\n"
"two candidates, and so on. Indices with the same number of candidates\n"
"will be ordered the same as order_simple.");

#define DATA_STATE_ORDER_BY_NUM_CANDIDATES_METHODDEF    \
    {"order_by_num_candidates", (PyCFunction)data_State_order_by_num_candidates, METH_NOARGS, data_State_order_by_num_candidates__doc__},

static PyObject *
data_State_order_by_num_candidates_impl(SudokuStateObject *self);

static PyObject *
data_State_order_by_num_candidates(SudokuStateObject *self, PyObject *Py_UNUSED(ignored))
{
    return data_State_order_by_num_candidates_impl(self);
}

static PyObject *
data_State_order_by_num_candidates_impl(SudokuStateObject *self)
/*[clinic end generated code: output=69277346c6c15a8a input=f9364d2884949c99]*/
{
    KeyIterObject *ki;
    Py_ssize_t set_lens[GRIDSIZE];
    Py_ssize_t i, j;
    Py_ssize_t sl = NUMROWS + 1;
    Py_ssize_t size = 0, found = 0;
    int seen[sl];
    cell_info *grid;

    ki = start_building_precalculated_keyiter(self);
    if (!ki)
        return NULL;
    grid = ki->ki_state->ss_grid;
    memset(seen, 0, sizeof(int) * sl);

    /* Populate the set_lens array with the sizes of every candidate set.
     * Then we make 10 passes through the array to determine the iteration
     * order of the keys.
     */
    for (i = 0; i < NUMROWS; i++) {
        for (j = 0; j < NUMROWS; j++) {
            if (CELL_FILLED(grid, i, j)) {
                set_lens[INDEX(i,j)] = -1;
            } else {
                size = isizes[(Py_ssize_t)CELL_CANDS(grid, i, j)];
                seen[size] = 1;
                set_lens[INDEX(i,j)] = size;
            }
        }
    }

    for (i = 0; i < sl; i++) {
        if (seen[i]) {
            for (j = 0; j < GRIDSIZE; j++) {
                if (set_lens[j] == i) {
                    ki->ki_data[found++] = j;
                }
            }
        }
    }

    return (PyObject *)ki;
}

/*[clinic input]
data.State.order_by_num_candidates_rev

Same as order_by_num_candidates but in reverse order.
[clinic start generated code]*/

PyDoc_STRVAR(data_State_order_by_num_candidates_rev__doc__,
"order_by_num_candidates_rev($self, /)\n"
"--\n"
"\n"
"Same as order_by_num_candidates but in reverse order.");

#define DATA_STATE_ORDER_BY_NUM_CANDIDATES_REV_METHODDEF    \
    {"order_by_num_candidates_rev", (PyCFunction)data_State_order_by_num_candidates_rev, METH_NOARGS, data_State_order_by_num_candidates_rev__doc__},

static PyObject *
data_State_order_by_num_candidates_rev_impl(SudokuStateObject *self);

static PyObject *
data_State_order_by_num_candidates_rev(SudokuStateObject *self, PyObject *Py_UNUSED(ignored))
{
    return data_State_order_by_num_candidates_rev_impl(self);
}

static PyObject *
data_State_order_by_num_candidates_rev_impl(SudokuStateObject *self)
/*[clinic end generated code: output=fca4277364ba9fb5 input=1a0cffd6bf5d4d36]*/
{
    KeyIterObject *ki
        = (KeyIterObject *)data_State_order_by_num_candidates_impl(self);
    if (!ki)
        return NULL;
    ki->ki_func = reversed_keyiterfunc;
    ki->ki_pos = Py_SIZE(ki);
    return (PyObject *)ki;
}

/* pickle support */

/* Create a dictionary containing either clues or candidates; used by pickle functions
 * and candidates/clues types. 
 */
typedef enum { CLUES, CANDS, } whichdict;

static PyObject *
build_dict(SudokuStateObject *state, whichdict wd, int hidden)
{
    Py_ssize_t i, j;
    PyObject *dict, *key = NULL, *value = NULL;

    dict = PyDict_New();
    if (!dict)
        return NULL;

    for (i = 0; i < NUMROWS; i++) {
        for (j = 0; j < NUMROWS; j++) {
            key = Py_BuildValue("(nn)", i, j);
            if (!key)
                goto error;

            switch (wd) {
            case CLUES:
                if (CELL_FILLED(state->ss_grid, i, j)) {
                    value = PyLong_FromLong((long)CELL_VALUE(state->ss_grid, i, j));
                    break;
                }
                goto decref;
            case CANDS:
                if (!CELL_FILLED(state->ss_grid, i ,j) ||
                    (hidden && CELL_CANDS(state->ss_grid, i, j) > 0)) {
                    value = build_set(CELL_CANDS(state->ss_grid, i, j));
                    break;
                }
                goto decref;
            default:
                PyErr_Format(PyExc_SystemError,
                    "Bad arg to build_dict: '%d'", wd);
                goto error;
            }

            if (!value) {
                Py_DECREF(key);
                goto error;
            }

            if (PyDict_SetItem(dict, key, value) < 0) {
                Py_DECREF(key);
                Py_DECREF(value);
                goto error;
            }

            Py_DECREF(value);
        decref:
            Py_DECREF(key);
        }
    }

    return dict;

error:
    Py_DECREF(dict);
    return NULL;
}

/*[clinic input]
data.State.__setstate__

    state: object
        A tuple; the first item is a dictionary that contains
        CandidateSets for keys with candidate data. The second
        item is the movehook from the pickled State object. The
        third is the object's dict in case dynamic attributes were
        assigned.
    /

Unpickle a State.
[clinic start generated code]*/

PyDoc_STRVAR(data_State___setstate____doc__,
"__setstate__($self, state, /)\n"
"--\n"
"\n"
"Unpickle a State.\n"
"\n"
"  state\n"
"    A tuple; the first item is a dictionary that contains\n"
"    CandidateSets for keys with candidate data. The second\n"
"    item is the movehook from the pickled State object. The\n"
"    third is the object\'s dict in case dynamic attributes were\n"
"    assigned.");

#define DATA_STATE___SETSTATE___METHODDEF    \
    {"__setstate__", (PyCFunction)data_State___setstate__, METH_O, data_State___setstate____doc__},

static PyObject *
data_State___setstate__(SudokuStateObject *self, PyObject *state)
/*[clinic end generated code: output=fa2f67e79e613542 input=d79485c6f1f74158]*/
{
    PyObject *cands, *key, *value, *hook, *dict;
    Py_ssize_t i = 0;
    uint16_t set;

    if (!PyTuple_Check(state) || PyTuple_GET_SIZE(state) != 3) {
        PyErr_Format(PyExc_TypeError,
            "__setstate__: Expected tuple, not '%.100s'",
            Py_TYPE(state)->tp_name);
        return NULL;
    }
    cands = PyTuple_GET_ITEM(state, 0);
    if (!PyDict_Check(cands)) {
        PyErr_Format(PyExc_TypeError,
            "__setstate__: Expected first item to be dict, not '%.100s'",
            Py_TYPE(cands)->tp_name);
        return NULL;
    }

    while (PyDict_Next(cands, &i, &key, &value)) {
        if (!CandidateSet_Check(value)) {
            PyErr_Format(PyExc_TypeError,
                "__setstate__: Expected value to be CandidateSet, not '%.100s'",
                Py_TYPE(value)->tp_name);
            return NULL;
        }
        set = ((CandidateSetObject *)value)->cs_set;

        UNPACK_KEY(key, return NULL, "__setstate__");
        CELL_CANDS(self->ss_grid, x, y) = set;
        if (!CELL_FILLED(self->ss_grid, x, y))
            house_adjust_cand_count_up(self, x, y, set);
    }

    dict = PyTuple_GET_ITEM(state, 2);
    if (dict != Py_None) {
        if (!PyDict_Check(dict)) {
            PyErr_Format(PyExc_TypeError,
            "__setstate__: Expected third item to be dict, not '%.100s'",
            Py_TYPE(dict)->tp_name);
            return NULL;
        }
        self->ss_dict = PyDict_New();
        if (!self->ss_dict)
            return NULL;
        i = 0;
        while (PyDict_Next(dict, &i, &key, &value)) {
            if (PyDict_SetItem(self->ss_dict, key, value) < 0)
                return NULL;
            Py_INCREF(key);
            Py_INCREF(value);
        }
    }

    hook = PyTuple_GET_ITEM(state, 1);
    if (hook != Py_None) {
        Py_INCREF(hook);
        self->ss_movehook = hook;
    }

    Py_RETURN_NONE;
}

/*[clinic input]
data.State.__reduce__

Pickle support for State objects.
[clinic start generated code]*/

PyDoc_STRVAR(data_State___reduce____doc__,
"__reduce__($self, /)\n"
"--\n"
"\n"
"Pickle support for State objects.");

#define DATA_STATE___REDUCE___METHODDEF    \
    {"__reduce__", (PyCFunction)data_State___reduce__, METH_NOARGS, data_State___reduce____doc__},

static PyObject *
data_State___reduce___impl(SudokuStateObject *self);

static PyObject *
data_State___reduce__(SudokuStateObject *self, PyObject *Py_UNUSED(ignored))
{
    return data_State___reduce___impl(self);
}

static PyObject *
data_State___reduce___impl(SudokuStateObject *self)
/*[clinic end generated code: output=45f0bbd5e088a3b2 input=738d173648e54c86]*/
{
    PyObject *clues, *cands, *reduction;
    Py_ssize_t len = self->ss_dict ? PyDict_Size(self->ss_dict) : 0;
    if (len < 0)
        return NULL;

    clues = build_dict(self, CLUES, 0);
    if (!clues)
        return NULL;
    cands = build_dict(self, CANDS, 1);
    if (!cands) {
        Py_DECREF(clues);
        return NULL;
    }

    reduction = Py_BuildValue("(O(OOO)(OOO)",
        Py_TYPE(self),
        clues,
        Py_False,   /* causes __init__ to not fill in pencilmarks */
        self->ss_grconfig == default_grconfig ? Py_None : self->ss_grconfig,
        cands,
        self->ss_movehook ? self->ss_movehook : Py_None,
        len > 0 ? self->ss_dict : Py_None);
    Py_DECREF(clues);
    Py_DECREF(cands);

    return reduction;
}

static PyMethodDef State_methods[] = {
    DATA_STATE_CANDIDATE_IN_KEYSET_METHODDEF
    DATA_STATE_CANDIDATES_FROM_KEYSET_METHODDEF
    DATA_STATE_ADD_CANDIDATES_METHODDEF
    DATA_STATE_REMOVE_CANDIDATES_METHODDEF
    DATA_STATE_CANDIDATE_IN_HOUSES_METHODDEF
    DATA_STATE_CANDIDATES_FROM_HOUSE_METHODDEF
    DATA_STATE_FIND_RECTANGLES_METHODDEF
    DATA_STATE_ORDER_SIMPLE_METHODDEF
    DATA_STATE_ORDER_SOLVED_METHODDEF
    DATA_STATE_ORDER_RANDOM_METHODDEF
    DATA_STATE_ORDER_BY_NUM_CANDIDATES_METHODDEF
    DATA_STATE_ORDER_BY_NUM_CANDIDATES_REV_METHODDEF
    DATA_STATE_ORDER_EXACTLY_N_METHODDEF
    DATA_STATE___SETSTATE___METHODDEF
    DATA_STATE___REDUCE___METHODDEF
    {NULL}  /* sentinel */
};

/* state_candidates and state_clues implementations */

typedef struct {
    PyObject_HEAD
    SudokuStateObject *state;
    PyObject *mp_weakref;
} SudokuMapObject;

static void
data_map_dealloc(SudokuMapObject *self)
{
    PyObject_ClearWeakRefs((PyObject *)self);
    Py_DECREF(self->state);
    Py_TYPE(self)->tp_free(self);
}

static Py_ssize_t
data_candidates_length(SudokuMapObject *self)
{
    return GRIDSIZE - self->state->ss_solved;
}

static PyObject *
data_candidates_subscript(SudokuMapObject *self, PyObject *key)
{
    SudokuStateObject *state = self->state;

    UNPACK_KEY(key, return NULL, "__getitem__");
    if (CELL_FILLED(state->ss_grid, x, y)) {
        _PyErr_SetKeyError(key);
        return NULL;
    }

    return build_set(CELL_CANDS(state->ss_grid, x, y));
}

static int
data_candidates_ass_sub(SudokuMapObject *self, PyObject *key, PyObject *cands)
{
    SudokuStateObject *state = self->state;
    uint16_t new_set, old_set;
    int need_decref = 0;
    int return_value = -1;

    /* For deletion, set to the empty CandidateSet */
    if (!cands) {
        cands = build_set(0);
        if (!cands)
            return -1;
        need_decref = 1;
    }

    if (!CandidateSet_Check(cands)) {
        PyErr_Format(PyExc_TypeError,
            "__setitem__: Expected CandidateSet, not '%.100s'",
            Py_TYPE(cands)->tp_name);
        goto done;
    }
    UNPACK_KEY(key, goto done,
        need_decref ? "__delitem__" : "__setitem__");

    if (CELL_FILLED(state->ss_grid, x, y)) {
        _PyErr_SetKeyError(key);
        goto done;
    }

    new_set = ((CandidateSetObject *)cands)->cs_set;
    old_set = CELL_CANDS(state->ss_grid, x, y);

    /* Adjust houses */
    house_adjust_cand_count_down(state, x, y, old_set);
    house_adjust_cand_count_up(state, x, y, new_set);

    CELL_CANDS(state->ss_grid, x, y) = new_set;
    return_value = 0;

done:
    if (need_decref)
        Py_DECREF(cands);
    return return_value;
}

static PyMappingMethods candidates_as_mapping = {
    (lenfunc)data_candidates_length,        /*mp_length*/
    (binaryfunc)data_candidates_subscript,  /*mp_subscript*/
    (objobjargproc)data_candidates_ass_sub, /*mp_ass_subscript*/
};

static Py_ssize_t
data_clues_length(SudokuMapObject *self)
{
    return self->state->ss_solved;
}

static PyObject *
data_clues_subscript(SudokuMapObject *self, PyObject *key)
{
    SudokuStateObject *state = self->state;

    UNPACK_KEY(key, return NULL, "__getitem__");

    if (!CELL_FILLED(state->ss_grid, x, y)) {
        _PyErr_SetKeyError(key);
        return NULL;
    }

    return PyLong_FromLong((long)CELL_VALUE(state->ss_grid, x, y));
}

static int
delete_clue(SudokuStateObject *state, PyObject *key)
{
    Py_ssize_t cl;

    UNPACK_KEY(key, return -1, "__delitem__");
    if (!CELL_FILLED(state->ss_grid, x, y)) {
        _PyErr_SetKeyError(key);
        return -1;
    }
    cl = CELL_VALUE(state->ss_grid, x, y);

    if (PySet_Discard(state->ss_skeys, key) < 0)
        return -1;
    state->ss_digits[cl]--;
    house_adjust_solved_down(state, x, y);
    house_adjust_cand_count_up(state, x, y, CELL_CANDS(state->ss_grid, x, y));
    CELL_VALUE(state->ss_grid, x, y) = -1;
    state->ss_solved--;

    return 0;
}

static int
assign_clue(SudokuStateObject *state, PyObject *key, Py_ssize_t digit)
{
    UNPACK_KEY(key, return -1, "__setitem__");

    if (CELL_FILLED(state->ss_grid, x, y)) {
        _PyErr_SetKeyError(key);
        return -1;
    }
    if (PySet_Add(state->ss_skeys, key) < 0)
        return -1;

    house_adjust_solved_up(state, x, y);
    house_adjust_cand_count_down(state, x, y, CELL_CANDS(state->ss_grid, x, y));
    CELL_VALUE(state->ss_grid, x, y) = (uint16_t)digit;
    state->ss_solved++;
    state->ss_digits[digit]++;
    
    return 0;
}

static int
data_clues_ass_sub(SudokuMapObject *self, PyObject* key, PyObject* value)
{
    int return_value = 0;
    Py_ssize_t digit;

    if (value == NULL) {
        return_value = delete_clue(self->state, key);
    } else {
        digit = PyLong_AsSsize_t(value);
        if (PyErr_Occurred()) {
            return_value = -1;
        } else if (digit < 0 || digit >= NUMROWS) {
            PyErr_Format(PyExc_ValueError,
                "__setitem__: Expected a digit from 0-%d, got '%ld'",
                NUMROWS, digit);
            return_value = -1;
        } else {
            return_value = assign_clue(self->state, key, digit);
        }
    }

    return return_value;
}

static PyMappingMethods clues_as_mapping = {
    (lenfunc)data_clues_length,         /*mp_length*/
    (binaryfunc)data_clues_subscript,   /*mp_subscript*/
    (objobjargproc)data_clues_ass_sub,  /*mp_ass_subscript*/
};

static PyMemberDef map_members[] = {
    {"state",       T_OBJECT, offsetof(SudokuMapObject, state),      READONLY},
    {"__weakref__", T_OBJECT, offsetof(SudokuMapObject, mp_weakref), READONLY},
    {NULL}  /* sentinel */
};

/*[clinic input]
data.state_candidates.fill

Fill in pencilmarks.

This method fills in all candidates in the grid naively based on the solved
cells. This doesn't do anything fancy like leave out candidates that are
eliminated by locked candidates.
[clinic start generated code]*/

PyDoc_STRVAR(data_state_candidates_fill__doc__,
"fill($self, /)\n"
"--\n"
"\n"
"Fill in pencilmarks.\n"
"\n"
"This method fills in all candidates in the grid naively based on the solved\n"
"cells. This doesn\'t do anything fancy like leave out candidates that are\n"
"eliminated by locked candidates.");

#define DATA_STATE_CANDIDATES_FILL_METHODDEF    \
    {"fill", (PyCFunction)data_state_candidates_fill, METH_NOARGS, data_state_candidates_fill__doc__},

static PyObject *
data_state_candidates_fill_impl(SudokuMapObject *self);

static PyObject *
data_state_candidates_fill(SudokuMapObject *self, PyObject *Py_UNUSED(ignored))
{
    return data_state_candidates_fill_impl(self);
}

static PyObject *
data_state_candidates_fill_impl(SudokuMapObject *self)
/*[clinic end generated code: output=04a7b76d9628e630 input=ee44234cd35a3892]*/
{
    if (fill_in_pencilmarks(self->state) < 0)
        return NULL;
    Py_RETURN_NONE;
}

/*[clinic input]
data.state_candidates.clear

Clear all pencilmarks.

This method deletes all candidates from the grid, including candidates for
cells which are solved but were unsolved.
[clinic start generated code]*/

PyDoc_STRVAR(data_state_candidates_clear__doc__,
"clear($self, /)\n"
"--\n"
"\n"
"Clear all pencilmarks.\n"
"\n"
"This method deletes all candidates from the grid, including candidates for\n"
"cells which are solved but were unsolved.");

#define DATA_STATE_CANDIDATES_CLEAR_METHODDEF    \
    {"clear", (PyCFunction)data_state_candidates_clear, METH_NOARGS, data_state_candidates_clear__doc__},

static PyObject *
data_state_candidates_clear_impl(SudokuMapObject *self);

static PyObject *
data_state_candidates_clear(SudokuMapObject *self, PyObject *Py_UNUSED(ignored))
{
    return data_state_candidates_clear_impl(self);
}

static PyObject *
data_state_candidates_clear_impl(SudokuMapObject *self)
/*[clinic end generated code: output=3a74712316627088 input=b38dc96986635a8a]*/
{
    Py_ssize_t i, j;
    uint16_t set;

    for (i = 0; i < NUMROWS; i++) {
        for (j = 0; j < NUMROWS; j++) {
            set = CELL_CANDS(self->state->ss_grid, i, j);
            if (set > 0) {
                if (!CELL_FILLED(self->state->ss_grid, i, j))
                    house_adjust_cand_count_down(self->state, i, j, set);
                CELL_CANDS(self->state->ss_grid, i, j) = 0;
            }
        }
    }

    Py_RETURN_NONE;
}

/*[clinic input]
data.state_candidates.getdict

Get a dictionary of CandidateSets.

The dictionary contains all candidate sets from unsolved cells
in the grid.
[clinic start generated code]*/

PyDoc_STRVAR(data_state_candidates_getdict__doc__,
"getdict($self, /)\n"
"--\n"
"\n"
"Get a dictionary of CandidateSets.\n"
"\n"
"The dictionary contains all candidate sets from unsolved cells\n"
"in the grid.");

#define DATA_STATE_CANDIDATES_GETDICT_METHODDEF    \
    {"getdict", (PyCFunction)data_state_candidates_getdict, METH_NOARGS, data_state_candidates_getdict__doc__},

static PyObject *
data_state_candidates_getdict_impl(SudokuMapObject *self);

static PyObject *
data_state_candidates_getdict(SudokuMapObject *self, PyObject *Py_UNUSED(ignored))
{
    return data_state_candidates_getdict_impl(self);
}

static PyObject *
data_state_candidates_getdict_impl(SudokuMapObject *self)
/*[clinic end generated code: output=f5e527202ff59ab1 input=6164e3445e6d6fdb]*/
{
    return build_dict(self->state, CANDS, 0);
}

/*[clinic input]
data.state_clues.getdict

Get a dictionary containing values for solved keys.
[clinic start generated code]*/

PyDoc_STRVAR(data_state_clues_getdict__doc__,
"getdict($self, /)\n"
"--\n"
"\n"
"Get a dictionary containing values for solved keys.");

#define DATA_STATE_CLUES_GETDICT_METHODDEF    \
    {"getdict", (PyCFunction)data_state_clues_getdict, METH_NOARGS, data_state_clues_getdict__doc__},

static PyObject *
data_state_clues_getdict_impl(SudokuMapObject *self);

static PyObject *
data_state_clues_getdict(SudokuMapObject *self, PyObject *Py_UNUSED(ignored))
{
    return data_state_clues_getdict_impl(self);
}

static PyObject *
data_state_clues_getdict_impl(SudokuMapObject *self)
/*[clinic end generated code: output=4fd506249e462bc7 input=eb62d226626d5704]*/
{
    return build_dict(self->state, CLUES, 0);
}

static PyMethodDef candidates_methods[] = {
    DATA_STATE_CANDIDATES_FILL_METHODDEF
    DATA_STATE_CANDIDATES_CLEAR_METHODDEF
    DATA_STATE_CANDIDATES_GETDICT_METHODDEF
    {NULL}  /* sentinel */
};

static PyMethodDef clues_methods[] = {
    DATA_STATE_CLUES_GETDICT_METHODDEF
    {NULL}  /* sentinel */
};

static PyObject *
data_candidates_iter(SudokuMapObject *self)
{
    return data_State_order_simple_impl(self->state);
}

static PyObject *
data_clues_iter(SudokuMapObject *self)
{
    return data_State_order_solved_impl(self->state);
}

static PyTypeObject state_candidates_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "data.state_candidates",    /*tp_name*/
    sizeof(SudokuMapObject),    /*tp_basicsize*/
    0,                          /*tp_itemsize*/
    /* methods */
    (destructor)data_map_dealloc,/*tp_dealloc*/
    0,                          /*tp_print*/
    0,                          /*tp_getattr*/
    0,                          /*tp_setattr*/
    0,                          /*tp_reserved*/
    0,                          /*tp_repr*/
    0,                          /*tp_as_number*/
    0,                          /*tp_as_sequence*/
    &candidates_as_mapping,     /*tp_as_mapping*/
    0,                          /*tp_hash*/
    0,                          /*tp_call*/
    0,                          /*tp_str*/
    0,                          /*tp_getattro*/
    0,                          /*tp_setattro*/
    0,                          /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,         /*tp_flags*/
    0,                          /*tp_doc*/
    0,                          /*tp_traverse*/
    0,                          /*tp_clear*/
    0,                          /*tp_richcompare*/
    offsetof(SudokuMapObject, mp_weakref),/*tp_weaklistoffset*/
    (getiterfunc)data_candidates_iter,/*tp_iter*/
    0,                          /*tp_iternext*/
    candidates_methods,         /*tp_methods*/
    map_members,                /*tp_members*/
};

static PyTypeObject state_clues_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "data.state_clues",         /*tp_name*/
    sizeof(SudokuMapObject),    /*tp_basicsize*/
    0,                          /*tp_itemsize*/
    /* methods */
    (destructor)data_map_dealloc,/*tp_dealloc*/
    0,                          /*tp_print*/
    0,                          /*tp_getattr*/
    0,                          /*tp_setattr*/
    0,                          /*tp_reserved*/
    0,                          /*tp_repr*/
    0,                          /*tp_as_number*/
    0,                          /*tp_as_sequence*/
    &clues_as_mapping,          /*tp_as_mapping*/
    0,                          /*tp_hash*/
    0,                          /*tp_call*/
    0,                          /*tp_str*/
    0,                          /*tp_getattro*/
    0,                          /*tp_setattro*/
    0,                          /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,         /*tp_flags*/
    0,                          /*tp_doc*/
    0,                          /*tp_traverse*/
    0,                          /*tp_clear*/
    0,                          /*tp_richcompare*/
    offsetof(SudokuMapObject, mp_weakref),/*tp_weaklistoffset*/
    (getiterfunc)data_clues_iter,/*tp_iter*/
    0,                          /*tp_iternext*/
    clues_methods,              /*tp_methods*/
    map_members,                /*tp_members*/
};

/* State getsets and members */

PyDoc_STRVAR(data_State_movehook_doc,
"Either the next move to be returned by a solver or None. Every time\n\
this attribute is accessed, it is set to None.");

static PyObject *
data_State_movehook_getter(SudokuStateObject *self)
{
    PyObject *move;

    if (!self->ss_movehook)
        Py_RETURN_NONE;
    move = self->ss_movehook;
    self->ss_movehook = NULL;

    return move;
}

static int
data_State_movehook_setter(SudokuStateObject *self, PyObject *move)
{
    Py_CLEAR(self->ss_movehook);
    if (move) {
        Py_INCREF(move);
        self->ss_movehook = move;
    }

    return 0;
}

static PyObject *
new_sudoku_map(SudokuStateObject *state, PyTypeObject *type)
{
    SudokuMapObject *self = PyObject_New(SudokuMapObject, type);
    if (!self)
        return NULL;
    Py_INCREF(state);
    self->state = state;
    self->mp_weakref = NULL;
    return (PyObject *)self;
}

PyDoc_STRVAR(data_State_candidates_doc,
"An accessor for candidate sets at unsolved keys.");

static PyObject *
data_State_candidates_getter(SudokuStateObject *self)
{
    return new_sudoku_map(self, &state_candidates_Type);
}

PyDoc_STRVAR(data_State_clues_doc,
"An accessor for values of solved keys.");

static PyObject *
data_State_clues_getter(SudokuStateObject *self)
{
    return new_sudoku_map(self, &state_clues_Type);
}

PyDoc_STRVAR(data_State_row_subgroups_doc,
"Dictionary of row subgroups; each key maps to a subgroup.\n\
A row subgroup is the intersection between a row and a group.");

static PyObject *
data_State_row_subgroups_getter(SudokuStateObject *self)
{
    PyObject *row_subgroups;

    row_subgroups = PyTuple_GetItem(self->ss_subgroups, 0);
    if (!row_subgroups)
        return NULL;

    Py_INCREF(row_subgroups);
    return row_subgroups;
}

PyDoc_STRVAR(data_State_col_subgroups_doc,
"Dictionary of col subgroups; each key maps to a subgroup.\n\
A col subgroup is the intersection between a column and a group.");

static PyObject *
data_State_col_subgroups_getter(SudokuStateObject *self)
{
    PyObject *col_subgroups;

    col_subgroups = PyTuple_GetItem(self->ss_subgroups, 1);
    if (!col_subgroups)
        return NULL;

    Py_INCREF(col_subgroups);
    return col_subgroups;
}

PyDoc_STRVAR(data_State_done_doc,
"True if the puzzle is done; i.e. if State.key_solved is true\n\
for all 81 cells in the grid.");

static PyObject *
data_State_done_getter(SudokuStateObject *self)
{
    PyObject *v = self->ss_solved == GRIDSIZE ? Py_True : Py_False;

    Py_INCREF(v);
    return v;
}

PyDoc_STRVAR(data_State_num_remaining_doc,
"How many cells are unsolved?");

static PyObject *
data_State_num_remaining_getter(SudokuStateObject *self)
{
    return PyLong_FromSsize_t(GRIDSIZE - self->ss_solved);
}

PyDoc_STRVAR(data_State_num_values_doc,
"A tuple containing the number of times that each clue appears in\n\
the grid. For example, num_values[4] tells you how many cells in the\n\
grid are set to 4.");

static PyObject *
data_State_num_values_getter(SudokuStateObject *self)
{
    PyObject *values, *integer;
    Py_ssize_t i;

    values = PyTuple_New(NUMROWS);
    if (!values)
        return NULL;
    for (i = 0; i < NUMROWS; i++) {
        integer = PyLong_FromSsize_t(self->ss_digits[i]);
        if (!integer) {
            Py_DECREF(values);
            return NULL;
        }
        PyTuple_SET_ITEM(values, i, integer);
    }

    return values;
}

static PyObject *
housekey_get(SudokuStateObject *self, Py_ssize_t n)
{
    PyObject *v = PyTuple_GET_ITEM(self->ss_housekeys, n);

    Py_INCREF(v);
    return v;
}

PyDoc_STRVAR(data_State_rows_doc,
"Keysets for each row.");

static PyObject *
data_State_rows_getter(SudokuStateObject *self)
{
    return housekey_get(self, 0);
}

PyDoc_STRVAR(data_State_cols_doc,
"Keysets for each column.");

static PyObject *
data_State_cols_getter(SudokuStateObject *self)
{
    return housekey_get(self, 1);
}

PyDoc_STRVAR(data_State_houses_doc,
"Keysets for every house, starting with groups, then columns, then rows.");

static PyObject *
data_State_houses_getter(SudokuStateObject *self)
{
    return housekey_get(self, 2);
}

PyDoc_STRVAR(data_State_has_default_config_doc,
"True if the State has a default group configuration, meaning\n\
that the groups are 3x3 boxes arranged in the normal way.");

static PyObject *
data_State_has_default_config_getter(SudokuStateObject *self)
{
    PyObject *v = self->ss_grconfig == default_grconfig ? Py_True : Py_False;

    Py_INCREF(v);
    return v;
}

static PyGetSetDef State_getsets[] = {
    {"movehook",      (getter)data_State_movehook_getter, (setter)data_State_movehook_setter, data_State_movehook_doc},
    {"candidates",    (getter)data_State_candidates_getter,    NULL, data_State_candidates_doc},
    {"clues",         (getter)data_State_clues_getter,         NULL, data_State_clues_doc},
    {"row_subgroups", (getter)data_State_row_subgroups_getter, NULL, data_State_row_subgroups_doc},
    {"col_subgroups", (getter)data_State_col_subgroups_getter, NULL, data_State_col_subgroups_doc},
    {"done",          (getter)data_State_done_getter,          NULL, data_State_done_doc},
    {"num_remaining", (getter)data_State_num_remaining_getter, NULL, data_State_num_remaining_doc},
    {"num_values",    (getter)data_State_num_values_getter,    NULL, data_State_num_values_doc},
    {"rows",          (getter)data_State_rows_getter,          NULL, data_State_rows_doc},
    {"cols",          (getter)data_State_cols_getter,          NULL, data_State_cols_doc},
    {"houses",        (getter)data_State_houses_getter,        NULL, data_State_houses_doc},
    {"has_default_config", (getter)data_State_has_default_config_getter, NULL, data_State_has_default_config_doc},
    {"__dict__", PyObject_GenericGetDict, NULL, NULL},
    {NULL}  /* sentinel */
};

static PyMemberDef State_members[] = {
    {"peers",       T_OBJECT,   offsetof(SudokuStateObject, ss_peers),      READONLY},
    {"grconfig",    T_OBJECT,   offsetof(SudokuStateObject, ss_grconfig),   READONLY},
    {"num_solved",  T_INT,      offsetof(SudokuStateObject, ss_solved),     READONLY},
    {"solved_keys", T_OBJECT,   offsetof(SudokuStateObject, ss_skeys),      READONLY},
    {"oneset",      T_OBJECT,   offsetof(SudokuStateObject, ss_oneset),     READONLY},
    {"__weakref__", T_OBJECT,   offsetof(SudokuStateObject, ss_weakref),    READONLY},
    {NULL}  /* sentinel */
};

/* other slots */

/* These attribute functions are slightly faster than the generic ones, and
 * the behavior is a bit different.
 */

static PyObject *
data_State_getattro(SudokuStateObject *self, PyObject *name)
{
    PyObject *descr, *res, *ob = (PyObject *)self;

    if (!State_CheckExact(self)) {
        /* We're a subtype; we'd better use default behavior. */
        return _PyObject_GenericGetAttrWithDict(ob, name, self->ss_dict);
    }

    descr = _PyType_Lookup(&SudokuState_Type, name);
    if (descr)
        return Py_TYPE(descr)->tp_descr_get(descr, ob, (PyObject *)&SudokuState_Type);

    if (self->ss_dict) {
        if ((res = PyDict_GetItem(self->ss_dict, name))) {
            Py_INCREF(res);
            return res;
        }
    }

    PyErr_Format(PyExc_AttributeError,
        "State: attribute '%U' not found", name);

    return NULL;
}

static int
data_State_setattro(SudokuStateObject *self, PyObject *name, PyObject *value)
{
    _Py_IDENTIFIER(movehook);
    PyObject *ob = (PyObject *)self;
    int res;

    if (!State_CheckExact(self)) {
        /* We're a subtype; we'd better use default behavior. */
        return _PyObject_GenericSetAttrWithDict(ob, name, value, self->ss_dict);
    }

    if (_PyUnicode_CompareWithId(name, &PyId_movehook) == 0) {
        /* movehook is the only setable descriptor attribute, so we doesn't search
         * through our type dicts. If name is not movehook, then we'll let the attribute
         * get set in __dict__. If the name is a different descriptor attribute, the new
         * attribute will be inaccessible, instead giving the original method/member on
         * attribute access. This is to mess with people's heads. No, it's actually so
         * that we won't have to look through the type's dict to check if the attribute
         * isn't setable.
         */
         return data_State_movehook_setter(self, value);
    }

    if (!self->ss_dict) {
        self->ss_dict = PyDict_New();
        if (!self->ss_dict)
            return -1;
    }

    if (value) {
        res = PyDict_SetItem(self->ss_dict, name, value);
    } else {
        res = PyDict_DelItem(self->ss_dict, name);
    }
    if (res < 0 && PyErr_ExceptionMatches(PyExc_KeyError))
        PyErr_SetObject(PyExc_AttributeError, name);

    return res;
}

/* The only attributes that should be able to participate in circular references
 * are movehook and __dict__. There are other possiblities if people were to
 * mutate other attributes, but there is no need to protect against these
 * cases.
 */

static int
data_State_traverse(SudokuStateObject *self, visitproc visit, void *arg)
{
    Py_VISIT(self->ss_dict);
    Py_VISIT(self->ss_movehook);
    return 0;
}

static int
data_State_clear(SudokuStateObject *self)
{
    Py_CLEAR(self->ss_dict);
    Py_CLEAR(self->ss_movehook);
    return 0;
}

static PyTypeObject SudokuState_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "engine.data.State",        /*tp_name*/
    sizeof(SudokuStateObject),  /*tp_basicsize*/
    0,                          /*tp_itemsize*/
    /* methods */
    (destructor)data_State_dealloc,/*tp_dealloc*/
    0,                          /*tp_print*/
    0,                          /*tp_getattr*/
    0,                          /*tp_setattr*/
    0,                          /*tp_reserved*/
    0,                          /*tp_repr*/
    0,                          /*tp_as_number*/
    0,                          /*tp_as_sequence*/
    0,                          /*tp_as_mapping*/
    0,                          /*tp_hash*/
    0,                          /*tp_call*/
    0,                          /*tp_str*/
    (getattrofunc)data_State_getattro,/*tp_getattro*/
    (setattrofunc)data_State_setattro,/*tp_setattro*/
    0,                          /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE |
        Py_TPFLAGS_HAVE_GC,     /*tp_flags*/
    data_State___init____doc__, /*tp_doc*/
    (traverseproc)data_State_traverse,/*tp_traverse*/
    (inquiry)data_State_clear,  /*tp_clear*/
    0,                          /*tp_richcompare*/
    offsetof(SudokuStateObject, ss_weakref),/*tp_weaklistoffset*/
    0,                          /*tp_iter*/
    0,                          /*tp_iternext*/
    State_methods,              /*tp_methods*/
    State_members,              /*tp_members*/
    State_getsets,              /*tp_getset*/
    0,                          /*tp_base*/
    0,                          /*tp_dict*/
    0,                          /*tp_descr_get*/
    0,                          /*tp_descr_set*/
    offsetof(SudokuStateObject, ss_dict),/*tp_dictoffset*/
    data_State___init__,        /*tp_init*/
    0,                          /*tp_alloc*/
    PyType_GenericNew,          /*tp_new*/
    PyObject_GC_Del,            /*tp_free*/
    0,                          /*tp_is_gc*/
};

/* Module level stuff */

void
data_free(void *m)
{
    Py_XDECREF(ContradictionError);
    Py_XDECREF(config_module);
    Py_XDECREF(default_grconfig);
    Py_XDECREF(default_peers);
    Py_XDECREF(default_subgroups);
    Py_XDECREF(default_housekeys);
    Py_XDECREF(default_oneset);
    /*printf("num allocs: %d, num deallocs %d\n", num_allocs, num_deallocs);*/
}

static struct PyModuleDef datamodule = {
    PyModuleDef_HEAD_INIT,
    "engine.data",
    module_doc,
    -1,
    NULL,
    NULL,
    NULL,
    NULL,
    data_free
};

PyMODINIT_FUNC
PyInit_data(void)
{
    _Py_IDENTIFIER(ContradictionError);
    
    PyObject *m = NULL, *err_mod, *con_mod, *err_dict;
    Py_ssize_t i;
    
    /* Get globals */
    con_mod = PyImport_ImportModule("engine.config");
    err_mod = PyImport_ImportModule("engine.errors");
    if (!con_mod || !err_mod)
        goto fail;
    config_module = PyModule_GetDict(con_mod);
    err_dict = PyModule_GetDict(err_mod);
    ContradictionError = _PyDict_GetItemId(err_dict, &PyId_ContradictionError);
    if (!ContradictionError) {
        PyErr_SetString(PyExc_AttributeError, "Can't find ContradictionError");
        goto fail;
    }
    Py_INCREF(ContradictionError);

    /* Default state attributes */
    default_grconfig = do_default_build_config();
    if (!default_grconfig)
        goto fail;
    default_peers = do_calculate_peers(default_grconfig);
    if (!default_peers)
        goto fail;
    default_subgroups = do_calculate_subgroups(default_peers);
    if (!default_subgroups)
        goto fail;
    default_housekeys = do_calculate_housekeys(default_grconfig);
    if (!default_housekeys)
        goto fail;
    default_oneset = do_calculate_oneset(default_peers);
    if (!default_oneset)
        goto fail;
    
    /* Prepare types */
    if (PyType_Ready(&SudokuState_Type)      < 0 ||
        PyType_Ready(&CandidateSet_Type)     < 0 ||
        PyType_Ready(&state_iterator_Type)   < 0 ||
        PyType_Ready(&state_candidates_Type) < 0 ||
        PyType_Ready(&state_clues_Type)      < 0   )
        goto fail;
    m = PyModule_Create(&datamodule);
    if (!m)
        goto fail;
    Py_INCREF(&SudokuState_Type);
    Py_INCREF(&CandidateSet_Type);
    PyModule_AddObject(m, "State", (PyObject *)&SudokuState_Type);
    PyModule_AddObject(m, "CandidateSet", (PyObject *)&CandidateSet_Type);

    /* Intern candidate set sizes */
    for (i = 0; i < 512; i++)
        isizes[i] = (Py_ssize_t)count_ones((int)i);

    /* Done */
    Py_DECREF(con_mod);
    Py_DECREF(err_mod);
   /* printf("%lu\n", sizeof(SudokuStateObject));*/
    return m;

fail:
    Py_XDECREF(con_mod);
    Py_XDECREF(err_mod);
    Py_XDECREF(m);
    return NULL;
}
