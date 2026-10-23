# Third-Party Notices

**USD BRep (SMLib geometry SDK)** is Copyright (c) 2020-2026, NVIDIA CORPORATION & AFFILIATES. All
rights reserved. NVIDIA-authored portions of this product are licensed under the
Apache License, Version 2.0, except where otherwise noted. See the `LICENSE` file
at the root of this repository for the full product license text.

This inventory covers the current geometry SDK, its Python build dependencies,
repository bootstrap scripts, and optional GUI tools. Dependency identities come
from `deps/target-deps-python.packman.xml`, `third_party/microsoft-gsl/UPSTREAM.json`,
`third_party/boost-multiprecision/UPSTREAM.json`, `third_party/tl-expected/UPSTREAM.json`,
`pyproject.toml`, and `uv.lock`. The native SDK archive and geometry wheel have
different contents, as described below; optional GUI dependencies are not vendored.

# Third-Party Open Source Components

## Microsoft Corporation - Guidelines Support Library - MIT License

Component: `third_party/microsoft-gsl` (version 5.0.1).

Attribution Statements: Native STEP components use the vendored header-only
Guidelines Support Library. Its unmodified headers, license, embedded notices,
and pinned source identity are recorded in `third_party/microsoft-gsl/` and
`UPSTREAM.json`. The native SDK archive carries those files; this document carries
the license text with the geometry wheel. Source: https://github.com/microsoft/GSL.

License Text

```
Copyright (c) 2015 Microsoft Corporation. All rights reserved.

This code is licensed under the MIT License (MIT).

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

---

## Sy Brand - tl::expected - CC0-1.0

Component: `third_party/tl-expected` (version 1.3.1).

Attribution Statements: Private native STEP import implementations use the
unmodified single-header expected implementation. Its commit, archive and file
hashes are recorded in `UPSTREAM.json`. The native SDK retains that source and
provenance and installs the dedication under `PACKAGE-LICENSES/TlExpected`.
Public SDK headers do not require tl types. Source:
https://github.com/TartanLlama/expected.

License Text

```
Creative Commons Legal Code

CC0 1.0 Universal

    CREATIVE COMMONS CORPORATION IS NOT A LAW FIRM AND DOES NOT PROVIDE
    LEGAL SERVICES. DISTRIBUTION OF THIS DOCUMENT DOES NOT CREATE AN
    ATTORNEY-CLIENT RELATIONSHIP. CREATIVE COMMONS PROVIDES THIS
    INFORMATION ON AN "AS-IS" BASIS. CREATIVE COMMONS MAKES NO WARRANTIES
    REGARDING THE USE OF THIS DOCUMENT OR THE INFORMATION OR WORKS
    PROVIDED HEREUNDER, AND DISCLAIMS LIABILITY FOR DAMAGES RESULTING FROM
    THE USE OF THIS DOCUMENT OR THE INFORMATION OR WORKS PROVIDED
    HEREUNDER.

Statement of Purpose

The laws of most jurisdictions throughout the world automatically confer
exclusive Copyright and Related Rights (defined below) upon the creator
and subsequent owner(s) (each and all, an "owner") of an original work of
authorship and/or a database (each, a "Work").

Certain owners wish to permanently relinquish those rights to a Work for
the purpose of contributing to a commons of creative, cultural and
scientific works ("Commons") that the public can reliably and without fear
of later claims of infringement build upon, modify, incorporate in other
works, reuse and redistribute as freely as possible in any form whatsoever
and for any purposes, including without limitation commercial purposes.
These owners may contribute to the Commons to promote the ideal of a free
culture and the further production of creative, cultural and scientific
works, or to gain reputation or greater distribution for their Work in
part through the use and efforts of others.

For these and/or other purposes and motivations, and without any
expectation of additional consideration or compensation, the person
associating CC0 with a Work (the "Affirmer"), to the extent that he or she
is an owner of Copyright and Related Rights in the Work, voluntarily
elects to apply CC0 to the Work and publicly distribute the Work under its
terms, with knowledge of his or her Copyright and Related Rights in the
Work and the meaning and intended legal effect of CC0 on those rights.

