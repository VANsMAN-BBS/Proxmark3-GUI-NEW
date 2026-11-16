# build/

Local build trees only. Nothing in this directory is tracked by Git.

```text
build/
├── qt515-mingw64/
│   ├── debug/        scripts/build.ps1
│   ├── release/      scripts/build.ps1 -Configuration Release
│   └── tests/        scripts/test.ps1
└── legacy/           old Stage 0-8 build directories (safe to remove)
```

Rules:

- One directory per configuration; Debug and Release never share objects.
- Test binaries live under `tests/<suite>/`.
- Use qmake shadow builds; never build inside `src/`.
- `scripts/clean-build.ps1` removes `qt515-mingw64/*`; `-All` also removes
  `legacy/`. Published files under `artifacts/publish/` are never touched.
