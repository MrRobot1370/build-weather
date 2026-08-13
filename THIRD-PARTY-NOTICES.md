# Third-party notices

Build Weather itself is MIT licensed; see [LICENSE](LICENSE). It uses the
following third-party components, each under its own terms.

## Vendored in this repository

| Component | Version | Licence | Location |
| --- | --- | --- | --- |
| [spdlog](https://github.com/gabime/spdlog) | 1.14.1 | MIT | `3rdparty/spdlog/`, licence at `3rdparty/spdlog/LICENSE` |
| [fmt](https://github.com/fmtlib/fmt) | bundled inside spdlog | MIT | `3rdparty/spdlog/include/spdlog/fmt/bundled/`, licence at `.../fmt.license.rst` |

spdlog is used in header-only mode (`SPDLOG_HEADER_ONLY=1`), so no spdlog
source is compiled separately and no spdlog binary is redistributed.

## Required at build and run time, not redistributed in this repository

**Qt 6** (Qt Group). Build Weather links Qt 6 dynamically and does not bundle
it. Qt is available under the LGPL v3, the GPL, and a commercial licence; the
terms you build under are the ones you obtained Qt under. If you redistribute
a built copy of Build Weather together with the Qt libraries, the obligations
of your Qt licence apply to that distribution, including the LGPL's
requirement that the user be able to relink against a modified Qt. Building
from source for your own use, which is what this repository is set up for,
raises none of that.

Nothing in this project modifies Qt.

## Data this project reads

`.ninja_log` and `-ftime-trace` documents are outputs of
[ninja](https://ninja-build.org/) and [Clang](https://clang.llvm.org/)
respectively. Build Weather only reads them; neither project's code is used
here.
