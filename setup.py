#! /usr/bin/env python3

from distutils.core import setup, Extension

datamodule = Extension('engine.data',
                       ['engine/datamodule.c'])

setup(name='engine',
      author='Joseph Tibbertsma',
      author_email='josephtibbertsma@gmail.com',
      ext_modules=[datamodule])
