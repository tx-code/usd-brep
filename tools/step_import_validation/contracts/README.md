# Frozen design inputs

`design/v1/` holds the ten original documents consumed by the AP203e2 planar
fixture generator and its independent reference anchors. They are test data,
preserved byte for byte, including historical wording, paths, and relative links.
Their recorded stage does not describe current implementation or authorization.

`common.DESIGN_DIRECTORY` owns their repository location. Newly generated
`sources.lock.json` records that location and the actual source hashes. Existing
corpus/evidence packages retain their original recorded paths and identities;
offline verification does not rewrite them or require their old source checkout.

Change fixture definitions through an explicit corpus revision, rather than
editing these files as current documentation. The supported behavior belongs in
the component READMEs; the long-term design is in
[the STEP contract](../../../docs/design/step-import.md).
