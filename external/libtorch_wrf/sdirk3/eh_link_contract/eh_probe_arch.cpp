// Archived thrower for probe 3 (compiled into a static .a first).
#include <stdexcept>
int eh_probe_archive_throw() {
    throw std::runtime_error("EH_PROBE_ARCHIVE");
}
