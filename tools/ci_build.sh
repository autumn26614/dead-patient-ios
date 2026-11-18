#!/usr/bin/env bash
# The macOS build: host comparison tests, the original asset package, the
# device IPA and the simulator smoke test. Run from the repository root on a
# macOS runner. Used by .github/workflows/ios-arm64.yml (private repository)
# and by the public repository's build workflow, which checks this repository
# out first.
#
# Environment:
#   ASSET_REPO   owner/name of the repository whose release holds the assets
#   GH_TOKEN     a token that can read that repository
#   CI_QUIET=1   send each step's output to build/ci.log and print only the
#                step names and their result (public logs stay free of game
#                file names and text)
#   CI_SKIP_SIMULATOR=1  skip the simulator build and smoke test
set -eo pipefail

mkdir -p build/ios
: > build/ci.log

# Each step runs in its own bash with errexit and pipefail (a function called
# in a `||` list would otherwise run with errexit disabled and hide failures).
step() {
    local name="$1"
    shift
    echo "::group::$name"
    local status=0
    if [ "${CI_QUIET:-0}" = "1" ]; then
        { echo "=== $name"; bash -eo pipefail -c "$*"; } >> build/ci.log 2>&1 || status=$?
    else
        bash -eo pipefail -c "$*" 2>&1 | tee -a build/ci.log || status=$?
    fi
    echo "::endgroup::"
    if [ "$status" -ne 0 ]; then
        echo "FAIL: $name (exit $status)"
        exit "$status"
    fi
    echo "PASS: $name"
}

