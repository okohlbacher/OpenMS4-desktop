# OpenMS desktop experiment

This source-complete desktop repository publishes the shared GUI SDK and the
viewer/workflow products from the same source revision. It consumes installed,
pinned OpenMS core and CLI SDKs. It does not add their source directories to its
build.

| Entry point | Output |
| --- | --- |
| `gui/` | `OpenMSGUI` 1.0.0 package, target `OpenMS::GUI` |
| `viewers/` | TOPPView, ImageCreator, INIFileEditor |
| `workflows/` | TOPPAS, ExecutePipeline |
| Repository root | Optional combination of these three builds |

Each entry point accepts installed dependencies through `CMAKE_PREFIX_PATH`.
The repository dependency lock fixes the exact core and CLI versions and source
revisions. A standalone viewer or workflow build additionally verifies that its
installed GUI SDK comes from this desktop repository's current commit. Source
archives must explicitly supply `OPENMS4_SOURCE_REVISION` and
`OPENMS4_SOURCE_DIRTY=ON/OFF`. `OPENMS4_REQUIRE_CLEAN_SOURCE=ON` requires clean
package and SDK sources; pre-build checks reject stale recorded source identity.

## Transitional GUI boundary

The GUI library retains TOPPViewBase, TOPPASBase, INIFileEditorWindow,
QApplicationTOPP, controllers, dialogs and the TOPPAS execution graph. Existing
controllers call these window implementations directly. Keeping them together
prevents a dependency cycle while the executable front ends release separately.
All five front ends and their native application resources have single owners.

The library generates its own export header, MOC/UIC output and Qt resource
archive. Its exported SDK contains public GUI headers; generated `ui_*.h`
headers remain internal to the GUI build and its tests.

Qt 6.7 or newer is required, including Core, Gui, Widgets and Svg; the 3D view renders
through QRhi (Metal, Direct3D, Vulkan or OpenGL, chosen by Qt per platform), whose
semi-public headers come from the GuiPrivate component. This package no longer uses OpenGL
itself; a Qt built with OpenGL support, such as conda-forge's, still needs the GL development
files when it is configured, which is why the Linux CI rows keep them;
macOS additionally links PrintSupport. Core/CLI and all C++ consumers must use compatible compiler,
runtime, architecture and build configuration. Revision checks do not establish
binary ABI compatibility by themselves.

## Tool discovery and resources

Products generate build-tree and installed TSV manifests under
`share/openms4/tools/`. Runtime discovery uses CLI's
`ToolHandler::findExecutable`, including the executable inside a macOS app
bundle. `OPENMS_TOOL_PREFIX_PATH` selects independently installed product
prefixes. Interactive programs use DesktopViewer/DesktopWorkflow categories,
so CLI tool-parameter discovery can omit them while still resolving their
executables. ImageCreator and ExecutePipeline remain discoverable CLI tools.

The stylesheet, Qt icons and sequence-view HTML are compiled into the GUI
library's resource archive. Desktop startup therefore locates its own style
without consulting Core data. Linux desktop/application metadata is installed
by its owning viewer or workflow product. Scientific data, example menus and
documentation lookup still use the pinned Core's data/doc paths.

macOS app icons/plists and Windows icons/resources are preserved. Installs
provide the GUI library and product binaries/bundles; automated collection of
Qt plugins, native dependency bundling, signing, notarization, relocatable
installers and Windows DLL deployment remain acceptance work. A configured
Qt/native runtime is needed to run these developer artifacts.

## Validation

`python3 -m unittest discover -s tests -p 'test_source_boundaries.py'` checks
resource closure, manifest source ownership, GUI include closure, UI inputs,
native application resources, workflow fixtures, resolver migration and import
provenance. It does not configure or compile OpenMS.
The platform CI driver runs this check before configuring the native build.
Provenance retains the original upstream OpenGL filenames and maps them to the
replacement QRhi canvas files in this repository.

