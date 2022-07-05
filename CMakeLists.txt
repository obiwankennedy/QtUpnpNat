cmake_minimum_required(VERSION 3.16)

project(qtnat)
set(CMAKE_AUTOMOC ON)

set(QT_REQUIRED_VERSION "6.3.0")
find_package(Qt6 ${QT_REQUIRED_VERSION} CONFIG REQUIRED COMPONENTS Core Network)


add_library(qtnat SHARED upnpnat.cpp upnpnat.h)
target_link_libraries(qtnat PRIVATE Qt6::Core Qt6::Network)

add_executable(tst_upnp tst_upnp.cpp)
target_link_libraries(tst_upnp PRIVATE Qt6::Core Qt6::Network qtnat )

