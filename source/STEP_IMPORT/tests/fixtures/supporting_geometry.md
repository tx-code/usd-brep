# Public synthetic supporting geometry truth

The public analytic box is [2,12] × [-3,17] × [5,35] mm; its material volume
is 6000 mm³, topology6faces/12edges/8vertices. The point(7,8,9) is inside and
4mm from the material boundary. It is also a separate supporting point with
its original source identity, not a source vertex of that body.

One CGRR directs the selected wrapper to CGR#960. Its six direct members are
frames#904/#970, plane#972, point#911, trimmed line#912 and arc#940. Frame#970
has origin(30,40,50), X=(0,1,0),Y=(0,0,1),Z=(1,0,0). The plane is unbounded:
P(u,v)=(30,40+u,50+v). Thus P(2,3)=(30,42,53); projecting(35,42,53) gives that
point at distance5mm. No application bounds or frame adapters create trims.

The independent line is(0,0,0)→(5,0,0). The radius2 quarter circle goes from
(2,0,0) to(0,2,0), with midpoint(√2,√2,0). Its native rational parameter is
not a STEP angle. All source numbers/units/senses and native mappings are
retained. A centimetre variant scales positions and radii10×, not unit axes.

C++ controls use these analytic equations, owning views after model release,
body-only queries/copy, complete archive readback, every extra native-call
limit/cancel and each final publication boundary. Python source controls test
directed context/endpoint clauses, exactly-one fixed category (including FACE
and FACE_SURFACE overlap), missing/duplicate links, AP214-only scope and exact
visits. Native controls include shared source definitions across separate
wireframe/support roles and rejection of an unbounded line before native calls.

These are public project subset controls, not a complete schema witness,
production quality calibration or a continuous bound for arbitrary geometry.
