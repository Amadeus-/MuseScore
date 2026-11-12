# Reprise fork-local build flags. Kept OUT of the upstream CMake files so it never
# conflicts on an `+Update (from upstream)+` merge; wired in by a single include()
# line in the top-level CMakeLists.txt (search that file for "RepriseBuildFlags").
#
# Purpose: silence the C4702 "unreachable code" flood that Qt 6.11 headers emit under
# /W4 (e.g. Qt/.../QtCore/qvariant.h(723)). We scope the suppression to EXTERNAL
# (angle-bracket) headers only, so our OWN code still gets C4702 and the fork keeps its
# zero-warnings discipline -- preferred over a blanket /wd4702. Qt and other third-party
# headers are included with <>, our own headers with "", so only vendor headers go quiet.
#
# /external:anglebrackets - treat every <>-included header as an external header
# /external:W0            - compile external headers at warning level 0
# /external is a stable MSVC feature since VS 2019 16.10 (no /experimental needed).

if (MSVC)
    add_compile_options(/external:anglebrackets /external:W0)

    # Emit a SEPARATE .pdb for Release too (symbols live in the .pdb, NOT embedded in the
    # exe/dll) so any crash can be symbolized. Debug / RelWithDebInfo already produce PDBs;
    # stock Release does not. Use /Z7 (debug info carried IN the .obj files -- which are
    # intermediate and never shipped) rather than /Zi: /Zi needs a shared compiler .pdb that
    # collides with the precompiled headers under the /MP parallel build (error C2859
    # "pdb ... is not the pdb file that was used when this precompiled header was created").
    # /DEBUG makes the linker harvest the /Z7 debug info into the final target .pdb; the
    # shipped exe/dll stays lean. /OPT:REF + /OPT:ICF restore the Release size optimizations
    # that a bare /DEBUG would otherwise disable.
    add_compile_options($<$<CONFIG:Release>:/Z7>)
    add_link_options($<$<CONFIG:Release>:/DEBUG>)
    add_link_options($<$<CONFIG:Release>:/OPT:REF>)
    add_link_options($<$<CONFIG:Release>:/OPT:ICF>)
endif()
