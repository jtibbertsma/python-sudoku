#! /usr/bin/env python3

from distutils.core import setup, Extension

datamodule = Extension('sudoku.data',
                       ['sudoku/datamodule.c'])

setup(name='sudoku',
      author='Joseph Tibbertsma',
      author_email='josephtibbertsma@gmail.com',
      ext_modules=[datamodule])
