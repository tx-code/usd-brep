SMLib Python API
================

The historical ``usd_brep`` package wraps the ``SM_API`` geometry layer through
pybind11. Its compiled module is ``_omni_solid``. Python 3.12 is required.
Use the runtime directory from the repository README on ``PYTHONPATH``.

.. code-block:: python

   import usd_brep as sm
   box = sm.create_box((0, 0, 0), 10, 20, 30)
   mesh = sm.tessellate(box)

Geometry functions are available flat and through category submodules such as
``primitives``, ``curves`` and ``booleans``. Native STEP import is a separate
C++ library. Debugger functions live in the optional ``_smlib_dev`` module.

Example
-------

``Examples/PyAPI/round_corner_pipe.py`` builds a bounded rectangular pipe with spherical corners from
a planar sweep, primitives, transforms and exact booleans:

.. literalinclude:: ../Examples/PyAPI/round_corner_pipe.py
   :language: python
   :pyobject: build_round_corner_pipe

Reference
---------

.. automodule:: usd_brep
    :members:
    :imported-members:
