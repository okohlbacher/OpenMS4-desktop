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
archives must supply `OPENMS4_SOURCE_REVISION`.

## Transitional GUI boundary

The GUI library retains TOPPViewBase, TOPPASBase, INIFileEditorWindow,
QApplicationTOPP, controllers, dialogs and the TOPPAS execution graph. Existing
controllers call these window implementations directly. Keeping them together
prevents a dependency cycle while the executable front ends release separately.
All five front ends and their native application resources have single owners.

The library generates its own export header, MOC/UIC output and Qt resource
archive. Its exported SDK contains public GUI headers; generated `ui_*.h`
headers remain internal to the GUI build and its tests.

Qt 6.1 or newer is required, including Core, Gui, Widgets, Svg and OpenGLWidgets;
macOS additionally links PrintSupport. `OPENMS_GUI_WEBENGINE=OFF` disables optional
WebEngine views. Core/CLI and all C++ consumers must use compatible compiler,
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

Core data remains a compatibility dependency: GUI startup reads
`GUISTYLE/qtStyleSheet.qss`, example menus use core examples, and documentation
lookup uses the core's data/doc paths. Required data must accompany the pinned
core runtime. Qt resource icons and sequence-view HTML are compiled into the
GUI library. This repository does not duplicate the core data bundle.

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
