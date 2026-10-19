# Independent linked wireframe control

Public synthetic AP214 carrier. The body is the existing P04 box
`[2,12] × [-3,17] × [5,35]` mm: 8 vertices, 12 edges, 6 faces, volume 6000 mm³.
Root #900 is a placement-only representation with distinct SRR links to body #20
and wireframe #920. Its placement is not a transform or Boolean composition.

Curve set #910 contains point #911=(7,8,9) mm, finite line #912 from (0,0,0) to
(5,0,0) mm, and quarter-circle #940 on radius 2 mm from (2,0,0) to (0,2,0).
The line is trimmed with Cartesian points; the circle is trimmed with radian
parameters 0 and binary64 decimal π/2. Both source senses are positive.
The circle's rational-parameter midpoint is (√2,√2,0), without asserting a
linear relation between native parameters and source angles.

The point is inside the box, at material-boundary distance 4 mm, and at wireframe
distance zero. The set bounds are (0,0,0) to (7,8,9) mm. Source geometry is
independent of the body; the expected native part contains two detached curves,
the material BRep and one isolated-point archive adapter BRep. The adapter is
not a source VERTEX_POINT. Tests add a distinct coincident point and a second
set referencing existing definitions to check identity/ownership independently.
