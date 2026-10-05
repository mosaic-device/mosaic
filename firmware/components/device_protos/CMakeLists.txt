# platform/packages/protocols owns generated/events.pb.c and events.pb.h.
# A fresh checkout must include those committed generator outputs before idf.py build.
idf_component_register(
    SRCS "generated/events.pb.c"
    INCLUDE_DIRS "generated"
    REQUIRES nanopb
)
