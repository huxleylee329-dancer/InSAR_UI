import os
import subprocess
import shutil
from pathlib import Path

import argparse

parser = argparse.ArgumentParser(description="Build and run real-DLL ExportKMLWorker regression tests")
parser.add_argument('--qt-root', required=True, type=Path)
parser.add_argument('--sdk-root', required=True, type=Path)
parser.add_argument('--vs-root', required=True, type=Path)
parser.add_argument('--out', required=True, type=Path)
parser.add_argument('--core-root', type=Path)
parser.add_argument('--toolset-version', default='14.44')
parser.add_argument('--baseline-source', type=Path)
parser.add_argument('--baseline-header', type=Path)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
core = (args.core_root or repo.parent/'InSAR0629').resolve()
sdk = args.sdk_root.resolve()
qt = args.qt_root.resolve()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
env = {k.upper(): v for k, v in os.environ.items()}
devcmd = args.vs_root.resolve()/'Common7/Tools/VsDevCmd.bat'
command = f'cmd /d /s /c ""{devcmd}" -arch=x64 -host_arch=x64 -vcvars_ver={args.toolset_version} >nul && set"'
for line in subprocess.check_output(command, env=env).decode('mbcs').splitlines():
    if '=' in line:
        key, value = line.split('=', 1)
        env[key.upper()] = value
includes = [repo, repo/'include', repo/'include/QtNodes/internal', repo/'include/ADS', core/'include',
            sdk/'opencv/build/include', sdk/'HDF5-1.8.22/include',
            sdk/'release-1928-x64-gdal-3-3-1-mapserver-7-6-4-libs/include', qt/'include']
includes += [p for p in (qt/'include').iterdir() if p.is_dir() and p.name.startswith('Qt')]
# Compile the exact production HDF5 helpers without unrelated MainWindow/XML dependencies.
# No HDF5 behavior is mocked: the extracted bodies call the installed production DLLs.
node_source = (repo/'NodeUtils.cpp').read_text(encoding='utf-8-sig')
def section(start, end):
    return node_source[node_source.index(start):node_source.index(end, node_source.index(start))]
node_slice = out/'NodeUtilsReadSlice.cpp'
globals_start = node_source.index('static QMutex g_hdf5GlobalMutex')
globals_end = node_source.index(';', node_source.index('static QMap<QString, std::shared_ptr<QMutex>> g_fileLocks', globals_start)) + 1
node_slice.write_text('''#include "NodeUtils.h"
#include "FormatConversion.h"
#include "Hdf5IO.h"
#include <QFileInfo>
#include <QDir>
#include <QMutexLocker>
#include <memory>
namespace NodeUtils {
''' + node_source[globals_start:globals_end] + '\n'
    + section('QMutex* getHdf5Mutex()', 'IApplicationInterface* getProjectContext')
    + section('bool readMatFromH5(', 'bool readScalarFromH5(const QString& filePath, const QString& dataset, double&')
    + '\n}\n', encoding='utf-8-sig')
sources = [repo/'ExportKMLWorker.cpp', repo/'BaseWorker.cpp', node_slice, repo/'InSARLogManager.cpp',
           repo/'diagnostics/export_kml/ExportKMLRegression.cpp']
for name in ['BaseWorker', 'ExportKMLWorker', 'InSARLogManager']:
    generated = out / ('moc_' + name + '.cpp')
    subprocess.run([str(qt/'bin/moc.exe'), str(repo/'include'/(name+'.h')), '-o', str(generated)], check=True, env=env)
    sources.append(generated)
flags = ['/nologo', '/c', '/MD', '/O1', '/Gy', '/Gw', '/Zc:inline', '/EHsc', '/std:c++14', '/utf-8', '/DNDEBUG',
         '/DNODE_EDITOR_STATIC', '/DADS_STATIC', '/D_CRT_SECURE_NO_WARNINGS']
