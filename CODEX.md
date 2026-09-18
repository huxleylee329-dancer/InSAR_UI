# CODEX.md

This file provides guidance to Codex and OpenAI agents when working with code in this repository.

## Project Overview

**SatExplorer** - A Windows desktop application for InSAR (Interferometric Synthetic Aperture Radar) data processing and visualization. Built with C++ and Qt 5.15.2.

For comprehensive architectural guidelines and workflow rules, refer to [AGENTS.md](AGENTS.md) and [GEMINI.md](GEMINI.md).

## Critical Guidelines

### 1. Build & Execution Rules
- The user builds manually in Visual Studio. **Never attempt to run build commands via CLI/terminal**.
- Project defaults to C++14. Do NOT enable C++17 globally; only ONNX-specific files (`TargetDetection.cpp`, `BatchTargetRecognition.cpp`) use C++17.

### 2. Large File Hashing Prohibition & User Consent (大文件哈希禁令与确认机制)
- **严禁给大文件添加哈希**：严禁在未征得用户明确确认和同意的情况下，对雷达图像矩阵等大文件（如 `.h5`、`.tif`、`.raw`、`.dat` 等几百 MB 到数十 GiB 的大型数据文件）引入全盘逐字节哈希（如 SHA-256、MD5 等）计算与校验逻辑。
- **添加哈希必须取得用户确认**：任何模块、工作流节点或底层 DLL 中，若确有必要添加文件哈希验证，必须先向用户充分说明性能与 IO 开销，在取得用户的明确确认和同意后方可添加。
- **大文件轻量级校验替代**：对于本地大文件的完整性与防篡改验证，必须优先使用 $O(1)$ 的轻量元数据（如 `fileSize` 文件大小 + `lastModified` 时间戳 + 关键 H5/TIFF 元数据属性读取）替代全盘读取哈希，严禁引入导致系统耗时数十分钟的 IO 性能灾难。

### 3. File Editing Safety
- **Never run global `git checkout`**: Do not run `git checkout <file>` to discard changes.
- **Surgical, minimal changes**: Make precise, minimal edits. Avoid rewriting large blocks of code.
- **Chinese Comments**: All code comments must be written in Chinese.
