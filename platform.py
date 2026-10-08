# Copyright (c) Synwit platform maintainers
# Licensed under the Apache License, Version 2.0
#
# Platform-level glue for platform-synwit. Two responsibilities only:
#   1. pick the framework-synwit package (local checkout wins over registry)
#   2. resolve per-series debug information (SVD) coming from that package

import os

from platformio.managers.platform import PlatformBase


def _read_json(path):
    import json

    try:
        with open(path, "r", encoding="utf-8") as fp:
            return json.load(fp)
    except Exception:  # pylint: disable=broad-except
        return None


class SynwitPlatform(PlatformBase):

    FRAMEWORK_PKG = "framework-synwit"

    def configure_default_packages(self, variables, targets):
        self._use_local_framework(variables)

        # drop the J-Link uploader when no one asked for it (same trick as
        # platform-ststm32) so we do not download ~40MB for nothing
        packageivars = [variables.get(option, "") for option in
                        ("upload_protocol", "debug_tool")]
        board = variables.get("board")
        if board:
            board_config = self.board_config(board)
            packageivars.append(board_config.get("upload.protocol", ""))
            packageivars.extend(board_config.get("debug.default_tools", []))
        if "tool-jlink" in self.packages and not any(
            "jlink" in str(f) for f in packageivars
        ):
            del self.packages["tool-jlink"]

        return PlatformBase.configure_default_packages(
            self, variables, targets)

    def _use_local_framework(self, variables):
        """Prefer a `framework-synwit` folder living next to this platform.

        This makes `pio pkg install --global symlink://<platform dir>` work
        straight away: the framework is symlinked the same way and edits are
        picked up immediately. When the sibling folder is absent (i.e. the
        platform was installed from the registry) the version declared in
        platform.json is used as-is.

        Can always be overridden per project with:
            platform_packages = framework-synwit@symlink:///abs/path
        """
        pkg = self.packages.get(self.FRAMEWORK_PKG)
        if not pkg:
            return
        if variables.get("platform_packages") and any(
            str(spec).startswith(self.FRAMEWORK_PKG)
            for spec in variables["platform_packages"]
        ):
            return

        here = os.path.realpath(self.get_dir())
        sibling = os.path.join(os.path.dirname(here), self.FRAMEWORK_PKG)
        manifest = os.path.join(sibling, "package.json")
        if os.path.isfile(manifest):
            pkg["version"] = "symlink://" + sibling

    def get_boards(self, id_=None):
        result = PlatformBase.get_boards(self, id_)
        if not result:
            return result
        if id_:
            return self._add_series_debug(result)
        for key, value in result.items():
            result[key] = self._add_series_debug(value)
        return result

    def _add_series_debug(self, board):
        """Point `debug.svd_path` at the SVD shipped inside framework-synwit."""
        debug = board.manifest.get("debug", {})
        svd = debug.get("svd_path")
        if not svd or os.path.isabs(svd):
            return board

        pkg_dir = self.get_package_dir(self.FRAMEWORK_PKG)
        if pkg_dir:
            candidate = os.path.join(pkg_dir, "svd", svd)
            if os.path.isfile(candidate):
                debug["svd_path"] = candidate
                board.manifest["debug"] = debug
                return board

        # fallback: platform local copy
        candidate = os.path.join(self.get_dir(), "misc", "svd", svd)
        if os.path.isfile(candidate):
            debug["svd_path"] = candidate
            board.manifest["debug"] = debug
        return board
