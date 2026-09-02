# TOPS Edge Probe

`TopsEdgeProbe` is a read-only console diagnostic for the B3-to-B4 boundary.
It uses the production `Deflat::computeSentinel1FlatEarthPhaseV5` kernel with
the complete registered source-row map to prepare the orbit exactly as the
full six-burst run does. It then evaluates only the requested rows and
columns. No SLC samples are read and no H5 product is written.

Build the probe itself from a Visual Studio Developer PowerShell:

```powershell
msbuild .\diagnostics\sentinel1_tops_fep\TopsEdgeProbe\TopsEdgeProbe.vcxproj /m /p:Configuration=Debug /p:Platform=x64
```

This project links the existing Debug Core import libraries. It does not add a
Core project reference, so it does not request a Core rebuild. The required
`Deflat_d.dll` and `Hdf5IO_d.dll` are already expected in `bin` beside the
probe executable.

Run one diagnostic invocation:

```powershell
.\bin\TopsEdgeProbe_d.exe `
  --master D:\test\4Benchmark\S1_Batch_Import_Orbit_regis\20160815_iw3vv_regis.h5 `
  --slave D:\test\4Benchmark\S1_Batch_Import_Orbit_regis\20160827_iw3vv_regis.h5 `
  --out D:\test\4Benchmark\tops_edge_probe_b3_b4_20260902.json
```

The default rows are source-row `4459` (B3 last retained line) and `4633`
(B4 first retained line). The default columns are `256`, `12478`, and
`24700`. `--rows` and `--columns` accept comma-separated non-negative integer
lists. The output path is refused when it already exists unless `--overwrite`
is supplied.

The JSON reports both `fine_v2` and `fine_lagrange` for every point, together
with their direct deltas. Each strategy records the master time/state, RDE
point, master range, slave mapped coordinate, seed and solved zero-Doppler
time/state, slave range, FEP, and the two closure bounds.

`consecutive_source_row_fep_deltas` also gives the direct FEP change between
each consecutive pair in `--rows`; with the defaults, this is the B4 minus B3
boundary delta at every requested column for both strategies.
