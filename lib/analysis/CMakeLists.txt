target_sources(libdragon
        PRIVATE
        SliceExpr.cpp
        BackwardSlicer.cpp
        SliceExprZ3.cpp
        ValueEnumerator.cpp
)

target_link_libraries(libdragon PRIVATE z3::libz3)