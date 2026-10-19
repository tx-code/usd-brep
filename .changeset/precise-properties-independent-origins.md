---
change: feature
---

`SmFace::ComputePreciseProperties` accepts independent area and volume reference origins in a new selected-group overload, allowing local face centroids and common-frame volume moments in one integration.

## Scope

Maintenance

## Integrator action

None. Rebuild the SDK before using the new overload.

## Compatibility

Additive nonvirtual C++ overload; existing signatures, object layout, same-origin calculations, accuracy control, retry policy and failure behavior remain unchanged. Area moments use the area origin and volume moments use the volume origin; callers must interpret each group in its documented frame.
