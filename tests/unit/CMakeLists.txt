add_executable(unit_tests
    test_arena.cpp
    test_slab.cpp
    test_spsc.cpp
    test_mpmc.cpp
    test_seqlock.cpp
    test_scheduler.cpp
    test_reactor.cpp
    test_work_stealing_deque.cpp
    test_actor.cpp
    test_proto.cpp
)

target_link_libraries(unit_tests PRIVATE
    jellybean
    GTest::gtest_main
)

add_test(NAME unit_tests COMMAND unit_tests)
