
set_xmakever("3.0.0")

-- Project
set_project("UniversalWeightScanner")
set_version("1.1.0")
set_languages("c++23")

-- Build modes
add_rules("mode.debug", "mode.release")

-- CommonLibSSE-NG by alandtse
includes("lib/commonlibsse-ng-alandtse")

-- SKSE Plugin
target("UniversalWeightScanner")
    add_rules("commonlibsse-ng.plugin", {
        name = "UniversalWeightScanner",
        author = "Alduin",
        description = "Universal item weight patcher for Skyrim Special Edition"
    })

    -- Source files
    add_files("UniversalWeightScanner/main.cpp")