host_tests() {
    cp direct/runtime/*.hpp direct/ios/hspproj/hsp3embed/
    xcrun clang++ -std=c++14 -Idirect/runtime direct/tests/legacy_memory_test.cpp -o build/ios/legacy-memory-test
    build/ios/legacy-memory-test
    xcrun clang++ -std=c++14 -DHSPIOS -DHSP64 -Wno-write-strings -Idirect/runtime -Idirect/ios/hspproj/hsp3 direct/tests/legacy_memory_runtime_test.cpp direct/ios/hspproj/hsp3/hspvar_core.cpp direct/ios/hspproj/hsp3/hspvar_int.cpp direct/ios/hspproj/hsp3/hspvar_double.cpp direct/ios/hspproj/hsp3/strbuf.cpp -o build/ios/legacy-memory-runtime-test
    build/ios/legacy-memory-runtime-test
    xcrun clang++ -std=c++14 -Idirect/runtime direct/tests/native_loader_test.cpp -o build/ios/native-loader-test
    build/ios/native-loader-test
    xcrun clang++ -std=c++14 -DHSPIOS -DHSP64 -Idirect/runtime -Idirect/ios/hspproj/hsp3embed direct/tests/function_info_test.cpp -o build/ios/function-info-test
    build/ios/function-info-test
    xcrun clang++ -std=c++14 -Wall -Wextra -Werror -Idirect/runtime direct/tests/screen_compat_test.cpp -o build/ios/screen-compat-test
    build/ios/screen-compat-test
    xcrun clang++ -std=c++14 -Wall -Wextra -Werror -Idirect/runtime direct/tests/win32_compat_test.cpp -o build/ios/win32-compat-test
    build/ios/win32-compat-test
    xcrun clang++ -std=c++14 -Wall -Wextra -Werror -Idirect/runtime direct/tests/zgp_math_test.cpp -o build/ios/zgp-math-test
    xcrun clang++ -std=c++14 -Wall -Wextra -Werror -Idirect/runtime direct/tests/input_state_test.cpp -o build/ios/input-state-test
    build/ios/input-state-test
    xcrun clang++ -std=c++14 -Wall -Wextra -Werror -Idirect/runtime direct/tests/touch_controls_test.cpp -o build/ios/touch-controls-test
    build/ios/touch-controls-test
    xcrun clang++ -std=c++14 -Wall -Wextra -Werror -Wno-unused-function -Idirect/runtime direct/tests/buffer_surface_test.cpp -o build/ios/buffer-surface-test
    build/ios/buffer-surface-test direct/tests/fixtures/buffer_surface.reference direct/tests/fixtures
    xcrun clang++ -std=c++14 -Wall -Wextra -Werror -Idirect/runtime -Idirect/tests direct/tests/fcpoly_test.cpp -o build/ios/fcpoly-test
    build/ios/fcpoly-test direct/tests/fixtures/fcpoly.reference
    build/ios/zgp-math-test
    mkdir -p build/asset-files-test
    xcrun clang++ -std=c++14 -Wall -Wextra -Werror -Idirect/runtime direct/tests/asset_files_test.cpp -o build/ios/asset-files-test
    build/ios/asset-files-test build/asset-files-test
    xcrun clang++ -std=c++14 -O2 -Wall -Wextra -Werror -Idirect/runtime direct/tests/csmesh_test.cpp -o build/ios/csmesh-test
    build/ios/csmesh-test
    xcrun clang++ -std=c++14 -O2 -Wall -Wextra -Werror -Idirect/runtime direct/tests/dsound_test.cpp -o build/ios/dsound-test
    build/ios/dsound-test
    xcrun clang++ -std=c++14 -Wall -Wextra -Werror -Idirect/runtime direct/tests/zgp_state_test.cpp -o build/ios/zgp-state-test
    build/ios/zgp-state-test
    xcrun clang++ -std=c++14 -O2 -Wall -Wextra -Werror -Idirect/runtime direct/tests/zgp_trace_test.cpp -o build/ios/zgp-trace-test
    xcrun clang++ -std=c++14 -O2 -Wall -Wextra -Werror -Idirect/runtime direct/tests/d3dx_mesh_test.cpp -o build/ios/d3dx-mesh-test
    xcrun clang++ -std=c++14 -O2 -Wall -Wextra -Werror -Idirect/runtime -Idirect/tests direct/tests/hspda_test.cpp -o build/ios/hspda-test
    for s in startup camera frame object objlate; do
        build/ios/zgp-trace-test "direct/tests/zgp/$s.script" "direct/tests/zgp/$s.reference"
    done
    python3 -m unittest discover -s direct/tests -p 'asset_package_test.py'
}

fetch_assets() {
    python3 tools/fetch_game_assets.py --repo "${ASSET_REPO:?ASSET_REPO is not set}"
}

csmesh_compare() {
    gunzip -c direct/tests/fixtures/csmesh-reference.txt.gz > build/csmesh-reference.txt
    build/ios/csmesh-test build/game-assets/model build/csmesh-reference.txt > build/ios/csmesh-reference.log
    tail -n 3 build/ios/csmesh-reference.log
}

hspda_compare() {
    build/ios/hspda-test build/game-assets/model/chara_model.lst direct/tests/fixtures/hspda.reference > build/ios/hspda.log
    tail -n 1 build/ios/hspda.log
}

dsound_compare() {
    gunzip -c direct/tests/fixtures/dsound-reference.txt.gz > build/dsound-reference.txt
    build/ios/dsound-test build/game-assets build/dsound-reference.txt > build/ios/dsound-reference.log
    tail -n 1 build/ios/dsound-reference.log
}

zgp_compare() {
    gunzip -c direct/tests/fixtures/zgp-texture-corpus.reference.gz > build/zgp-texture-corpus.reference
    (
        cd build/game-assets
        for s in texture chrset sprite mesh model static-model objflags meshmemset boardang objlate unproje objinst \
                 objclno objcat clink objdraw texwrite24 zmat meshlines; do
            ../ios/zgp-trace-test "../../direct/tests/zgp/$s.script" "../../direct/tests/zgp/$s.reference"
        done
        ../ios/zgp-trace-test ../../direct/tests/zgp/texture-corpus.script ../zgp-texture-corpus.reference \
            > ../ios/zgp-texture-corpus.log
    )
}

zgp_static_models() {
    gunzip -c direct/tests/fixtures/zgp-static-model-corpus.reference.gz > build/zgp-static-model-corpus.reference
    (cd build/game-assets && ../ios/zgp-trace-test ../../direct/tests/zgp/static-model-corpus.script \
        ../zgp-static-model-corpus.reference > ../ios/zgp-static-model-corpus.log)
}

zgp_model_meshes() {
    gunzip -c direct/tests/fixtures/zgp-model-corpus.reference.gz > build/zgp-model-corpus.reference
    build/ios/d3dx-mesh-test build/game-assets/model direct/tests/zgp/model-corpus.script \
        build/zgp-model-corpus.reference > build/ios/d3dx-mesh-corpus.log
}

zgp_model_draws() {
    gunzip -c direct/tests/fixtures/zgp-model-draw-corpus.reference.gz > build/zgp-model-draw-corpus.reference
    (cd build/game-assets && ../ios/zgp-trace-test ../../direct/tests/zgp/model-draw-corpus.script \
        ../zgp-model-draw-corpus.reference > ../ios/zgp-model-draw-corpus.log)
}

simulator() {
    python3 tools/build_ios.py --simulator
    python3 tools/run_ios_simulator.py
}

toolchain() {
    xcodebuild -version
    xcrun --sdk iphoneos --show-sdk-version
}

export -f toolchain host_tests fetch_assets csmesh_compare hspda_compare dsound_compare zgp_compare \
    zgp_static_models zgp_model_meshes zgp_model_draws simulator

step "Apple toolchain" toolchain
step "Host compatibility tests" host_tests
step "Original asset package (download and hash verification)" fetch_assets
step "CSMesh against the original plugin, every game model" csmesh_compare
step "hspda against the original plugin" hspda_compare
step "DirectSound against the original plugin, every game sound" dsound_compare
step "ZGP traces and texture loading against the original plugin" zgp_compare
step "ZGP static models against the original plugin" zgp_static_models
step "CSMesh meshes against the original plugin" zgp_model_meshes
step "objdraw and CSMesh drawing against the original plugin" zgp_model_draws
step "Device build (unsigned IPA)" python3 tools/build_ios.py
if [ "${CI_SKIP_SIMULATOR:-0}" != "1" ]; then
    step "Simulator build and smoke test" simulator
fi
