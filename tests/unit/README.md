# Unit tests

The directory layout mirrors the public components. Every test includes an
`api/*.h` header and is registered with both `unit` and component CTest labels.

Run all unit tests:

```bash
ctest --test-dir build -L unit --output-on-failure
```

Run one component, for example PTA:

```bash
ctest --test-dir build -L pta --output-on-failure
```

Available component labels are `adapters`, `cfg`, `cdg`, `pta`, `cg`, and
`ddg`.
