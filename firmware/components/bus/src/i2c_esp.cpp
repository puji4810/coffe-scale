// Everything is inline in i2c_esp.hpp; this file exists so the component has
// a translation unit and to catch header self-containment issues.
#include "bus/i2c_esp.hpp"

static_assert(bus::i2c_device<bus::i2c_dev_esp>);