flags += ['/I"'+str(p)+'"' for p in includes]
compile_flags = list(flags)
flags += ['"'+str(p)+'"' for p in sources]
(out/'compile.rsp').write_text('\n'.join(flags), encoding='utf-8-sig')
with (out/'build.log').open('w') as log:
    result = subprocess.run([shutil.which('cl.exe', path=env['PATH']), '@compile.rsp'], cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode: raise SystemExit(result.returncode)
    paths = [core/'bin', repo/'bin', qt/'lib', sdk/'opencv/build/x64/vc15/lib',
             sdk/'release-1928-x64-gdal-3-3-1-mapserver-7-6-4-libs/lib']
    flags = ['/NOLOGO', '/SUBSYSTEM:CONSOLE', '/OPT:REF', '/OPT:ICF', '/OUT:ExportKMLRegression.exe']
    flags += ['/LIBPATH:"'+str(p)+'"' for p in paths]
    flags += ['"'+str(p.with_suffix('.obj').name)+'"' for p in sources]
    flags += ['Qt5Core.lib', 'Qt5Gui.lib', 'Qt5Widgets.lib', 'Qt5Network.lib', 'Qt5Charts.lib',
              'FormatConversion.lib', 'Hdf5IO.lib', 'Utils.lib', 'ComplexMat.lib', 'gdal_i.lib', 'opencv_world450.lib']
    (out/'link.rsp').write_text('\n'.join(flags), encoding='utf-8-sig')
    result = subprocess.run([shutil.which('link.exe', path=env['PATH']), '@link.rsp'], cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT)
print('Regression build exit:', result.returncode)
if result.returncode: raise SystemExit(result.returncode)
env['PATH'] = ';'.join(map(str, [core/'bin', repo/'bin', qt/'bin', sdk/'opencv/build/x64/vc15/bin'])) + ';' + env['PATH']
env['QT_QPA_PLATFORM_PLUGIN_PATH'] = str(qt/'plugins/platforms')
with (out/'test.log').open('w') as log:
    result = subprocess.run([str(out/'ExportKMLRegression.exe')], cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT)
print('Regression test exit:', result.returncode)
if result.returncode: raise SystemExit(result.returncode)

if args.baseline_source or args.baseline_header:
    if not (args.baseline_source and args.baseline_header):
        parser.error('baseline source and header must be supplied together')
    baseline = out/'baseline'
    baseline.mkdir(exist_ok=True)
    shutil.copyfile(args.baseline_source, baseline/'ExportKMLWorker.cpp')
    shutil.copyfile(args.baseline_header, baseline/'ExportKMLWorker.h')
    baseline_flags = compile_flags + ['/Fo:ExportKMLWorker.before.obj', '"'+str(baseline/'ExportKMLWorker.cpp')+'"']
    (out/'baseline-compile.rsp').write_text('\n'.join(baseline_flags), encoding='utf-8-sig')
    baseline_link = (out/'link.rsp').read_text(encoding='utf-8-sig').replace('"ExportKMLWorker.obj"', '"ExportKMLWorker.before.obj"').replace('/OUT:ExportKMLRegression.exe', '/OUT:ExportKMLBefore.exe')
    (out/'baseline-link.rsp').write_text(baseline_link, encoding='utf-8-sig')
    with (out/'baseline-build.log').open('w') as log:
        subprocess.run([shutil.which('cl.exe', path=env['PATH']), '@baseline-compile.rsp'], cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([shutil.which('link.exe', path=env['PATH']), '@baseline-link.rsp'], cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (out/'baseline-test.log').open('w') as log:
        before = subprocess.run([str(out/'ExportKMLBefore.exe'), '--missing-lat'], cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=60)
        after = subprocess.run([str(out/'ExportKMLRegression.exe'), '--missing-lat'], cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=60)
        log.write(f'Before exit: 0x{before.returncode & 0xffffffff:08X}\nAfter exit: {after.returncode}\n')
    print(f'Before exit: 0x{before.returncode & 0xffffffff:08X}; after exit: {after.returncode}')
    if (before.returncode & 0xffffffff) != 0xC0000005 or after.returncode != 0:
        raise SystemExit('Baseline comparison did not match the expected access violation/fix')