1. Copyright and Related Rights. A Work made available under CC0 may be
protected by copyright and related or neighboring rights ("Copyright and
Related Rights"). Copyright and Related Rights include, but are not
limited to, the following:

  i. the right to reproduce, adapt, distribute, perform, display,
     communicate, and translate a Work;
 ii. moral rights retained by the original author(s) and/or performer(s);
iii. publicity and privacy rights pertaining to a person's image or
     likeness depicted in a Work;
 iv. rights protecting against unfair competition in regards to a Work,
     subject to the limitations in paragraph 4(a), below;
  v. rights protecting the extraction, dissemination, use and reuse of data
     in a Work;
 vi. database rights (such as those arising under Directive 96/9/EC of the
     European Parliament and of the Council of 11 March 1996 on the legal
     protection of databases, and under any national implementation
     thereof, including any amended or successor version of such
     directive); and
vii. other similar, equivalent or corresponding rights throughout the
     world based on applicable law or treaty, and any national
     implementations thereof.

2. Waiver. To the greatest extent permitted by, but not in contravention
of, applicable law, Affirmer hereby overtly, fully, permanently,
irrevocably and unconditionally waives, abandons, and surrenders all of
Affirmer's Copyright and Related Rights and associated claims and causes
of action, whether now known or unknown (including existing as well as
future claims and causes of action), in the Work (i) in all territories
worldwide, (ii) for the maximum duration provided by applicable law or
treaty (including future time extensions), (iii) in any current or future
medium and for any number of copies, and (iv) for any purpose whatsoever,
including without limitation commercial, advertising or promotional
purposes (the "Waiver"). Affirmer makes the Waiver for the benefit of each
member of the public at large and to the detriment of Affirmer's heirs and
successors, fully intending that such Waiver shall not be subject to
revocation, rescission, cancellation, termination, or any other legal or
equitable action to disrupt the quiet enjoyment of the Work by the public
as contemplated by Affirmer's express Statement of Purpose.

3. Public License Fallback. Should any part of the Waiver for any reason
be judged legally invalid or ineffective under applicable law, then the
Waiver shall be preserved to the maximum extent permitted taking into
account Affirmer's express Statement of Purpose. In addition, to the
extent the Waiver is so judged Affirmer hereby grants to each affected
person a royalty-free, non transferable, non sublicensable, non exclusive,
irrevocable and unconditional license to exercise Affirmer's Copyright and
Related Rights in the Work (i) in all territories worldwide, (ii) for the
maximum duration provided by applicable law or treaty (including future
time extensions), (iii) in any current or future medium and for any number
of copies, and (iv) for any purpose whatsoever, including without
limitation commercial, advertising or promotional purposes (the
"License"). The License shall be deemed effective as of the date CC0 was
applied by Affirmer to the Work. Should any part of the License for any
reason be judged legally invalid or ineffective under applicable law, such
partial invalidity or ineffectiveness shall not invalidate the remainder
of the License, and in such case Affirmer hereby affirms that he or she
will not (i) exercise any of his or her remaining Copyright and Related
Rights in the Work or (ii) assert any associated claims and causes of
action with respect to the Work, in either case contrary to Affirmer's
express Statement of Purpose.

4. Limitations and Disclaimers.

 a. No trademark or patent rights held by Affirmer are waived, abandoned,
    surrendered, licensed or otherwise affected by this document.
 b. Affirmer offers the Work as-is and makes no representations or
    warranties of any kind concerning the Work, express, implied,
    statutory or otherwise, including without limitation warranties of
    title, merchantability, fitness for a particular purpose, non
    infringement, or the absence of latent or other defects, accuracy, or
    the present or absence of errors, whether or not discoverable, all to
    the greatest extent permissible under applicable law.
 c. Affirmer disclaims responsibility for clearing rights of other persons
    that may apply to the Work or any use thereof, including without
    limitation any person's Copyright and Related Rights in the Work.
    Further, Affirmer disclaims responsibility for obtaining any necessary
    consents, permissions or other rights required for any use of the
    Work.
 d. Affirmer understands and acknowledges that Creative Commons is not a
    party to this document and has no duty or obligation with respect to
    this CC0 or use of the Work.
```

---

## Boost authors - Boost.Multiprecision and Boost.Config - Boost Software License 1.0

Component: `third_party/boost-multiprecision` (version 1.89.0).

Attribution Statements: Native STEP unit normalization uses unmodified standalone
Boost.Multiprecision and Boost.Config headers. Source commits, archive identities,
and individual header hashes are pinned in `UPSTREAM.json`. Per-header author
notices remain in the vendored sources. Only the private unit implementation uses
these headers; no Boost type enters the public SDK. The native SDK includes the
license under `PACKAGE-LICENSES/BoostMultiprecision`. Optional portable tools carry
this notice file. Sources: https://github.com/boostorg/multiprecision and
https://github.com/boostorg/config.

License Text

```
Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

---

## Python Software Foundation - CPython runtime library - Python Software Foundation License Version 2

Component: `python` (version 3.12.15), the runtime library only

Attribution Statements: The SDK build and optional tools use CPython 3.12 from
`deps/target-deps-python.packman.xml` or a caller-supplied interpreter. The geometry
wheel and native SDK archive do not vendor the CPython runtime or standard library;
Python consumers supply their own compatible interpreter. CPython's complete license
and incorporated-software notices are provided with that interpreter. Source code:
https://github.com/python/cpython.

License Text (https://docs.python.org/3.12/license.html; CPython's full `LICENSE` file also
gives the license history and the earlier BeOpen, CNRI and CWI agreements)

```
PYTHON SOFTWARE FOUNDATION LICENSE VERSION 2
--------------------------------------------

1. This LICENSE AGREEMENT is between the Python Software Foundation
("PSF"), and the Individual or Organization ("Licensee") accessing and
otherwise using this software ("Python") in source or binary form and
its associated documentation.

2. Subject to the terms and conditions of this License Agreement, PSF hereby
grants Licensee a nonexclusive, royalty-free, world-wide license to reproduce,
analyze, test, perform and/or display publicly, prepare derivative works,
distribute, and otherwise use Python alone or in any derivative version,
provided, however, that PSF's License Agreement and PSF's notice of copyright,
i.e., "Copyright (c) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010,
2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019, 2020, 2021, 2022, 2023 Python Software Foundation;
All Rights Reserved" are retained in Python alone or in any derivative version
prepared by Licensee.

3. In the event Licensee prepares a derivative work that is based on
or incorporates Python or any part thereof, and wants to make
the derivative work available to others as provided herein, then
Licensee hereby agrees to include in any such work a brief summary of
the changes made to Python.

4. PSF is making Python available to Licensee on an "AS IS"
basis.  PSF MAKES NO REPRESENTATIONS OR WARRANTIES, EXPRESS OR
IMPLIED.  BY WAY OF EXAMPLE, BUT NOT LIMITATION, PSF MAKES NO AND
DISCLAIMS ANY REPRESENTATION OR WARRANTY OF MERCHANTABILITY OR FITNESS
FOR ANY PARTICULAR PURPOSE OR THAT THE USE OF PYTHON WILL NOT
INFRINGE ANY THIRD PARTY RIGHTS.

5. PSF SHALL NOT BE LIABLE TO LICENSEE OR ANY OTHER USERS OF PYTHON
FOR ANY INCIDENTAL, SPECIAL, OR CONSEQUENTIAL DAMAGES OR LOSS AS
A RESULT OF MODIFYING, DISTRIBUTING, OR OTHERWISE USING PYTHON,
OR ANY DERIVATIVE THEREOF, EVEN IF ADVISED OF THE POSSIBILITY THEREOF.

6. This License Agreement will automatically terminate upon a material
breach of its terms and conditions.

7. Nothing in this License Agreement shall be deemed to create any
relationship of agency, partnership, or joint venture between PSF and
Licensee.  This License Agreement does not grant permission to use PSF
trademarks or trade name in a trademark sense to endorse or promote
products or services of Licensee, or any third party.

8. By copying, installing or otherwise using Python, Licensee
agrees to be bound by the terms and conditions of this License
Agreement.
```

---

## INRIA, CMU, Microsoft Corporation and HACL* Contributors - HACL* - MIT License

Component: HACL* (CPython 3.12.15's `Modules/_hacl`, built into `python312.dll` on Windows)

Attribution Statements: The fetched Windows CPython development runtime includes
HACL* code in `python312.dll`. This is an interpreter dependency, rather than a
library vendored into the geometry wheel or SDK archive. Source code is available
at https://github.com/hacl-star/hacl-star.

License Text(https://github.com/python/cpython/blob/v3.12.15/Modules/_hacl/Hacl_Hash_SHA3.c)

```
MIT License

Copyright (c) 2016-2022 INRIA, CMU and Microsoft Corporation
Copyright (c) 2022-2023 HACL* Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## Wenzel Jakob - pybind11 - BSD 3-Clause License

Component: `pybind11` (version 2.11.1)

Attribution Statements: The SMLib geometry SDK includes pybind11 headers and uses its
macros/types to define Python binding modules that expose the C++ and C APIs to
Python. This is a compile-time, header-only interaction. Source code is available at
https://github.com/pybind/pybind11.

License Text(https://github.com/pybind/pybind11/blob/master/LICENSE)

```
Copyright (c) 2016 Wenzel Jakob <wenzel.jakob@epfl.ch>, All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

---

## NVIDIA CORPORATION - Repository Bootstrap Scripts - MIT License

Component: `repo.sh`, `repo.bat`, `tools/repoman/repoman_bootstrapper.py`

Attribution Statements: These three files bootstrap the `repo` build-tool launcher
shared across NVIDIA Omniverse repositories and are vendored/updated from that shared
tooling. NVIDIA actively chooses the MIT license for these specific files; the rest of
the NVIDIA-authored source in this repository is licensed as stated at the top of this
document and in `LICENSE`.

License Text

```
MIT License

Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

---

## Various - Optional GUI developer tool (uv.lock dependency group `gui`) - LGPL v3, MIT, BSD and others

Component: Python packages declared in `pyproject.toml` (dependency group `gui`) and
pinned, with their transitive dependencies, in `uv.lock`. They are used only by the
optional SMLib GUI developer tool (`tools/smlib_gui`, started with
`uv run --locked --group gui python tools/scripts/smlib_gui.py`).

Attribution Statements: These packages are obtained from PyPI by the user when the GUI is
run. They are not packman dependencies and are not redistributed in the geometry wheel or
release package, which contain none of the GUI code. They are listed for completeness
because the source repository carries `pyproject.toml`, `uv.lock` and the GUI. The licenses
below are the ones each package declares in its metadata; the authoritative license texts
are the ones shipped inside each package. `uv.lock` is the authoritative full list (35
packages for the locked Linux and Windows environments). The PySide6-Essentials wheel also
contains Qt modules that Qt licenses only under the GPL (Qt Qml Compiler, Qt Lottie
Animation, Qt Quick Timeline, Qt Wayland Compositor and the Qt Virtual Keyboard input
plugin); the GUI uses only QtCore, QtGui and QtWidgets.

Direct dependencies:

| Package | Version | License | Use |
|---|---|---|---|
| PySide6-Essentials | 6.11.1 | LGPL v3 OR GPL v2 OR GPL v3 | GUI toolkit (includes the Qt 6 libraries) |
| numpy | 2.4.6 | BSD-3-Clause AND 0BSD AND MIT AND Zlib AND CC0-1.0 | array handling |
| pyvista | 0.48.4 | MIT | 3D plotting |
| pyvistaqt | 0.11.4 | MIT | embeds pyvista in a Qt window |
| vtk | 9.6.2 | BSD | rendering |

Transitive dependencies, by declared license:

* LGPL v3 OR GPL v2 OR GPL v3: shiboken6 (the binding runtime used by PySide6)
* BSD-2-Clause: Pygments
* BSD / BSD-3-Clause: contourpy, cycler, kiwisolver, idna, pooch
* MIT: attrs, charset-normalizer, docstring-parser, fonttools, markdown-it-py, mdurl, platformdirs, pyparsing, QtPy, rich, rich-rst, scooby, six, urllib3
* MIT-CMU: pillow
* Apache-2.0: cyclopts, requests
* Apache-2.0 OR BSD-2-Clause: packaging
* Apache-2.0 / BSD dual license: python-dateutil
* MPL-2.0: certifi
* PSF license: matplotlib, typing-extensions (PSF-2.0)

---

# Notes

* Native SDK archive contents are installed by `CMakeLists.txt` and archived via `repo.toml`; wheel contents are staged
  by `tools/repoman/py_package.py`. Both carry this notice and the product license.
* Repository tools from `deps/repo-deps.packman.xml` are not bundled in the geometry wheel or native SDK archive.
* Preserve the license and embedded notices supplied with each dependency when
  redistributing it. The component entries above distinguish redistributed headers,
  compiled binding code, interpreter dependencies, and optional developer tools.
