#if !defined(__HDRMERGE_H)
#define __HDRMERGE_H

#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <cmath>
#include <iostream>
#include <stdint.h>
#include <memory>

using std::cout;
using std::cerr;
using std::endl;

typedef std::map<std::string, std::string> StringMap;

struct Exposure {
	std::string filename;
	float exposure;
	float shown_exposure;
	float *image;

	inline Exposure(const std::string &filename)
	 : filename(filename), exposure(-1), image(NULL) { }

	~Exposure() {
		if (image)
			delete[] image;
	}

	/// Return the exposure has a human-readable string
	std::string toString() const {
		char buf[10];
		if (exposure < 1)
			snprintf(buf, sizeof(buf), "1/%.4g", 1/exposure);
		else
			snprintf(buf, sizeof(buf), "%.4g", exposure);
		return buf;
	}
};

struct ExposureSeries {
	std::vector<Exposure> exposures;
	StringMap metadata;
	size_t width, height;
	float saturation;

	/**
	 * Add a file to the exposure series (or, optionally, a sequence
	 * such as file_%03i.png expressed using the printf-style format)
	 */
	void add(const char *filename);

	/**
	 * Check that all exposures are valid, and that they satisfy
	 * a few basic requirements such as:
	 *  - all images use the same ISO speed and aperture setting
	 *  - the images were taken using manual focus and manual exposure mode
     *  - there are no duplicate exposures.
	 *
	 * This also sorts the exposures in case they weren't ordered already
	 */
	void check();

	/**
	 * Run dcraw on an entire exposure series (in parallel)
	 * and fill the exposure series with a normalized RGB floating
	 * point image representation
	 */
	void load();

	/// Evaluate a given pixel of an exposure series
	inline float eval(size_t img, size_t x, size_t y) const {
		return exposures[img].image[x+width*y];
	}

	/// Evaluate a given pixel of an exposure series (Bayer grid)
	inline float eval(size_t img, size_t x, size_t y, int ch) const {
		x = x*2 + ch&1;
		y = y*2 + (ch&2) >> 1;
		return exposures[img].image[x+width*y];
	}

	/// Return the number of exposures
	inline size_t size() const {
		return exposures.size();
	}
};

/// Return the number of processors available for multithreading
extern int getProcessorCount();

/**
 * Write a lossless floating point OpenEXR file using either half or
 * single precision
 */
extern void writeOpenEXR(const std::string &filename, size_t w, size_t h,
	int channels, float *data, const StringMap &metadata, bool writeHalf);

#define RS_SCALE (1.0f / (1.0f + RAND_MAX))

/// Generate a uniformly distributed random number in [0, 1)
inline float randf() {
    float f;
    do {
       f = (((rand () * RS_SCALE) + rand ()) * RS_SCALE + rand()) * RS_SCALE;
    } while (f >= 1); /* Round off */

    return f;
}

#endif /* __HDRMERGE_H */
