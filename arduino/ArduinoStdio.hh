
// stdio.h emulation for arduino. Unfortunateley Arduino does not support stdio
// operations but it has different APIs which support SD drives. This is an
// attempt to make stdio configurable so that we can link it to a specific SD
// implementation

#pragma once

#include "FileAccess.hh"
