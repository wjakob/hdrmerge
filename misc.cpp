#include "hdrmerge.h"
#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>
#include <thread>

namespace po = boost::program_options;

ERotateFlipType flipTypeFromString(int rotation, std::string axes) {
    axes = boost::to_lower_copy(axes);
    if (axes == "yx") axes = "xy";

    if (rotation == 0   && axes == "")   return ERotateNoneFlipNone;
    if (rotation == 0   && axes == "x")  return ERotateNoneFlipX;
    if (rotation == 0   && axes == "y")  return ERotateNoneFlipY;
    if (rotation == 0   && axes == "xy") return ERotateNoneFlipXY;
    if (rotation == 90  && axes == "")   return ERotate90FlipNone;
    if (rotation == 90  && axes == "x")  return ERotate90FlipX;
    if (rotation == 90  && axes == "y")  return ERotate90FlipY;
    if (rotation == 90  && axes == "xy") return ERotate90FlipXY;
    if (rotation == 180 && axes == "")   return ERotate180FlipNone;
    if (rotation == 180 && axes == "x")  return ERotate180FlipX;
    if (rotation == 180 && axes == "y")  return ERotate180FlipY;
    if (rotation == 180 && axes == "xy") return ERotate180FlipXY;
    if (rotation == 270 && axes == "")   return ERotate270FlipNone;
    if (rotation == 270 && axes == "x")  return ERotate270FlipX;
    if (rotation == 270 && axes == "y")  return ERotate270FlipY;
    if (rotation == 270 && axes == "xy") return ERotate270FlipXY;

    throw std::runtime_error("The argument to --rotate must be one of "
        "0, 90, 180 or 270, and the argument to --flip must be one of x, y, or xy");
}

void rotateFlip(
        uint8_t *src,  size_t  s_width, size_t  s_height,
        uint8_t *&dst, size_t &t_width, size_t &t_height,
        int bypp, ERotateFlipType type) {
    bool rotate_90 = type&1;

    bool flip_x = (type & 6) == 2 || (type & 6) == 4;
    bool flip_y = (type & 3) == 1 || (type & 3) == 2;

    t_width = s_width; t_height = s_height;

    if (rotate_90)
        std::swap(t_width, t_height);

    int src_stride = s_width * bypp,
        dst_stride = t_width * bypp;

    dst = new uint8_t[t_width*t_height*bypp];

    uint8_t *dst_row = dst, *src_row = src;
    if (flip_x)
        src_row += bypp * (s_width - 1);

    if (flip_y)
        src_row += src_stride * (s_height - 1);

    int src_x_step, src_y_step;
    if (rotate_90) {
        src_x_step = flip_y ? -src_stride : src_stride;
        src_y_step = flip_x ? -bypp : bypp;
    } else {
        src_x_step = flip_x ? -bypp : bypp;
        src_y_step = flip_y ? -src_stride : src_stride;
    }

    for (size_t y=0; y<t_height; y++) {
        uint8_t *src_pixel = src_row;
        uint8_t *dst_pixel = dst_row;

        for (size_t x=0; x<t_width; x++) {
            memcpy(dst_pixel, src_pixel, bypp);
            dst_pixel += bypp;
            src_pixel += src_x_step;
        }

        src_row += src_y_step;
        dst_row += dst_stride;
    }
}

std::istream& operator>>(std::istream& in, EColorMode& unit) {
    std::string token;
    in >> token;
    std::string token_lc = boost::to_lower_copy(token);

    if (token_lc == "native")
        unit = ENative;
    else if (token_lc == "srgb")
        unit = ESRGB;
    else if (token_lc == "xyz")
        unit = EXYZ;
    else
        throw po::validation_error(po::validation_error::invalid_option_value, "colormode", token);
    return in;
}

int getProcessorCount() {
    return std::thread::hardware_concurrency();
}

