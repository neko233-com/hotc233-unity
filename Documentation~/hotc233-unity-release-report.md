# hotc233-unity Release Report

Generated: 2026-06-13 CST

## Core Summary

This package is now published as **hotc233-unity**:

- GitHub repository: `https://github.com/neko233-com/hotc233-unity`
- Unity package name: `com.neko233.hotc233-unity`
- Display name: `hotc233-unity`
- Runtime API namespace remains `Hotc233` for compatibility.

## Included In This Package Commit

- Runtime loader API for loading AOT metadata and hot update assemblies from `byte[]`.
- Editor generation pipeline for CompileDll, Generate/All, link.xml, stripped AOT DLLs, MethodBridge, ReversePInvokeWrapper, and AOTGenericReference.
- Built-in `Data~/Libil2cpp` runtime source, so the package does not depend on a HybridCLR-style external installer.
- Persistent log language selection:
  - `hotc233/Language/Auto Detect`
  - `hotc233/Language/Chinese`
  - `hotc233/Language/English`
- Scene-save guard before `Generate/All`, preventing Unity/Tuanjie from hanging on dirty scene save prompts.
- Export menu for partial `.unitypackage` snapshots, with warnings that `Data~` is not included by Unity's package exporter.

## Distribution Rule

Use this Git repository or a UPM/local package checkout as the canonical distribution format. Do not distribute only a `.unitypackage` when the receiver needs the full runtime data, because Unity's `.unitypackage` export skips `Data~` and `Documentation~`.

## Verification Note

The parent demo repository has already verified Windows / Android / iOS / WebGL IL2CPP generation and produced hotc233 / Mono / IL2CPP performance comparison reports. This package repository contains the reusable runtime/editor package portion of that work.
