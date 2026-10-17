cmake_minimum_required(VERSION 3.13)
project(LmmsMinimalContracts LANGUAGES CXX)
include(CTest)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
# Hardware.h uses the same generated feature header as the application.
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" minimal_processor)
if(minimal_processor MATCHES "amd64|x86_64")
    set(LMMS_HOST_X86_64 TRUE)
elseif(minimal_processor MATCHES "aarch64|arm64")
    set(LMMS_HOST_ARM64 TRUE)
elseif(minimal_processor MATCHES "i.86|x86")
    set(LMMS_HOST_X86 TRUE)
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../src/lmmsconfig.h.in" "${CMAKE_CURRENT_BINARY_DIR}/lmmsconfig.h")
set(LMMS_QT_MAJOR 5 CACHE STRING "Qt major version")
get_filename_component(LMMS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(Qt${LMMS_QT_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Test)
include("${LMMS_ROOT}/cmake/LmmsProjectCodec.cmake")
include("${LMMS_ROOT}/cmake/LmmsTestRuntime.cmake")
set(QT_VERSION_MAJOR "${LMMS_QT_MAJOR}")
lmms_ensure_test_launcher("${LMMS_ROOT}")

function(lmms_minimal_test name)
	add_executable(${name} ${ARGN})
	target_include_directories(${name} PRIVATE "${LMMS_ROOT}/include" "${CMAKE_CURRENT_BINARY_DIR}")
	target_link_libraries(${name} PRIVATE Qt${LMMS_QT_MAJOR}::Core Qt${LMMS_QT_MAJOR}::Test)
	target_link_libraries(${name} PRIVATE lmms_project_codec_runtime)
	lmms_add_qtest(${name} ${name} -o -,txt)
	set_tests_properties(${name} PROPERTIES TIMEOUT 60 LABELS "lmms-minimal")
endfunction()

lmms_minimal_test(SaveTransactionTest
	"${LMMS_ROOT}/tests/src/core/SaveTransactionTest.cpp"
	"${LMMS_ROOT}/src/core/SaveTransaction.cpp")
lmms_minimal_test(ProjectCodecTest
	"${LMMS_ROOT}/tests/src/core/ProjectCodecTest.cpp"
	"${LMMS_ROOT}/src/core/ProjectCodec.cpp")
lmms_minimal_test(BundleTransactionTest
	"${LMMS_ROOT}/tests/src/core/BundleTransactionTest.cpp"
	"${LMMS_ROOT}/src/core/BundleTransaction.cpp"
	"${LMMS_ROOT}/src/core/ProjectCodec.cpp"
	"${LMMS_ROOT}/src/core/RecoveryIdentity.cpp")
lmms_minimal_test(RecoveryIdentityTest
	"${LMMS_ROOT}/tests/src/core/RecoveryIdentityTest.cpp"
	"${LMMS_ROOT}/src/core/RecoveryIdentity.cpp")
lmms_minimal_test(WorkerDeadlineTest
    "${LMMS_ROOT}/tests/src/core/WorkerDeadlineTest.cpp"
    "${LMMS_ROOT}/src/core/AudioEngineWorkerThread.cpp"
    "${LMMS_ROOT}/include/AudioEngineWorkerThread.h")
lmms_minimal_test(ReleaseQueueTest
	"${LMMS_ROOT}/tests/src/core/ReleaseQueueTest.cpp")

if(WIN32)
	add_library(lmms_missing_dep SHARED "${LMMS_ROOT}/tests/runtime/missing_dep.cpp")
	add_executable(lmms_missing_dll_child "${LMMS_ROOT}/tests/runtime/missing_child.cpp")
	target_link_libraries(lmms_missing_dll_child PRIVATE lmms_missing_dep)
	file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/loader-paths/$<CONFIG>/loader_paths.h" CONTENT
		"#define LMMS_LAUNCHER_PATH R\"($<TARGET_FILE:lmms_test_launcher>)\"\n#define LMMS_CHILD_PATH R\"($<TARGET_FILE:lmms_missing_dll_child>)\"\n#define LMMS_DEP_DLL R\"($<TARGET_FILE:lmms_missing_dep>)\"\n")
	add_executable(WindowsLoaderTest "${LMMS_ROOT}/tests/src/core/WindowsLoaderTest.cpp")
	target_include_directories(WindowsLoaderTest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/loader-paths/$<CONFIG>")
	target_link_libraries(WindowsLoaderTest PRIVATE Qt${LMMS_QT_MAJOR}::Core Qt${LMMS_QT_MAJOR}::Test)
	target_compile_features(WindowsLoaderTest PRIVATE cxx_std_20)
	add_dependencies(WindowsLoaderTest lmms_test_launcher lmms_missing_dll_child lmms_missing_dep)
	lmms_add_qtest(WindowsLoaderTest WindowsLoaderTest -o -,txt)
	set_tests_properties(WindowsLoaderTest PROPERTIES TIMEOUT 30 LABELS "lmms-windows")
endif()

# Link the actual waveform renderer and storage, without the decoder or engine.
include(GenerateExportHeader)
add_library(lmms_sample_storage STATIC "${LMMS_ROOT}/src/core/SampleBufferStorage.cpp")
generate_export_header(lmms_sample_storage BASE_NAME LMMS EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/lmms_export.h")
target_compile_definitions(lmms_sample_storage PUBLIC LMMS_STATIC_DEFINE)
target_include_directories(lmms_sample_storage PUBLIC "${LMMS_ROOT}/include" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(lmms_sample_storage PUBLIC Qt${LMMS_QT_MAJOR}::Core)
lmms_minimal_test(SampleThumbnailTest "${LMMS_ROOT}/tests/src/gui/SampleThumbnailTest.cpp" "${LMMS_ROOT}/src/gui/SampleThumbnail.cpp")
target_link_libraries(SampleThumbnailTest PRIVATE lmms_sample_storage Qt${LMMS_QT_MAJOR}::Widgets)
set_tests_properties(SampleThumbnailTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