When compiled testing is authorized, `BUILD_TESTING=ON` builds five GUI class
tests using the core SDK's optional TestSupport component and its
`OpenMS_TEST_SUPPORT_SOURCE`. `OPENMS_DESKTOP_INTERACTIVE_TESTS=ON` adds TOPPView
and theoretical-spectrum-dialog Qt tests, requiring a suitable display. The
TOPPView test also needs the core example `peakpicker_tutorial_1.mzML`.

Viewers retain two ImageCreator integration cases with exact BMP comparisons.
`OPENMS_DESKTOP_PIPELINE_TESTS=ON` enables the retained ExecutePipeline fixture;
the installed FileInfo and FileMerger tools must be discoverable through the
configured tool prefixes. Broader example workflows and their external search
engines are not automatically enabled.

Later acceptance must build/install GUI, make its source and build directories
unavailable, independently build both products against that installed SDK, run
the class/GUI/pipeline tests, and verify discovery across separate installation
prefixes. No binary correctness or installer readiness is claimed by the source
checks.

## Native developer build

Configure each entry point in a separate build directory using the same Debug
compiler/dependency profile as the installed Core and CLI SDKs. For example, from
this checkout with `OPENMS_SDK_PREFIX` pointing to that shared installation:

```bash
cmake -S gui -B ../desktop-gui-build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$OPENMS_SDK_PREFIX" -DCMAKE_INSTALL_PREFIX="$OPENMS_SDK_PREFIX" \
  -DOPENMS4_REQUIRE_CLEAN_SOURCE=ON -DBUILD_TESTING=ON
cmake --build ../desktop-gui-build --parallel 3
QT_QPA_PLATFORM=offscreen ctest --test-dir ../desktop-gui-build --output-on-failure
cmake --install ../desktop-gui-build
```

Use out-of-tree build directories outside the Git checkout when requiring clean
sources. Repeat with `-S viewers` or `-S workflows` and a new build directory after
installing GUI. The installed GUI must match this checkout's commit. Provide the
same native dependency prefixes/curl discovery flags used for Core when necessary.
Current native results and remaining platform/product gates are recorded in the
superproject implementation validation report; the commands alone do not establish
acceptance. Interactive tests need their own runs.

The Mac Studio CI runner additionally runs `Plot3DRhiCanvas_test` with the Cocoa
display backend and requires it to render a nonempty, multicolour frame. Failure
to render fails that job; the PNG and test log are uploaded with its results.
Other runners retain the headless tests. The CI driver enables this gate through
`--render-test` or `OPENMS4_QRHI_RENDER_TEST=1`, requiring a logged-in macOS display
session and the same native dependencies as the build.

<!-- package-graph:begin -->
## Where this package sits

![OpenMS 4 package architecture](docs/package-architecture.svg)

`desktop` builds against the installed **core**, **cli**, **test-data** packages at the revisions recorded in [`dependencies.lock.json`](dependencies.lock.json). No other package builds against it.

| Repository | Relation | Contents |
| --- | --- | --- |
| [OpenMS4-core](https://github.com/okohlbacher/OpenMS4-core) | dependency | scientific library, OpenSwathAlgo, readers and writers, runtime data, optional TestSupport |
| [OpenMS4-cli](https://github.com/okohlbacher/OpenMS4-cli) | dependency | TOPPBase, tool registration and discovery |
| [OpenMS4-test-data](https://github.com/okohlbacher/OpenMS4-test-data) | dependency | versioned fixtures and the installed numerical suite |

The eighteen repositories are assembled by the parent repository
[OpenMS4-tests](https://github.com/okohlbacher/OpenMS4-tests), which holds the submodule pins (`packages.lock.json`), the
dependency-order build runner and the contract tests that keep the graph consistent.
[`docs/project-state.md`](https://github.com/okohlbacher/OpenMS4-tests/blob/codex/package-split/docs/project-state.md) is the current state
of the whole project; [`docs/build-split-packages.md`](https://github.com/okohlbacher/OpenMS4-tests/blob/codex/package-split/docs/build-split-packages.md)
reproduces the installed-SDK build.
<!-- package-graph:end -->
