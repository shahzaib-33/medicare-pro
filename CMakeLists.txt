cmake_minimum_required(VERSION 3.21)
project(MediCarePro VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Sql Network Concurrent Charts PrintSupport LinguistTools Test)
find_package(OpenSSL REQUIRED)

if(MSVC)
    add_compile_options(/utf-8 /W3 /permissive-)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

option(MEDICARE_BUILD_TESTS "Build unit and acceptance tests" ON)

# Core library: everything except main(). Shared by the app, tests and tools.
add_library(medicare_core STATIC
    src/app/AppPaths.h src/app/AppPaths.cpp
    src/app/PerformanceProfile.h src/app/PerformanceProfile.cpp
    src/app/Version.h
    src/db/Database.h src/db/Database.cpp
    src/db/Schema.h src/db/Schema.cpp
    src/core/Money.h src/core/Money.cpp
    src/services/Common.h src/services/Common.cpp
    src/services/AuthService.h src/services/AuthService.cpp
    src/services/InventoryService.h src/services/InventoryService.cpp
    src/services/PurchaseService.h src/services/PurchaseService.cpp
    src/services/SalesService.h src/services/SalesService.cpp
    src/services/BackupService.h src/services/BackupService.cpp
    src/services/SettingsService.h src/services/SettingsService.cpp
    src/services/ReceiptBuilder.h src/services/ReceiptBuilder.cpp
    src/services/ReportService.h src/services/ReportService.cpp
    src/refdata/TextNormalizer.h src/refdata/TextNormalizer.cpp
    src/refdata/MedicineRecord.h
    src/refdata/SearchIndexer.h src/refdata/SearchIndexer.cpp
    src/refdata/MedicineSearch.h src/refdata/MedicineSearch.cpp
    src/refdata/CsvReader.h src/refdata/CsvReader.cpp
    src/refdata/CatalogImporter.h src/refdata/CatalogImporter.cpp
    src/refdata/CatalogStats.h src/refdata/CatalogStats.cpp
    src/refdata/PharmacyCatalog.h src/refdata/PharmacyCatalog.cpp
    src/update/Signature.h src/update/Signature.cpp
    src/update/UpdateManifest.h src/update/UpdateManifest.cpp
    src/update/ReferenceUpdater.h src/update/ReferenceUpdater.cpp
    src/update/UpdateClient.h src/update/UpdateClient.cpp
    src/update/SoftwareUpdater.h src/update/SoftwareUpdater.cpp
    src/update/UpdateLog.h src/update/UpdateLog.cpp
    src/net/ConnectivityMonitor.h src/net/ConnectivityMonitor.cpp
)
target_include_directories(medicare_core PUBLIC src)
target_compile_definitions(medicare_core PUBLIC MEDICARE_VERSION="${PROJECT_VERSION}")
target_link_libraries(medicare_core PUBLIC Qt6::Core Qt6::Sql Qt6::Network Qt6::Concurrent OpenSSL::Crypto)

qt_add_executable(MediCarePro WIN32
    src/main.cpp
    src/ui/Theme.h src/ui/Theme.cpp
    src/ui/MainWindow.h src/ui/MainWindow.cpp
    src/ui/MedicineDbCard.h src/ui/MedicineDbCard.cpp
    src/ui/MedicinesPage.h src/ui/MedicinesPage.cpp
    src/ui/CatalogBrowserDialog.h src/ui/CatalogBrowserDialog.cpp
    src/ui/AddToPharmacyDialog.h src/ui/AddToPharmacyDialog.cpp
    src/ui/UpdateCenterPage.h src/ui/UpdateCenterPage.cpp
    src/ui/SettingsPage.h src/ui/SettingsPage.cpp
    src/ui/OfflineBanner.h src/ui/OfflineBanner.cpp
    src/ui/AppContext.h
    src/ui/Ui.h src/ui/Ui.cpp
    src/ui/LoginDialog.h src/ui/LoginDialog.cpp
    src/ui/ReceiptDialog.h src/ui/ReceiptDialog.cpp
    src/ui/DashboardPage.h src/ui/DashboardPage.cpp
    src/ui/PosPage.h src/ui/PosPage.cpp
    src/ui/SalesPage.h src/ui/SalesPage.cpp
    src/ui/InventoryPage.h src/ui/InventoryPage.cpp
    src/ui/PurchasesPage.h src/ui/PurchasesPage.cpp
    src/ui/PartiesPage.h src/ui/PartiesPage.cpp
    src/ui/OperationsPages.h src/ui/OperationsPages.cpp
    src/ui/ReportsPage.h src/ui/ReportsPage.cpp
    src/ui/UsersPage.h src/ui/UsersPage.cpp
    src/ui/BackupPage.h src/ui/BackupPage.cpp
    resources/resources.qrc
)
if(WIN32)
    target_sources(MediCarePro PRIVATE resources/app.rc)
endif()
target_link_libraries(MediCarePro PRIVATE medicare_core Qt6::Widgets Qt6::Charts Qt6::PrintSupport)

get_target_property(_core_sources medicare_core SOURCES)
get_target_property(_app_sources MediCarePro SOURCES)
list(FILTER _app_sources INCLUDE REGEX "\\.(cpp|h)$")
list(TRANSFORM _core_sources PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(TRANSFORM _app_sources PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
qt_add_translations(MediCarePro TS_FILES translations/medicarepro_ur.ts
    SOURCES ${_core_sources} ${_app_sources}
    RESOURCE_PREFIX /i18n LUPDATE_OPTIONS -no-obsolete)

# Command line tool for SoftzenAgency: builds and signs update packages.
add_executable(refpack tools/refpack/main.cpp)
target_link_libraries(refpack PRIVATE medicare_core)

if(MEDICARE_BUILD_TESTS)
    enable_testing()
    foreach(t test_normalizer test_import_search test_updates test_acceptance test_workflows)
        add_executable(${t} tests/${t}.cpp tests/TestUtil.h)
        target_link_libraries(${t} PRIVATE medicare_core Qt6::Test)
        add_test(NAME ${t} COMMAND ${t})
    endforeach()
endif()

install(TARGETS MediCarePro RUNTIME DESTINATION .)
