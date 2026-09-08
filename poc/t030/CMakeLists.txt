# T-030 PoC providers: lógica offline (Qt6Core) + selfcheck ejecutable.
# No se instala nada: la lib valida compilación en el build normal;
# el exe se compila solo bajo demanda (--target provider-poc-selfcheck).

find_package(Qt6 COMPONENTS Core REQUIRED)

add_library(provider-poc STATIC provider_poc.cpp)
target_link_libraries(provider-poc PRIVATE Qt6::Core)
target_include_directories(provider-poc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(provider-poc PROPERTIES AUTOMOC ON)

add_executable(provider-poc-selfcheck EXCLUDE_FROM_ALL selfcheck.cpp)
target_link_libraries(provider-poc-selfcheck PRIVATE provider-poc Qt6::Core)
