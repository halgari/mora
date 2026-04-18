-- Local xmake-repo override tree.
--
-- Packages defined here take precedence over the same names in the
-- upstream xmake-repo. Currently used to patch Apache Arrow 7.0.0's
-- bundled xsimd dep so it builds under CMake 4.x (xsimd's upstream
-- `cmake_minimum_required(VERSION 3.1)` is rejected by CMake 4+).
--
-- Activated from the root xmake.lua:
--   add_repositories("mora-local scripts/xmake-repo")
-- which must appear before any `add_requires(...)` that names a
-- package we override.

set_xmakever("2.8.0")
