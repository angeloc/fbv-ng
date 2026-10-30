// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "image.hpp"

namespace fbvng {

// Nearest neighbour resize.
Image resize_nearest(const Image &src, int width, int height);

// Box filter for shrinking; alpha is resized by nearest neighbour.
Image resize_average(const Image &src, int width, int height);

// Rotates clockwise by quarter_turns * 90 degrees.
Image rotate(const Image &src, int quarter_turns);

} // namespace fbvng
