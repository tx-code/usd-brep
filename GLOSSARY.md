# Boundary representation geometry

## Planar geometry

**Planar region**: A portion of a plane selected by closed boundaries, with
included interiors and excluded holes. It is independent of its application.
_Avoid_: CAM region (when referring to the general geometry).

**Boundary loop**: A closed traversal of curves delimiting a planar region or
one of its holes.

**Hole**: An excluded interior enclosed directly by a material boundary loop.

**Island**: An included region nested inside a hole; it can contain its own holes.

**Projected linework**: Curves visible from a projection direction, which may
include internal contours and creases as well as exterior boundaries.
_Avoid_: Footprint (unless the intended material boundary has been determined).
