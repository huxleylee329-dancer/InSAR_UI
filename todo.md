# InSAR_UI (SatExplorer) TODO List

## Technical Debt & Future Fixes

### 1. `FormatConversion.dll` TinyXML Destructor Crash (Memory Leak Workaround)

**Status:** Workaround Implemented (Intentional Leak)  
**Affected Files:** 
- **All UI and Worker task files** (e.g., `GenericSARImportTask.cpp`, `ClutterSuppression.cpp`, `treeview.cpp`, `MyThread.cpp`, `import_sentinel.cpp`, etc.)

**Background:**
During the refactoring of `MyThread` to the new `QRunnable` Task architecture, a critical bug in `FormatConversion_d.dll` was exposed. The `XMLFile` destructor (`~XMLFile()`) triggers a TinyXML assertion failure (`Expression: sentinel.prev == &sentinel` at `tinyxml.cpp` Line 1510) due to memory corruption. 

Because `SatExplorer.exe` compiles its own copy of `tinyxml.cpp` while `FormatConversion.dll` internally uses its own, the `TiXmlDocument` state diverges across the CRT/Heap boundary. Calling `~XMLFile()` inside the DLL via the EXE triggers this assertion. Furthermore, reusing the same `XMLFile` instance to call `LoadFile()` multiple times (e.g., in a `for` loop for Batch Import) triggers `doc.Clear()` internally inside the DLL, which also results in the exact same crash.

**Current Workaround:**
To prevent the application from crashing while using the existing DLL, we applied an "以毒攻毒" (fight fire with fire) approach globally across the codebase. We reverted all memory management of `XMLFile` to raw pointers without deletion:
```cpp
XMLFile* xml = new XMLFile(); // Intentional leak
```
This intentionally leaks ~2KB of heap memory per instance, bypassing the DLL destruction phase and successfully avoiding the crash. Additionally, for batch processing tasks, `new XMLFile()` is now explicitly placed **inside** the loop to ensure a fresh, uncorrupted object is used per iteration.

**Future Action Required:**
When `FormatConversion.dll` is fixed and recompiled (specifically aligning the TinyXML configuration between EXE/DLL and fixing the attribute memory corruption during `XMLFile_add_origin`):
1. Search the codebase for `new XMLFile()` across all `.cpp` files.
2. For stack-based usages, revert to standard stack allocation:
   ```cpp
   XMLFile xml;
   ```
3. For task-based heap usages, restore proper memory management using `std::unique_ptr`:
   ```cpp
   std::unique_ptr<XMLFile> localXml(new XMLFile());
   XMLFile* DOC = localXml.get();
   ```
4. Test thoroughly (especially "Delete Node", "New Project", and "Batch Import") to ensure no assertions are thrown when the objects are properly cleaned up.
